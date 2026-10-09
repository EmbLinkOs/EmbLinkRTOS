#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Footprint report from a GNU ld map file (TEST-011, R-003 T1 and T2, SPEC-012 §13).

Reads the input-section lines of a map file, attributes each to the object file that
contributed it, and sums text, data and bss per component (kernel module, port, board,
application, toolchain runtime). Optionally checks the sums against a thresholds file:

    {"kernel_text_max": 4096, "kernel_ram_max": 64, "tcb_max": 32}

Exit status 1 when a threshold is exceeded (the build's footprint gate, 05 §5).
"""
import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict

SECTION_LINE = re.compile(r"^\s(\.[A-Za-z0-9_.$]+)(?:\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+))?\s*$")
CONT_LINE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$")


def classify(section: str) -> str:
    if section.startswith((".text", ".progmem", ".rodata", ".vectors", ".init", ".fini", ".ctors", ".dtors")):
        return "text"
    if section.startswith(".data"):
        return "data"
    if section.startswith((".bss", ".noinit")):
        return "bss"
    return "other"


def component(obj: str) -> str:
    m = re.search(r"emb_kernel\.dir/([a-z]+)/|libemb_kernel\.a\(([a-z_]+)\.c\.obj\)", obj)
    if m:
        return "kernel/" + (m.group(1) or m.group(2))
    if "/arch/" in obj or "emb_arch.dir" in obj or "libemb_arch.a" in obj:
        return "arch"
    if "/boards/" in obj or "emb_board.dir" in obj or "libemb_board.a" in obj:
        return "board"
    if "libemb_test.a" in obj or "/tests/" in obj:
        return "test harness"
    if "/samples/" in obj or obj.endswith("main.c.obj"):
        return "app"
    if "libgcc" in obj or "libc.a" in obj or "crt" in obj or obj.endswith((".a", ".o")):
        return "runtime"
    return "other:" + obj.rsplit("/", 1)[-1]


def parse_map(path: str):
    sums = defaultdict(lambda: {"text": 0, "data": 0, "bss": 0})
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    in_memory_map = False
    pending = None
    for line in lines:
        if line.startswith("Linker script and memory map"):
            in_memory_map = True
            continue
        if not in_memory_map:
            continue
        if line.startswith("OUTPUT(") or line.startswith("LOAD "):
            continue
        m = SECTION_LINE.match(line)
        if m:
            if m.group(2) is None:
                pending = m.group(1)  # name on its own line; numbers follow
                continue
            sec, size, obj = m.group(1), int(m.group(3), 16), m.group(4)
            pending = None
        else:
            m2 = CONT_LINE.match(line)
            if not (m2 and pending):
                continue
            sec, size, obj = pending, int(m2.group(2), 16), m2.group(3)
            pending = None
        kind = classify(sec)
        if kind == "other" or size == 0 or obj.startswith("0x"):
            continue
        # output-section summary lines have no object; input lines name a file
        if "(" not in obj and not obj.endswith((".obj", ".o", ".a")):
            continue
        sums[component(obj)][kind] += size
    return sums


def tcb_size(elf: str, nm: str):
    try:
        out = subprocess.run([nm, "--print-size", elf], check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[3] == "embk_idle_thread":
            return int(p[1], 16)
    return None


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("map", help="GNU ld map file (-Wl,-Map)")
    ap.add_argument("--elf", help="the image, for the control block size")
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--thresholds", help="JSON thresholds file")
    ap.add_argument("--json", help="write the report as JSON here")
    ap.add_argument("--title", default="")
    args = ap.parse_args(argv)
    sums = parse_map(args.map)
    kernel = {"text": 0, "data": 0, "bss": 0}
    rows = []
    for comp in sorted(sums):
        v = sums[comp]
        rows.append((comp, v["text"], v["data"], v["bss"]))
        if comp.startswith("kernel/"):
            for k in kernel:
                kernel[k] += v[k]
    tcb = tcb_size(args.elf, args.nm) if args.elf else None
    print(f"# Footprint {args.title}".rstrip())
    print()
    print("| component | text | data | bss |")
    print("|---|---:|---:|---:|")
    for comp, t, d, b in rows:
        print(f"| {comp} | {t} | {d} | {b} |")
    print(f"| **kernel total** | **{kernel['text']}** | **{kernel['data']}** | **{kernel['bss']}** |")
    print()
    print(f"Kernel static RAM (data + bss): {kernel['data'] + kernel['bss']} bytes")
    if tcb is not None:
        print(f"Thread control block (embk_idle_thread): {tcb} bytes")
    report = {"components": {c: {"text": t, "data": d, "bss": b} for c, t, d, b in rows},
              "kernel": kernel, "kernel_ram": kernel["data"] + kernel["bss"], "tcb": tcb}
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)
    rc = 0
    if args.thresholds:
        with open(args.thresholds, encoding="utf-8") as f:
            th = json.load(f)
        checks = [("kernel_text_max", kernel["text"]), ("kernel_ram_max", kernel["data"] + kernel["bss"]),
                  ("tcb_max", tcb)]
        for key, value in checks:
            if key in th and value is not None and value > th[key]:
                print(f"THRESHOLD EXCEEDED: {key} = {th[key]}, measured {value}")
                rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
