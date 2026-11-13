#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The host side of the framed byte stream (SPEC-015 §7, OBS-019).

    frame.py [--port /dev/cu.usbmodem1103 --baud 115200 | FILE] [--channel N] [--raw]

A frame is COBS(channel u8, payload, CRC-16) followed by a zero byte. The CRC is
CRC-16/IBM-3740 (polynomial 0x1021, initial value 0xFFFF, not reflected) over the channel
byte and the payload, sent least significant byte first. Channels 0 log, 1 trace, 2 crash,
3 shell, 4 update; 5 to 15 the application's (AMENDMENTS-M4 A113).

As a library: encode(), decode() for one frame, and Decoder for a stream, which drops bad
frames and resynchronizes at the next zero, as the target's decoder does. As a tool: prints
each good frame of a stream, from a serial port (pyserial) or a file of captured bytes, and
the counts at the end.
"""
import argparse
import sys

CHANNEL_MAX = 15
CHANNELS = {0: "log", 1: "trace", 2: "crash", 3: "shell", 4: "update"}


class FrameError(ValueError):
    pass


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def cobs_encode(data):
    out, run = bytearray(), bytearray()
    for b in data:
        if b == 0:
            out += bytes([len(run) + 1]) + run
            run = bytearray()
            continue
        run.append(b)
        if len(run) == 254:
            out += bytes([255]) + run
            run = bytearray()
    out += bytes([len(run) + 1]) + run
    return bytes(out)


def cobs_decode(data):
    out, i = bytearray(), 0
    while i < len(data):
        code = data[i]
        if code == 0:
            raise FrameError("a zero inside a frame")
        block = data[i + 1:i + code]
        if len(block) != code - 1:
            raise FrameError("a block cut short")
        out += block
        i += code
        if code != 255 and i < len(data):
            out.append(0)
    return bytes(out)


def encode(channel, payload=b""):
    """One frame's bytes, the delimiter included."""
    if not 0 <= channel <= CHANNEL_MAX:
        raise FrameError(f"channel {channel} is above {CHANNEL_MAX}")
    body = bytes([channel]) + bytes(payload)
    crc = crc16(body)
    return cobs_encode(body + bytes([crc & 0xFF, crc >> 8])) + b"\0"


def decode(frame):
    """(channel, payload) of one frame, with or without its delimiter."""
    raw = cobs_decode(frame[:-1] if frame.endswith(b"\0") else frame)
    if len(raw) < 3:
        raise FrameError("too short for a channel and a CRC")
    crc = crc16(raw[:-2])
    if raw[-2] != crc & 0xFF or raw[-1] != crc >> 8:
        raise FrameError("bad CRC")
    if raw[0] > CHANNEL_MAX:
        raise FrameError(f"channel {raw[0]} is above {CHANNEL_MAX}")
    return raw[0], raw[1:-2]


class Decoder:
    """A stream decoder: feed() bytes as they arrive, get the good frames back."""

    def __init__(self, max_payload=4096):
        self.max = max_payload + 3
        self.buf = bytearray()
        self.dropping = False
        self.frames = self.errors = self.overruns = 0

    def feed(self, data):
        out = []
        for b in data:
            if b != 0:
                if not self.dropping:
                    self.buf.append(b)
                    if len(self.buf) > self.max * 2:  # COBS adds little: this is no frame
                        self.dropping, self.buf = True, bytearray()
                        self.overruns += 1
                continue
            if self.dropping:
                self.dropping = False
            elif self.buf:
                try:
                    ch, payload = decode(bytes(self.buf))
                    if len(payload) + 3 > self.max:
                        self.overruns += 1
                    else:
                        out.append((ch, payload))
                        self.frames += 1
                except FrameError:
                    self.errors += 1
            self.buf = bytearray()
        return out

    def feed_all(self, data):
        """As feed(), keeping what is not a frame: ("frame", channel, payload) for a good frame,
        ("raw", bytes, None) for bytes between zeros that are not one and read as text (a boot
        ROM's message, text before a framed console started). A framed console precedes each
        frame with a zero, so such text ends before the next frame (A127)."""
        out = []
        for b in data:
            if b != 0:
                if not self.dropping:
                    self.buf.append(b)
                    if len(self.buf) > self.max * 2:
                        if _texty(self.buf):
                            out.append(("raw", bytes(self.buf), None))
                        else:
                            self.overruns += 1
                        self.buf = bytearray()
                continue
            self.dropping = False
            if self.buf:
                out.extend(self._end())
            self.buf = bytearray()
        return out

    def finish(self):
        """The bytes after the last zero, when they read as text."""
        out = [("raw", bytes(self.buf), None)] if self.buf and _texty(self.buf) else []
        self.buf = bytearray()
        return out

    def _end(self):
        try:
            ch, payload = decode(bytes(self.buf))
        except FrameError:
            if _texty(self.buf):
                return [("raw", bytes(self.buf), None)]
            self.errors += 1
            return []
        if len(payload) + 3 > self.max:
            self.overruns += 1
            return []
        self.frames += 1
        return [("frame", ch, payload)]


def _texty(buf):
    """Whether bytes read as text: printable ASCII, tabs and line ends, at least nine in ten."""
    if not buf:
        return False
    good = sum(1 for b in buf if 32 <= b < 127 or b in (9, 10, 13))
    return good * 10 >= len(buf) * 9


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("file", nargs="?", help="a file of captured bytes")
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--channel", type=int, action="append", help="only these channels")
    ap.add_argument("--raw", action="store_true", help="payloads in hexadecimal, not text")
    args = ap.parse_args(argv)
    if (args.file is None) == (args.port is None):
        ap.error("give a file or --port")
    dec = Decoder()

    def show(frames):
        for ch, payload in frames:
            if args.channel and ch not in args.channel:
                continue
            text = payload.hex() if args.raw else payload.decode("utf-8", "replace").rstrip("\n")
            print(f"[{CHANNELS.get(ch, ch)}] {text}", flush=True)

    try:
        if args.file:
            with open(args.file, "rb") as f:
                show(dec.feed(f.read()))
        else:
            import serial  # pyserial, only for a live port
            with serial.Serial(args.port, args.baud, timeout=0.2) as s:
                while True:
                    show(dec.feed(s.read(256)))
    except KeyboardInterrupt:
        pass
    print(f"frame: {dec.frames} frames, {dec.errors} errors, {dec.overruns} overruns", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
