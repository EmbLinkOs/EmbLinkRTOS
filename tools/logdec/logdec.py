#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Decode deferred log records with the image's ELF (SPEC-015 §2.5, OBS-020, A127).

    logdec.py --elf build/app/app.elf console.bin       # a framed console capture
    some-capture | logdec.py --elf app.elf -            # from standard input
    logdec.py --elf app.elf --records payload.bin       # bare records, without frames

A call site's metadata is in the ELF's emb_log section, at the record's identifier less 8; the
section never reaches the image (A127). Records arrive on channel 0 of SPEC-015 §7's frames,
whole records to a frame. The console's text on channel 3, and bytes that are not a frame, are
printed as they are. The stream's start record names the image's build id: a stream from
another image is refused (exit 3).
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "transport"))
import frame as framing  # noqa: E402

ID_SYNC, ID_DROPPED, ID_START, ID_FIRST = 0, 1, 2, 8
T_END, T_INT, T_UINT, T_LONG, T_ULONG, T_LLONG, T_ULLONG, T_DOUBLE, T_STR, T_PTR, T_HEX = range(11)
LEVELS = "?EWID"
CH_LOG, CH_TRACE, CH_CRASH, CH_SHELL = 0, 1, 2, 3


class LogError(Exception):
    pass


class Elf:
    """The sections and symbols of an ELF file: enough to read the log's metadata."""

    def __init__(self, path):
        self.data = data = Path(path).read_bytes()
        if data[:4] != b"\x7fELF":
            raise LogError(f"{path}: not an ELF file (a native image on macOS is Mach-O: "
                           "decode a Linux or embedded build)")
        self.cls = data[4]
        self.end = "<" if data[5] == 1 else ">"
        e = self.end
        if self.cls == 1:
            shoff, = struct.unpack_from(e + "I", data, 0x20)
            shentsize, shnum, shstrndx = struct.unpack_from(e + "HHH", data, 0x2E)
            fmt, self.ptr = e + "IIIIIIIIII", 4
        else:
            shoff, = struct.unpack_from(e + "Q", data, 0x28)
            shentsize, shnum, shstrndx = struct.unpack_from(e + "HHH", data, 0x3A)
            fmt, self.ptr = e + "IIQQQQIIQQ", 8
        self.machine, = struct.unpack_from(e + "H", data, 18)
        raw = [struct.unpack_from(fmt, data, shoff + i * shentsize) for i in range(shnum)]
        names = raw[shstrndx]
        self.sections = []
        for s in raw:
            n = data[names[4] + s[0]:data.index(b"\0", names[4] + s[0])].decode()
            self.sections.append({"name": n, "type": s[1], "addr": s[3], "offset": s[4], "size": s[5],
                                  "link": s[6]})
        self.symbols = {}
        for s in self.sections:
            if s["type"] != 2:  # SHT_SYMTAB
                continue
            strtab = self.sections[s["link"]]
            size = 16 if self.cls == 1 else 24
            for off in range(s["offset"], s["offset"] + s["size"], size):
                if self.cls == 1:
                    st_name, value, sz, _, _, shndx = struct.unpack_from(e + "IIIBBH", data, off)
                else:
                    st_name, _, _, shndx, value, sz = struct.unpack_from(e + "IBBHQQ", data, off)
                at = strtab["offset"] + st_name
                name = data[at:data.index(b"\0", at)].decode(errors="replace")
                if name and shndx not in (0, 0xFFF1):
                    self.symbols.setdefault(name, (value, sz, shndx))

    def section(self, name):
        return next((s for s in self.sections if s["name"] == name), None)

    def read(self, addr, size, within=None):
        """Bytes at an address: in the given section, else in any section that holds them."""
        for s in ([within] if within else []) + self.sections:
            if s and s["type"] != 8 and s["addr"] <= addr and addr + size <= s["addr"] + s["size"] \
                    and (s["addr"] != 0 or s is within):
                at = s["offset"] + addr - s["addr"]
                return self.data[at:at + size]
        raise LogError(f"no section holds 0x{addr:x}")

    def cstring(self, addr, within=None):
        for s in ([within] if within else []) + self.sections:
            if s and s["type"] != 8 and s["addr"] <= addr < s["addr"] + s["size"] \
                    and (s["addr"] != 0 or s is within):
                at = s["offset"] + addr - s["addr"]
                return self.data[at:self.data.index(b"\0", at)].decode(errors="replace")
        return "?"

    def symbol_bytes(self, name):
        if name not in self.symbols:
            return None
        value, size, shndx = self.symbols[name]
        sec = self.sections[shndx]
        at = sec["offset"] + value - sec["addr"]
        return self.data[at:at + size]


class Meta:
    def __init__(self, fmt, file, module, line, sig, level):
        self.format, self.file, self.module, self.line, self.sig, self.level = fmt, file, module, line, sig, level


