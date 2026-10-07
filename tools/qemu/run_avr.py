#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Run an ATmega328P test image under QEMU's arduino-uno machine (SIM-002).

The image prints its results on USART0; the run ends when the test framework's
summary line ("<n> tests, <m> failed") or the marker EMB_TEST_END appears, or at the
timeout. Exit status: 0 when the summary reports no failure, 1 on failures, 2 on a
timeout or a QEMU error. Emulated timing is never evidence (SIM-003): this runner
checks behavior only.
"""
import argparse
import re
import subprocess
import sys
import time

SUMMARY = re.compile(rb"(\d+) tests, (\d+) failed")
END = b"EMB_TEST_END"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("elf")
    ap.add_argument("--qemu", default="qemu-system-avr")
    ap.add_argument("--machine", default="arduino-uno")
    ap.add_argument("--timeout", type=float, default=240.0)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    cmd = [args.qemu, "-M", args.machine, "-bios", args.elf, "-nographic", "-monitor", "none"]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    except OSError as e:
        print(f"run_avr: cannot start {args.qemu}: {e}", file=sys.stderr)
        return 2
    deadline = time.monotonic() + args.timeout
    buf = b""
    rc = 2
    try:
        while time.monotonic() < deadline:
            chunk = proc.stdout.read1(4096) if hasattr(proc.stdout, "read1") else proc.stdout.read(1)
            if not chunk:
                if proc.poll() is not None:
                    break
                time.sleep(0.01)
                continue
            if not args.quiet:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            buf += chunk
            m = SUMMARY.search(buf)
            if m and END in buf:
                rc = 0 if int(m.group(2)) == 0 else 1
                break
        else:
            print("\nrun_avr: timeout", file=sys.stderr)
    finally:
        proc.kill()
        proc.wait()
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
