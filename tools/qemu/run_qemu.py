#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Run a test image under a QEMU machine and read its console (SIM-002).

The board supplies the machine arguments that precede the image path (EMB_BOARD_QEMU_ARGS
in board.cmake): for example "-M arduino-uno -bios", "-M mps2-an385 -semihosting-config
enable=on,target=native -kernel", "-M virt -bios none -kernel". Boards add
`-icount shift=N,sleep=off` so that virtual time follows executed instructions: the timing
tests then do not depend on host load. The image prints its
results on the board console; the run ends when the test framework's summary line
("<n> tests, <m> failed") and the marker EMB_TEST_END have appeared, when the emulator
exits, or at the timeout. Exit status: 0 when the summary reports no failure, 1 on
failures, 2 on a timeout, an early emulator exit, or a QEMU error. Emulated timing is
never evidence (SIM-003): this runner checks behavior only.
"""
import argparse
import re
import shlex
import subprocess
import sys
import time

SUMMARY = re.compile(rb"(\d+) tests, (\d+) failed")
END = b"EMB_TEST_END"


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("elf")
    ap.add_argument("--qemu", required=True, help="the qemu-system-* executable")
    ap.add_argument("--qemu-args", required=True,
                    help="machine arguments, ending with the option that takes the image (-bios or -kernel)")
    ap.add_argument("--timeout", type=float, default=240.0)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)
    cmd = [args.qemu] + shlex.split(args.qemu_args) + [args.elf, "-nographic", "-monitor", "none"]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    except OSError as e:
        print(f"run_qemu: cannot start {args.qemu}: {e}", file=sys.stderr)
        return 2
    deadline = time.monotonic() + args.timeout
    buf = b""
    rc = 2
    try:
        while time.monotonic() < deadline:
            chunk = proc.stdout.read1(4096) if hasattr(proc.stdout, "read1") else proc.stdout.read(1)
            if not chunk:
                if proc.poll() is not None:
                    m = SUMMARY.search(buf)
                    if m and END in buf:
                        rc = 0 if int(m.group(2)) == 0 else 1
                    else:
                        print(f"\nrun_qemu: the emulator exited ({proc.returncode}) before the summary",
                              file=sys.stderr)
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
            print("\nrun_qemu: timeout", file=sys.stderr)
    finally:
        proc.kill()
        proc.wait()
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