class LogDb:
    """The image's call sites and the record layout, from its ELF."""

    def __init__(self, elf_path):
        self.elf = elf = Elf(elf_path)
        self.sec = sec = elf.section("emb_log")
        if sec is None:
            raise LogError(f"{elf_path}: no emb_log section: built with the plain-text backend, "
                           "or nothing logs")
        start = elf.symbols.get("__start_emb_log", (sec["addr"],))[0]
        stop = elf.symbols.get("__stop_emb_log", (sec["addr"] + sec["size"],))[0]
        self.base = start
        self.id_bytes = 2 if (stop - start) <= 0xFFFF - ID_FIRST else 4
        hdr = elf.symbol_bytes("emb_log_header")
        if hdr is None or hdr[:4] != b"EMBL":
            raise LogError(f"{elf_path}: no emb_log_header")
        self.version, flags, self.long_size, self.ptr_size, self.double_size, self.str_max = hdr[4:10]
        self.timestamps = bool(flags & 1)
        self.build_id = elf.symbol_bytes("emb_build_id")
        self.end = elf.end
        self._metas = {}

    def meta(self, rid):
        if rid in self._metas:
            return self._metas[rid]
        p, e = self.ptr_size, self.end
        raw = self.elf.read(self.base + rid - ID_FIRST, 3 * p + 9, self.sec)
        pf = e + ("I" if p == 4 else "Q")
        fmt, file, module = (struct.unpack_from(pf, raw, i * p)[0] for i in range(3))
        line, sig = struct.unpack_from(e + "II", raw, 3 * p)
        level = raw[3 * p + 8]
        m = Meta(self.elf.cstring(fmt, self.sec), self.elf.cstring(file, self.sec) if file else "",
                 self.elf.cstring(module, self.sec), line, sig, level)
        self._metas[rid] = m
        return m


# ---------------------------------------------------------------------------- formatting

def _signed(v, bits):
    return v - (1 << bits) if v >> (bits - 1) else v


def format_c(fmt, args):
    """printf's subset the target uses (SPEC-015 §2.1), with the arguments a record carries:
    (type, value) pairs, ints already sized."""
    out, i, k = [], 0, 0
    while i < len(fmt):
        c = fmt[i]
        if c != "%":
            out.append(c)
            i += 1
            continue
        j = i + 1
        if j < len(fmt) and fmt[j] == "%":
            out.append("%")
            i = j + 1
            continue
        flags = ""
        while j < len(fmt) and fmt[j] in "-0+ #":
            flags += fmt[j]
            j += 1
        width = ""
        while j < len(fmt) and fmt[j].isdigit():
            width += fmt[j]
            j += 1
        prec = None
        if j < len(fmt) and fmt[j] == ".":
            j += 1
            p = ""
            while j < len(fmt) and fmt[j].isdigit():
                p += fmt[j]
                j += 1
            prec = int(p or "0")
        h = 0
        while j < len(fmt) and fmt[j] in "hlzjtL":
            h += fmt[j] == "h"
            j += 1
        if j >= len(fmt):
            break
        conv = fmt[j]
        i = j + 1
        t, v, bits = args[k] if k < len(args) else (T_END, 0, 32)
        k += 1
        spec = "%" + flags + width + ("" if prec is None else f".{prec}")
        if conv == "s":
            s = v if isinstance(v, str) else str(v)
            out.append(("%" + flags.replace("0", "") + width + ("" if prec is None else f".{prec}") + "s") % s)
        elif conv == "c":
            out.append(("%" + flags.replace("0", "") + width + "c") % chr(v & 0xFF))
        elif conv in "fFeEgG":
            # the target prints %e and %g as %f (A127): the same here, for the same lines
            d = float(v)
            p = 6 if prec is None else min(prec, 9)
            out.append(("%" + flags + width + f".{p}f") % d)
        elif conv == "p":
            s = format(v, "x")
            out.append(("%" + flags.replace("0", "").replace("#", "") + width + "s") % ("0x" + s))
        else:
            if h:
                bits = 8 if h >= 2 else 16
                v &= (1 << bits) - 1
                if conv in "di":
                    v = _signed(v, bits)
            if conv in "di":
                if t in (T_UINT, T_ULONG, T_ULLONG, T_PTR) and not h:
                    v = _signed(v & ((1 << bits) - 1), bits)
                out.append((spec + "d") % v)
            else:
                v &= (1 << bits) - 1
                if conv == "u":
                    out.append((spec + "d") % v)
                elif conv in "xXo":
                    out.append((spec + conv) % v)
                else:
                    out.append((spec + "d") % v)
    return "".join(out)


