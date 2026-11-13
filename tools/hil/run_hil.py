#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

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
- debug_probe.esp (an ESP32 board's ROM loader, base: the flash offset its ROM boots from):
  the port is shared too. EmbCC's embflash --esp writes the image's .bin beside the ELF;
  the runner then opens the console and resets the board into a normal boot itself (EN low
  through RTS with IO0 high through DTR, as the board's auto-reset circuit wires them), so
  the run starts again from its first line. $EMBFLASH names the embflash to use.
- debug_probe.picotool (an RP2350 board's boot ROM loader in BOOTSEL, family: the UF2 family):
  the console is another port (a USB-UART on the board's UART0), opened first. The runner
  waits for the board in BOOTSEL (picotool info), then picotool loads the image's .uf2,
  verifies it and boots it. A test image built for the board reboots into BOOTSEL when it
  ends, so the next image needs no button; after a hang, hold BOOTSEL and replug the board.

It reads until the test framework's summary ("<n> tests, <m> failed") and EMB_TEST_END, or
--until's text for a benchmark, or the timeout. A suite whose fault checks reset the board
(A89) prints across several boots; the runner reads through them, as it reads one.
Exit status as the emulator runners': 0 when the summary reports no failure (or --until's
text appeared), 1 on failures, 2 on a flash failure, a timeout or a usage error.

With --bench, --until's text is not enough: the console must also carry at least one
"BENCH v1 name=... key=value" line whose values are all unsigned integers, with a nonzero
operation count and nonzero measurements, or the run fails (exit 1). A benchmark that only
reaches its end marker proves nothing.

Unlike the emulators' (SIM-003), a run here is timing evidence: the runner prints a "hil:"
line with the board, the probe, the port and the image's SHA-256 for the record of 05 §4.4.
With --archive DIR it also keeps the record (A89): DIR/<image>.log holds that line and the
console, and DIR/<image>.json the board, the port, the flash command, the SHA-256 of the
image and of every file the command flashes, the git commit, the times, the outcome, the
summary, each case's outcome line and any BENCH results. tools/hil/archive.py files a run's
records under docs/engineering/hil-runs/.
"""
import argparse
import fcntl
import hashlib
import json
import os
import re
import select
import shlex
import shutil
import struct
import subprocess
import sys
import termios
import time
import tty
from pathlib import Path

SUMMARY = re.compile(rb"(\d+) tests, (\d+) failed(?:, (\d+) skipped)?")
END = b"EMB_TEST_END"
CASE = re.compile(rb"^CASE (\S+) outcome=(\w+) asserts=(\d+) skips=(\d+)", re.M)
EVIDENCE = re.compile(rb"^EVIDENCE v1 (.*)$", re.M)
BENCH = re.compile(rb"^BENCH v1 name=(\S+)(.*)$", re.M)
# Not anchored at a line start: a fault check resets the board while the case's TEST line is still
# leaving the UART, so the next boot's count often continues a cut line ("TEST name\xff  fault ...").
FAULT_CHECKS = re.compile(rb"fault checks that reset the board: (\d+)")
ISR_STACK = re.compile(rb"^ISR_STACK size=(\d+) used=(\d+)", re.M)
ROOT = Path(__file__).resolve().parents[2]
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
    probe = hw["board"].get("debug_probe", {})
    return "avrdude" in probe or "esp" in probe


def resets_after_open(hw):
    """Whether the runner resets the board once the console is open (an ESP32's auto-reset
    circuit: opening the port does not reset it, as DTR does an Uno)."""
    return "esp" in hw["board"].get("debug_probe", {})


TIOCM_DTR, TIOCM_RTS = 0x002, 0x004


def reset_esp(fd):
    """A normal boot: EN low (RTS) with IO0 high (DTR clear), then EN released. What arrived
    before, the start of the run the flasher's own reset began, is discarded while the board
    is held in reset, so the console starts at this boot's first byte."""
    try:
        fcntl.ioctl(fd, termios.TIOCMSET, struct.pack("I", TIOCM_RTS))
        time.sleep(0.15)
        termios.tcflush(fd, termios.TCIFLUSH)
        fcntl.ioctl(fd, termios.TIOCMSET, struct.pack("I", 0))
    except (OSError, termios.error):
        pass  # a pseudo-terminal in the tests has no modem lines


def uses_picotool(hw):
    """Whether the board is loaded by picotool in BOOTSEL (an RP2350 board)."""
    return "picotool" in hw["board"].get("debug_probe", {})


def wait_bootsel(timeout, poll=0.5):
    """None once a board in BOOTSEL answers picotool, else what to report: the previous image
    reboots into BOOTSEL as it ends, which takes the USB device a moment to appear."""
    deadline = time.monotonic() + timeout
    while True:
        try:
            found = subprocess.run(["picotool", "info"], capture_output=True, timeout=10).returncode == 0
        except (OSError, subprocess.TimeoutExpired) as e:
            return f"picotool: {e}"
        if found:
            return None
        if time.monotonic() >= deadline:
            return ("no board in BOOTSEL: hold the BOOTSEL button while plugging the board in "
                    "(an image that hung does not reboot into BOOTSEL itself)")
        time.sleep(poll)


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
    if "esp" in probe:
        tool = os.environ.get("EMBFLASH", "embflash")
        return [tool, str(Path(image).with_suffix(".bin")), "--esp", f"serial:{port}",
                "--base", hex(int(probe["esp"]["base"])), "--verify"]
    if "picotool" in probe:
        return ["picotool", "load", "-v", "-x", str(Path(image).with_suffix(".uf2")), "-t", "uf2"]
    if "openocd" not in probe:
        raise ValueError("hw.json has no debug_probe.openocd, .avrdude, .esp or .picotool hint; give --flash-cmd")
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


TEXT_FIELDS = ("os",)  # BENCH fields that name something rather than count it


def bench_results(buf):
    """The BENCH v1 lines: [{"name": ..., key: int, ...}]; ValueError on a malformed value."""
    out = []
    for m in BENCH.finditer(buf):
        r = {"name": m.group(1).decode(errors="replace")}
        for field in m.group(2).decode(errors="replace").split():
            key, sep, value = field.partition("=")
            if key in TEXT_FIELDS and sep and value:
                r[key] = value  # an identity, such as os=freertos
                continue
            if not sep or not value.isdigit():
                raise ValueError(f"BENCH {r['name']}: {field!r} is not key=unsigned integer")
            r[key] = int(value)
        out.append(r)
    return out


def bench_problem(results):
    """None when the results are numbers a board measured, else what is wrong."""
    if not results:
        return "no BENCH v1 result line"
    for r in results:
        ops = r.get("ops", 0)
        if ops == 0:
            return f"BENCH {r['name']}: no operation count"
        measured = {k: v for k, v in r.items() if k not in ("name", "ops", "unit_hz", "checked") + TEXT_FIELDS}
        if not measured or any(v == 0 for v in measured.values()):
            return f"BENCH {r['name']}: a zero measurement ({measured}): is the cycle counter running?"
    return None


def git_state():
    try:
        commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True,
                                text=True, timeout=10).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"],
                               capture_output=True, text=True, timeout=10).stdout.strip() != ""
    except (OSError, subprocess.TimeoutExpired):
        return None
    return {"commit": commit, "dirty": dirty} if commit else None


def parse_console(buf):
    """The facts of a run's console for its record."""
    rec = {}
    m = SUMMARY.search(buf)
    if m:
        rec["summary"] = {"tests": int(m.group(1)), "failed": int(m.group(2)),
                          "skipped": int(m.group(3)) if m.group(3) else None}
    rec["cases"] = [{"name": c.group(1).decode(errors="replace"), "outcome": c.group(2).decode(),
                     "asserts": int(c.group(3)), "skips": int(c.group(4))} for c in CASE.finditer(buf)]
    rec["fault_checks_by_reset"] = sum(int(n) for n in FAULT_CHECKS.findall(buf))
    isr = ISR_STACK.search(buf)
    if isr:  # the interrupt stack's high-water mark over the run (A90)
        rec["isr_stack"] = {"size": int(isr.group(1)), "used": int(isr.group(2))}
    ev = EVIDENCE.search(buf)
    if ev:
        rec["evidence"] = dict(f.split("=", 1) for f in ev.group(1).decode(errors="replace").split() if "=" in f)
    try:
        rec["bench"] = bench_results(buf)
    except ValueError as e:
        rec["bench_error"] = str(e)
    return rec


def write_archive(directory, rec, buf):
    d = Path(directory)
    d.mkdir(parents=True, exist_ok=True)
    stem = Path(rec["image"]).stem
    (d / f"{stem}.log").write_bytes(rec["header"].encode() + b"\n" + buf)
    rec = dict(rec, **parse_console(buf))
    del rec["header"]
    (d / f"{stem}.json").write_text(json.dumps(rec, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run(args):
    rec = {"format": "emb-hil-run v1", "started": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    buf = bytearray()
    rc = 2
    try:
        rc = run_board(args, rec, buf)
    finally:
        rec["ended"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        rec["exit"] = rc
        rec["outcome"] = {0: "passed", 1: "failed"}.get(rc, "error")
        if args.archive and "image" in rec:
            write_archive(args.archive, rec, bytes(buf))
    return rc


def run_board(args, rec, console):
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
    header = (f"hil: board {hw['board']['name']}, soc {hw['soc'].get('part', hw['soc']['name'])}, "
              f"probe {' '.join(cmd[:3])}, port {args.port} at {baud} baud, image {image.name} sha256 {digest}")
    print(header, flush=True)  # before the console's bytes, which go out unbuffered
    flashed = {}
    for word in cmd:  # every file the command programs, by its hash (the .hex beside the ELF)
        for part in re.split(r"[\s:,]", word):
            f = Path(part)
            if f.suffix in (".hex", ".bin", ".elf", ".uf2") and f.is_file():
                flashed[f.name] = hashlib.sha256(f.read_bytes()).hexdigest()
    rec.update({"header": header, "board": hw["board"]["name"], "soc": hw["soc"].get("part", hw["soc"]["name"]),
                "port": args.port, "baud": baud, "flash_command": cmd, "image": image.name,
                "image_sha256": digest, "flashed_sha256": flashed, "git": git_state()})
    if shared:
        # the bootloader needs the port: flash, then open the console (which resets the board)
        problem = flash(cmd, args.flash_timeout)
        if problem and resets_after_open(hw):
            # an ESP32's ROM loader now and then misses a block over USB serial: once more
            print(f"run_hil: {problem}; flashing again", file=sys.stderr)
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
    if resets_after_open(hw):
        reset_esp(fd)
    try:
        if not shared:
            problem = wait_bootsel(args.flash_timeout) if uses_picotool(hw) and not args.flash_cmd else None
            problem = problem or flash(cmd, args.flash_timeout)
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
            console += chunk
            if until is not None:
                if until in buf:
                    if args.bench:
                        try:
                            problem = bench_problem(bench_results(buf))
                        except ValueError as e:
                            problem = str(e)
                        if problem:
                            print(f"\nrun_hil: {problem}", file=sys.stderr)
                            return 1
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
    ap.add_argument("--archive", help="keep the run's record: DIR/<image>.log and DIR/<image>.json")
    ap.add_argument("--bench", action="store_true",
                    help="with --until: also require valid, nonzero BENCH v1 result lines")
    ap.add_argument("--flasher", choices=["auto", "embflash", "avrdude"], default="auto",
                    help="a serial bootloader's programmer: EmbCC's embflash when it has --stk500 (auto)")
    ap.add_argument("--shared-port", dest="shared_port", action="store_true", default=None,
                    help="flash before opening the console (default: from hw.json's probe)")
    return run(ap.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
