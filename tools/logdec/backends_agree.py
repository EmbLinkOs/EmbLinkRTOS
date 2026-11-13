#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Check that examples/logging prints the same lines with both log backends (A127).

    backends_agree.py --plain plain.log --framed framed.bin --elf framed.elf [--burst 200]

The plain-text build's console against the framed build's, decoded with tools/logdec from its
ELF: after the timestamps, the lines must be the same in the same order. The burst is the
exception: each build keeps as many of its records as its ring holds (a deferred record is
smaller), so each must keep the first ones, in order, and count the rest as dropped.
"""
import argparse
import io
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import logdec  # noqa: E402

STAMP = re.compile(r"^\[\d+\.\d{3}\] ")
BURST = re.compile(r"^I app: burst (\d+)$")
DROPPED = re.compile(r"^W emb_log: (\d+) records dropped$")


def split(text, burst, name):
    """The lines without their timestamps, and the burst's records checked and set aside."""
    rest, kept, dropped = [], [], 0
    for raw in text.splitlines():
        line = STAMP.sub("", raw)
        m = BURST.match(line)
        if m:
            kept.append(int(m.group(1)))
            continue
        m = DROPPED.match(line)
        if m:
            dropped += int(m.group(1))
            continue
        rest.append(line)
    problems = []
    if kept != list(range(len(kept))):
        problems.append(f"{name}: the burst's records are not its first ones in order: {kept[:8]}...")
    if len(kept) + dropped != burst:
        problems.append(f"{name}: {len(kept)} burst records kept and {dropped} dropped, not {burst}")
    if not kept or not dropped:
        problems.append(f"{name}: the burst should both fill the ring and overflow it")
    return rest, problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--plain", required=True, help="the plain-text build's console")
    ap.add_argument("--framed", required=True, help="the framed build's console, as captured")
    ap.add_argument("--elf", required=True, help="the framed build's ELF")
    ap.add_argument("--burst", type=int, default=200)
    args = ap.parse_args(argv)
    plain = Path(args.plain).read_bytes().decode(errors="replace")
    out = io.StringIO()
    stats = logdec.decode_stream(logdec.LogDb(args.elf), Path(args.framed).read_bytes(), out)
    a, pa = split(plain, args.burst, "plain")
    b, pb = split(out.getvalue(), args.burst, "framed")
    problems = pa + pb
    if not stats["started"]:
        problems.append("framed: no start record")
    if stats["errors"] or stats["overruns"]:
        problems.append(f"framed: {stats['errors']} damaged frames, {stats['overruns']} too long")
    if a != b:
        problems.append("the lines differ:")
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else "(none)"
            y = b[i] if i < len(b) else "(none)"
            if x != y:
                problems.append(f"  line {i + 1}: plain {x!r}, framed {y!r}")
    if "logging: done" not in a:
        problems.append("the run did not end")
    for p in problems:
        print(p, file=sys.stderr)
    if not problems:
        print(f"backends agree: {len(a)} lines, and the burst checked in both")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
