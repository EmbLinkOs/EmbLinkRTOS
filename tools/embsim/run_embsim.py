#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Run a test image on an EmbSIM board and read its console (SIM-002).

The board names its EmbSIM target in board.cmake (EMB_BOARD_EMBSIM_TARGET, a board of
EmbSIM's catalog such as emblink/arduino_uno). EmbSIM runs the image on that board's
simulated MCU in virtual time, deterministically: the same image gives the same console,
to the byte, on every run and every host. The run ends at the millisecond of virtual time
the marker EMB_TEST_END appears on the console (`embsim run --until-text`), or at
--until. Exit status, as run_qemu.py's: 0 when the framework's summary line
("<n> tests, <m> failed") reports no failure, 1 on failures, 2 on a timeout, a missing
summary, or an EmbSIM error. Simulated timing is never evidence (SIM-003): this runner
checks behavior only. With --core-hz (a board's EMB_BOARD_EMBSIM_CORE_HZ) the image is run
a second time for EmbSIM's JSON report, in which the board's MCU must end at that core
clock: the board's clock setup took effect (status 1 if not).

With --net PORT,PORT (repeatable) the board runs inside a project whose nets join its
ports, as a bench's wires do: --net A0,A1 is the NUCLEO-F446RE's loopback of UART4. A
project run stops at --until, which then should be short, and its console is the board's
console UART in EmbSIM's JSON report.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

SUMMARY = re.compile(r"(\d+) tests, (\d+) failed")
END = "EMB_TEST_END"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("elf")
    ap.add_argument("--embsim", required=True, help="the embsim executable")
    ap.add_argument("--target", required=True, help="the EmbSIM board (emblink/arduino_uno)")
    ap.add_argument("--until", default="300s", help="the virtual time limit")
    ap.add_argument("--timeout", type=float, default=240.0, help="the wall-time limit, in seconds")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--core-hz", type=int, default=0, help="the core clock the MCU must end at")
    ap.add_argument("--net", action="append", default=[], metavar="PORT,PORT",
                    help="wire the board's ports together, in a project")
    args = ap.parse_args(argv)
    if args.net:
        return run_project(args)
    cmd = [args.embsim, "run", "--target", args.target, "--image", args.elf, "--policy", "functional",
           "--until", args.until, "--until-text", END]
    try:
        p = subprocess.run(cmd + ["--console"], capture_output=True, text=True, timeout=args.timeout,
                           stdin=subprocess.DEVNULL)
        r = subprocess.run(cmd + ["--json"], capture_output=True, text=True, timeout=args.timeout,
                           stdin=subprocess.DEVNULL) if args.core_hz else None
    except OSError as e:
        print(f"run_embsim: cannot start {args.embsim}: {e}", file=sys.stderr)
        return 2
    except subprocess.TimeoutExpired:
        print("run_embsim: timeout", file=sys.stderr)
        return 2
    out = p.stdout
    clocks = []
    if r is not None:
        try:
            report = json.loads(r.stdout)
        except ValueError:
            print(f"run_embsim: EmbSIM's report is not JSON (embsim exit {r.returncode})", file=sys.stderr)
            sys.stderr.write(r.stderr)
            return 2
        clocks = [o["core_hz"] for o in report.get("snapshot", {}).get("outputs", {}).values()
                  if isinstance(o, dict) and "core_hz" in o]
    if not args.quiet:
        sys.stdout.write(out)
    if p.returncode not in (0, 1) or p.stderr.strip():
        sys.stderr.write(p.stderr)
    m = SUMMARY.search(out)
    if not m or END not in out:
        print(f"\nrun_embsim: no summary by {args.until} of virtual time (embsim exit {p.returncode})",
              file=sys.stderr)
        return 2
    if args.core_hz and clocks != [args.core_hz]:
        print(f"\nrun_embsim: the MCU ends at a core clock of {clocks} Hz, not {args.core_hz}", file=sys.stderr)
        return 1
    return 0 if int(m.group(2)) == 0 else 1


def project(elf, target, nets):
    """An EmbSIM project of one board, B1, running the image, its ports joined by the nets."""
    return {
        "schema_version": 1, "kind": "project", "name": "run-embsim",
        "description": "run_embsim.py: one board and the wires the test needs",
        "policy": "functional",
        "boards": [{"id": "B1", "board": target, "layout": {"x": 0, "y": 0},
                    "firmware": {"image": str(Path(elf).resolve()), "format": "elf"}}],
        "components": [],
        "nets": {f"W{i}": [f"B1.{port}" for port in net.split(",")] for i, net in enumerate(nets)},
    }


def run_project(args):
    # beside the image, by a short relative name: EmbSIM refuses a long project path
    name = Path(args.elf).name + ".embsim.json"
    Path(name).write_text(json.dumps(project(args.elf, args.target, args.net), indent=1) + "\n",
                          encoding="utf-8")
    cmd = [args.embsim, "run", name, "--until", args.until, "--json"]
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=args.timeout, stdin=subprocess.DEVNULL)
    except OSError as e:
        print(f"run_embsim: cannot start {args.embsim}: {e}", file=sys.stderr)
        return 2
    except subprocess.TimeoutExpired:
        print("run_embsim: timeout", file=sys.stderr)
        return 2
    try:
        report = json.loads(p.stdout.decode("utf-8", "replace"))
        serial = report["serial"]["B1"]
        out = serial[serial["console"]]
    except (ValueError, KeyError, TypeError):
        print(f"run_embsim: EmbSIM's report has no console (embsim exit {p.returncode})", file=sys.stderr)
        sys.stderr.write(p.stderr.decode("utf-8", "replace"))
        return 2
    end = out.find(END)
    out = out[:end + len(END) + 1] if end >= 0 else out  # the first run: the image restarts after
    if not args.quiet:
        sys.stdout.write(out)
    m = SUMMARY.search(out)
    if not m or end < 0:
        print(f"\nrun_embsim: no summary by {args.until} of virtual time (embsim exit {p.returncode})",
              file=sys.stderr)
        return 2
    return 0 if int(m.group(2)) == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
