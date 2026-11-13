#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Soak the framed console: read tools/hil/log-soak's console for a while and judge it (M4.3).

    log_soak.py --elf build/log_soak-nucleo_f446re/log_soak.elf --port /dev/cu.usbmodem31103 \\
                --minutes 30 --archive docs/engineering/hil-runs/<date>-<board>-log-soak

Passes when no frame was damaged or too long, every log record decoded, the numbered records
came in order, and each one missing is covered by the dropped records that came after it (A127:
the count stands where the loss was). The trace's packets are counted and their gaps reported.
The run's console and a JSON record of its counters go to --archive.
"""
import argparse
import json
import os
import re
import select
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
for sub in ("transport", "logdec", "trace", "emb"):
    sys.path.insert(0, str(HERE.parent / sub))
sys.path.insert(0, str(HERE))
import emb  # noqa: E402
import logdec  # noqa: E402
import run_hil  # noqa: E402
import trace  # noqa: E402

# a line's time is there once the stream's start record was seen, which a late reader misses
SEQ = re.compile(r"(?:^|\] )I soak: seq (\d+)$")
DROPPED = re.compile(r"(?:^|\] )W emb_log: (\d+) records dropped$")
COUNTERS = re.compile(r"(?:^|\] )I soak: (log|trace) (.*)$")


class Judge:
    def __init__(self, db):
        self.stream = logdec.Stream(db)
        self.console = emb.Console()
        self.trace = trace.Trace()
        self.seq_next = None
        self.records = self.seq = self.dropped = self.uncovered = 0
        self.pending_drop = 0  # dropped records seen since the last numbered record
        self.decode_errors = 0
        self.trace_packets = 0
        self.counters = {}
        self.texts = []

    def feed(self, data):
        for item in self.console.feed(data):
            if item[0] == "text":
                self.texts.append(item[1].decode(errors="replace"))
                continue
            _, ch, payload = item
            if ch == 1:
                self.trace_packets += 1
                try:
                    self.trace.packet(payload)
                except trace.TraceError:
                    self.decode_errors += 1
                continue
            if ch != 0:
                continue
            try:
                lines = self.stream.records(payload)
            except logdec.LogError:
                self.decode_errors += 1
                continue
            for line in lines:
                self.line(line)

    def line(self, line):
        self.records += 1
        m = DROPPED.search(line)
        if m:
            self.dropped += int(m.group(1))
            self.pending_drop += int(m.group(1))
            return
        m = COUNTERS.search(line)
        if m:
            self.counters[m.group(1)] = m.group(2)
            return
        m = SEQ.search(line)
        if not m:
            return
        n = int(m.group(1))
        self.seq += 1
        if self.seq_next is not None:
            gap = n - self.seq_next
            if gap < 0:
                self.uncovered += 1  # out of order: no drop explains that
            elif gap > self.pending_drop:
                self.uncovered += gap - self.pending_drop
        self.seq_next = n + 1
        self.pending_drop = 0

    def record(self):
        c = self.console
        return {"frames": c.frames, "damaged_frames": c.errors, "log_records": self.records,
                "numbered_records": self.seq, "dropped_counted": self.dropped,
                "missing_not_counted": self.uncovered, "decode_errors": self.decode_errors,
                "trace_packets": self.trace_packets, "trace_events": len(self.trace.events),
                "trace_gaps": self.trace.gaps, "target_counters": self.counters}

    def passed(self):
        return (self.console.errors == 0 and self.decode_errors == 0 and self.uncovered == 0
                and self.seq > 0 and self.trace_packets > 0)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--elf", required=True)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--minutes", type=float, default=30.0)
    ap.add_argument("--archive", help="keep the console and the record here")
    ap.add_argument("--board", default="", help="named in the record")
    ap.add_argument("--input", help="judge a capture instead of the port")
    args = ap.parse_args(argv)
    judge = Judge(logdec.LogDb(args.elf))
    raw = bytearray()
    started = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    t0 = time.monotonic()
    if args.input:
        raw += Path(args.input).read_bytes()
        judge.feed(bytes(raw))
    else:
        fd = run_hil.open_console(args.port, args.baud)
        end = t0 + args.minutes * 60.0
        last_report = t0
        try:
            while time.monotonic() < end:
                r, _, _ = select.select([fd], [], [], 0.5)
                if r:
                    try:
                        chunk = os.read(fd, 4096)
                    except BlockingIOError:
                        continue
                    raw += chunk
                    judge.feed(chunk)
                if time.monotonic() - last_report > 60.0:
                    last_report = time.monotonic()
                    rec = judge.record()
                    print(f"log_soak: {(last_report - t0) / 60:.0f} min: {rec['frames']} frames, "
                          f"{rec['damaged_frames']} damaged, {rec['numbered_records']} numbered, "
                          f"{rec['dropped_counted']} dropped, {rec['missing_not_counted']} missing uncounted",
                          flush=True)
        finally:
            os.close(fd)
    rec = judge.record()
    rec.update({"board": args.board, "started": started, "minutes": round((time.monotonic() - t0) / 60.0, 2),
                "image": Path(args.elf).name, "outcome": "passed" if judge.passed() else "failed",
                "git": run_hil.git_state()})
    if args.archive:
        out = Path(args.archive)
        out.mkdir(parents=True, exist_ok=True)
        (out / "log_soak.bin").write_bytes(bytes(raw))
        (out / "log_soak.json").write_text(json.dumps(rec, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(rec, indent=1, sort_keys=True))
    return 0 if judge.passed() else 1


if __name__ == "__main__":
    sys.exit(main())