class Stream:
    """Records to text lines, with the image's metadata."""

    def __init__(self, db, where=False, tick_ns=None):
        self.db = db
        self.where = where
        self.tick_ns = tick_ns
        self.started = False

    def _ts(self, ticks):
        if ticks is None or not self.tick_ns:
            return ""
        ms = ticks * self.tick_ns // 1_000_000
        return f"[{ms // 1000}.{ms % 1000:03d}] "

    def records(self, payload):
        """The lines of one frame's records."""
        db, at, lines = self.db, 0, []
        le = "<" if db.end == "<" else ">"

        def take(n):
            nonlocal at
            if at + n > len(payload):
                raise LogError("a record cut short")
            b = payload[at:at + n]
            at += n
            return b

        def uint(n):
            return int.from_bytes(take(n), "little" if le == "<" else "big")

        while at < len(payload):
            rid = uint(db.id_bytes)
            ts = uint(4) if db.timestamps else None
            if rid == ID_START:
                build_id = take(20)
                self.tick_ns = uint(4)
                if db.build_id is not None and build_id != db.build_id:
                    raise LogError(f"the stream is from build {build_id.hex()}, the ELF is "
                                   f"{db.build_id.hex()}: decode it with its own ELF")
                self.started = True
                continue
            if rid == ID_DROPPED:
                lines.append(f"{self._ts(ts)}W emb_log: {uint(2)} records dropped")
                continue
            if rid == ID_SYNC:
                continue
            if rid < ID_FIRST:
                raise LogError(f"record {rid}: an identifier the stream reserves")
            m = db.meta(rid)
            head = f"{self._ts(ts)}{LEVELS[m.level] if m.level < len(LEVELS) else '?'} {m.module}: "
            if m.sig == T_HEX:
                n = uint(2)
                text = m.format + ":" + "".join(f" {b:02x}" for b in take(n))
            else:
                args = []
                for k in range(8):
                    t = (m.sig >> (4 * k)) & 0xF
                    if t == T_END or t > T_HEX:
                        break
                    if t in (T_INT, T_UINT):
                        args.append((t, uint(4), 32))
                    elif t in (T_LONG, T_ULONG):
                        args.append((t, uint(db.long_size), 8 * db.long_size))
                    elif t in (T_LLONG, T_ULLONG):
                        args.append((t, uint(8), 64))
                    elif t == T_PTR:
                        args.append((t, uint(db.ptr_size), 8 * db.ptr_size))
                    elif t == T_DOUBLE:
                        raw = take(db.double_size)
                        f = "<d" if db.double_size == 8 else "<f"
                        args.append((t, struct.unpack(f if le == "<" else ">" + f[1], raw)[0], 64))
                    elif t == T_STR:
                        n = uint(1)
                        args.append((t, take(n).decode(errors="replace"), 0))
                for idx, (t, v, bits) in enumerate(args):
                    if t in (T_INT, T_LONG, T_LLONG):
                        args[idx] = (t, _signed(v, bits), bits)
                text = format_c(m.format, args)
            where = f"  ({m.file}:{m.line})" if self.where else ""
            lines.append(head + text + where)
        return lines


def decode_stream(db, data, out, where=False):
    """A framed console capture: log records decoded, the console's text and stray bytes as
    they are. Returns the stream's counters."""
    stream = Stream(db, where)
    dec = framing.Decoder(max_payload=4096)
    for kind, a, b in dec.feed_all(data) + dec.finish():
        if kind == "raw":
            text = a.decode(errors="replace")
            if text.strip():
                out.write(text if text.endswith("\n") else text + "\n")
            continue
        ch, payload = a, b
        if ch == CH_LOG:
            for line in stream.records(payload):
                out.write(line + "\n")
        elif ch == CH_SHELL:
            out.write(payload.decode(errors="replace"))
        else:
            out.write(f"(channel {ch}: {len(payload)} bytes)\n")
    return {"frames": dec.frames, "errors": dec.errors, "overruns": dec.overruns,
            "started": stream.started}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--elf", required=True, help="the image the stream comes from")
    ap.add_argument("input", nargs="?", default="-", help="a capture of the console, or - for stdin")
    ap.add_argument("--records", action="store_true", help="the input is bare records, not frames")
    ap.add_argument("--where", action="store_true", help="append each call site's file and line")
    ap.add_argument("--tick-ns", type=int, help="the tick's length, when the stream has no start record")
    args = ap.parse_args(argv)
    try:
        db = LogDb(args.elf)
        data = sys.stdin.buffer.read() if args.input == "-" else Path(args.input).read_bytes()
        if args.records:
            for line in Stream(db, args.where, args.tick_ns).records(data):
                print(line)
            return 0
        stats = decode_stream(db, data, sys.stdout, args.where)
    except LogError as e:
        print(f"logdec: {e}", file=sys.stderr)
        return 3 if "build" in str(e) else 2
    if stats["errors"] or stats["overruns"]:
        print(f"logdec: {stats['frames']} frames, {stats['errors']} damaged, {stats['overruns']} too long",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
