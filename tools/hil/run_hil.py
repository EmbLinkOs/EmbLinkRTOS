#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Run a test image on a physical board (hardware in the loop) and read its console.

    run_hil.py IMAGE --hw-json BOARD/generated/hw.json --port /dev/ttyACM0
               [--flash-cmd "openocd -f board/st_nucleo_f4.cfg -c 'program {image} verify reset exit'"]
               [--until TEXT] [--timeout S] [--log FILE]

The board's generated hw.json (HW-005, HW-015) gives the console's baud rate and the probe
hint the default flash command is built from (OpenOCD with its debug_probe.openocd script).
The runner opens the console first, so nothing the image prints after the flash is lost,
then flashes and resets the board, and reads until the test framework's summary ("<n>
tests, <m> failed") and EMB_TEST_END, or --until's text for a benchmark, or the timeout.
Exit status as the emulator runners': 0 when the summary reports no failure (or --until's
text appeared), 1 on failures, 2 on a flash failure, a timeout or a usage error.

Unlike the emulators' (SIM-003), a run here is timing evidence: the runner prints a "hil:"
line with the board, the probe, the port and the image's SHA-256 for the record of 05 §4.4.
"""
import argparse
import hashlib
import json
import os
import re
import select
import shlex
import subprocess
import sys
import termios
import time
import tty
from pathlib import Path

SUMMARY = re.compile(rb"(\d+) tests, (\d+) failed")
END = b"EMB_TEST_END"
BAUD = {9600: termios.B9600, 19200: termios.B19200, 38400: termios.B38400, 57600: termios.B57600,
        115200: termios.B115200, 230400: getattr(termios, "B230400", termios.B115200)}


def open_console(port, baud):
    """The serial port in raw mode at the console's rate (POSIX termios: no pyserial)."""
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        speed = BAUD.get(baud)
        if speed is None:
            raise ValueError(f"baud rate {baud} is not supported here")
        attrs[4] = attrs[5] = speed
        attrs[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIFLUSH)  # nothing from before this run
    except termios.error:
        pass  # a pseudo-terminal in the tests may refuse some attributes
    return fd


def echo(chunk):
    out = getattr(sys.stdout, "buffer", None)
    if out is not None:
        out.write(chunk)
        out.flush()
    else:
        sys.stdout.write(chunk.decode("utf-8", "replace"))


def flash_command(hw, image, template):
    if template:
        return shlex.split(template.replace("{image}", shlex.quote(str(image))))
    probe = hw["board"].get("debug_probe", {})
    if "openocd" not in probe:
        raise ValueError("hw.json has no debug_probe.openocd hint; give --flash-cmd")
    return ["openocd", "-f", probe["openocd"], "-c", f"program {image} verify reset exit"]


def run(args):
    hw = json.loads(Path(args.hw_json).read_text(encoding="utf-8"))
    image = Path(args.image)
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    baud = hw["derived"]["console"]["baud"]
    try:
        cmd = flash_command(hw, image, args.flash_cmd)
    except ValueError as e:
        print(f"run_hil: {e}", file=sys.stderr)
        return 2
    print(f"hil: board {hw['board']['name']}, soc {hw['soc'].get('part', hw['soc']['name'])}, "
          f"probe {' '.join(cmd[:3])}, port {args.port} at {baud} baud, image {image.name} sha256 {digest}")
    try:
        fd = open_console(args.port, baud)
    except (OSError, ValueError) as e:
        print(f"run_hil: cannot open the console {args.port}: {e}", file=sys.stderr)
        return 2
    log = open(args.log, "wb") if args.log else None
    try:
        try:
            flash = subprocess.run(cmd, capture_output=True, timeout=args.flash_timeout)
        except (OSError, subprocess.TimeoutExpired) as e:
            print(f"run_hil: flashing failed: {e}", file=sys.stderr)
            return 2
        if flash.returncode != 0:
            sys.stderr.write(flash.stdout.decode(errors="replace") + flash.stderr.decode(errors="replace"))
            print(f"run_hil: the flash command exited {flash.returncode}", file=sys.stderr)
            return 2
        until = args.until.encode() if args.until else None
        deadline = time.monotonic() + args.timeout
        buf = b""
        while time.monotonic() < deadline:
            ready, _, _ = select.select([fd], [], [], 0.1)
            if not ready:
                continue
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            except OSError:
                break  # the device went away (unplugged, or the test's pseudo-terminal closed)
            if not chunk:
                continue
            echo(chunk)
            if log:
                log.write(chunk)
            buf += chunk
            if until is not None:
                if until in buf:
                    return 0
                continue
            m = SUMMARY.search(buf)
            if m and END in buf:
                return 0 if int(m.group(2)) == 0 else 1
        print("\nrun_hil: timeout: no summary from the board", file=sys.stderr)
        return 2
    finally:
        os.close(fd)
        if log:
            log.close()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="the ELF (or hex) the flash command programs")
    ap.add_argument("--hw-json", required=True, help="the board's generated hw.json")
    ap.add_argument("--port", required=True, help="the console's serial device, e.g. the ST-LINK's /dev/ttyACM0")
    ap.add_argument("--flash-cmd", help="the flash command, {image} replaced by the image path")
    ap.add_argument("--until", help="end successfully when this text appears (a benchmark's BENCH_END)")
    ap.add_argument("--timeout", type=float, default=120.0, help="seconds to wait for the result")
    ap.add_argument("--flash-timeout", type=float, default=60.0)
    ap.add_argument("--log", help="also write the console to this file")
    return run(ap.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
