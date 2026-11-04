#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Run a test image on a physical board (hardware in the loop) and read its console.

    run_hil.py IMAGE --hw-json BOARD/generated/hw.json --port /dev/ttyACM0
               [--flash-cmd "openocd -f board/st_nucleo_f4.cfg -c 'program {image} verify reset exit'"]
               [--until TEXT] [--timeout S] [--log FILE]

The board's generated hw.json (HW-005, HW-015) gives the console's baud rate and the probe
hint the default flash command is built from:

- debug_probe.openocd (the NUCLEO's ST-LINK): the console is a separate port, so the runner
  opens it first, then flashes and resets, and nothing the image prints is lost;
- debug_probe.avrdude (the Arduino Uno's Optiboot): flashing and the console share the USB
  serial port, so the runner flashes the image's .hex first and then opens the console,
  which resets the board (DTR) and starts the image from its first line. The flasher is
  EmbCC's embflash (--stk500) when the one installed has it, else avrdude (--flasher).

It reads until the test framework's summary ("<n> tests, <m> failed") and EMB_TEST_END, or
--until's text for a benchmark, or the timeout.
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
import shutil
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


def shares_port(hw):
    """Whether flashing goes through the console's own port (a serial bootloader)."""
    return "avrdude" in hw["board"].get("debug_probe", {})


def embflash_has_stk500():
    """Whether the embflash on PATH programs serial bootloaders (EmbCC with --stk500)."""
    tool = shutil.which("embflash")
    if tool is None:
        return False
    try:
        usage = subprocess.run([tool, "--help"], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return "--stk500" in usage.stdout + usage.stderr


def flash_command(hw, image, template, port=None, flasher="auto"):
    if template:
        return shlex.split(template.replace("{image}", shlex.quote(str(image))).replace("{port}", str(port)))
    probe = hw["board"].get("debug_probe", {})
    if "avrdude" in probe:
        a = probe["avrdude"]
        hex_image = Path(image).with_suffix(".hex")
        if flasher == "embflash" or (flasher == "auto" and embflash_has_stk500()):
            return ["embflash", str(hex_image), "--stk500", f"serial:{port}@{a['baud']}", "--verify"]
        return ["avrdude", "-q", "-p", a["part"], "-c", a["programmer"], "-P", str(port), "-b", str(a["baud"]),
                "-D", "-U", f"flash:w:{hex_image}:i"]
    if "openocd" not in probe:
        raise ValueError("hw.json has no debug_probe.openocd or .avrdude hint; give --flash-cmd")
    return ["openocd", "-f", probe["openocd"], "-c", f"program {image} verify reset exit"]


def flash(cmd, timeout):
    """None when the flash command succeeded, else what to report."""
    try:
        done = subprocess.run(cmd, capture_output=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as e:
        return f"flashing failed: {e}"
    if done.returncode != 0:
        sys.stderr.write(done.stdout.decode(errors="replace") + done.stderr.decode(errors="replace"))
        return f"the flash command exited {done.returncode}"
    return None


def run(args):
    hw = json.loads(Path(args.hw_json).read_text(encoding="utf-8"))
    image = Path(args.image)
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    baud = hw["derived"]["console"]["baud"]
    shared = shares_port(hw) if args.shared_port is None else args.shared_port
    try:
        cmd = flash_command(hw, image, args.flash_cmd, args.port, args.flasher)
    except (KeyError, ValueError) as e:
        print(f"run_hil: {e}", file=sys.stderr)
        return 2
    print(f"hil: board {hw['board']['name']}, soc {hw['soc'].get('part', hw['soc']['name'])}, "
          f"probe {' '.join(cmd[:3])}, port {args.port} at {baud} baud, image {image.name} sha256 {digest}",
          flush=True)  # before the console's bytes, which go out unbuffered
    if shared:
        # the bootloader needs the port: flash, then open the console (which resets the board)
        problem = flash(cmd, args.flash_timeout)
        if problem:
            print(f"run_hil: {problem}", file=sys.stderr)
            return 2
    try:
        fd = open_console(args.port, baud)
    except (OSError, ValueError) as e:
        print(f"run_hil: cannot open the console {args.port}: {e}", file=sys.stderr)
        return 2
    log = open(args.log, "wb") if args.log else None
    try:
        if not shared:
            problem = flash(cmd, args.flash_timeout)
            if problem:
                print(f"run_hil: {problem}", file=sys.stderr)
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
    ap.add_argument("--flasher", choices=["auto", "embflash", "avrdude"], default="auto",
                    help="a serial bootloader's programmer: EmbCC's embflash when it has --stk500 (auto)")
    ap.add_argument("--shared-port", dest="shared_port", action="store_true", default=None,
                    help="flash before opening the console (default: from hw.json's probe)")
    return run(ap.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
