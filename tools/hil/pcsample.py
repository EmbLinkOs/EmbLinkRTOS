#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A statistical profile of a Cortex-M3/M4/M7/M33 board as it runs: OpenOCD attaches without
halting the core and reads the DWT's PC sample register (DWT_PCSR) again and again; each read
is where the CPU was at that instant. The samples fold into functions with the image's symbol
table.

    pcsample.py IMAGE.elf [--samples 50000] [--period-cycles 1587] [--openocd-cfg board/st_nucleo_f4.cfg]
    pcsample.py IMAGE.elf --from-file samples.txt [--period-cycles ...]

Flash and start the image first: a profiling image runs one operation forever
(bench_compare.c with BENCH_PROFILE_NOTIFY). --period-cycles, the cycles of one period of that
loop (from the benchmark), turns each function's share into cycles per period. The image must
leave the DWT enabled (DEMCR.TRCENA), as the benchmarks do for CYCCNT.

The reads come at the ST-LINK's pace, about one every 0.6 ms, unrelated to the loop's period,
so the shares estimate where the cycles go; 50,000 samples put each share within about 0.5%.
"""
import argparse
import bisect
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

DWT_PCSR = 0xE000101C
TCL = """init
set f [open {out} w]
for {{set i 0}} {{$i < {n}}} {{incr i}} {{
    puts $f [read_memory 0x{addr:08X} 32 1]
}}
close $f
shutdown
"""


def symbols(elf, nm):
    """[(start, size, name)] of the image's functions, sorted by address."""
    out = subprocess.run([nm, "-n", "-S", "--defined-only", str(elf)], capture_output=True, text=True,
                         check=True).stdout
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in "tTwW":
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    return sorted(syms)


def fold(values, syms):
    """(Counter of function -> samples, samples with the core asleep or halted)."""
    starts = [s[0] for s in syms]
    counts = Counter()
    asleep = 0
    for v in values:
        if v == 0xFFFFFFFF:  # DWT_PCSR reads all ones while the core sleeps or is halted
            asleep += 1
            continue
        v &= ~1
        i = bisect.bisect_right(starts, v) - 1
        name = "?"
        if i >= 0 and v < syms[i][0] + max(syms[i][1], 2):
            name = syms[i][2]
        counts[name] += 1
    return counts, asleep


def sample(n, cfg, openocd):
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "pcsr.txt"
        script = Path(d) / "sample.tcl"
        script.write_text(TCL.format(out=out, n=n, addr=DWT_PCSR))
        subprocess.run([openocd, "-f", cfg, "-f", str(script)], capture_output=True, check=True)
        return [int(x.split()[0], 0) for x in out.read_text().split("\n") if x.strip()]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("--samples", type=int, default=50000)
    ap.add_argument("--period-cycles", type=float, help="cycles of one period of the profiled loop")
    ap.add_argument("--openocd-cfg", default="board/st_nucleo_f4.cfg")
    ap.add_argument("--openocd", default="openocd")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--from-file", help="fold samples saved earlier (one value per line) instead")
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args(argv)
    if a.from_file:
        values = [int(x.split()[0], 0) for x in Path(a.from_file).read_text().split("\n") if x.strip()]
    else:
        values = sample(a.samples, a.openocd_cfg, a.openocd)
    counts, asleep = fold(values, symbols(a.elf, a.nm))
    awake = len(values) - asleep
    if awake == 0:
        print("pcsample: every sample found the core asleep or halted", file=sys.stderr)
        return 1
    print(f"{len(values)} samples, {asleep} with the core asleep or halted")
    unit = f"cycles/period ({a.period_cycles:g})" if a.period_cycles else ""
    print(f"{'function':40s} {'share':>7s} {unit}")
    for name, k in counts.most_common(a.top):
        cyc = f"{a.period_cycles * k / awake:12.1f}" if a.period_cycles else ""
        print(f"{name:40s} {100 * k / awake:6.1f}% {cyc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
