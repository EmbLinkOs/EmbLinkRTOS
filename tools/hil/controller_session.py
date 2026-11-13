#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Run the reference controller's scenario on a board and keep its record (M3a, docs/adoption.md).

    controller_session.py --port /dev/cu.usbmodem11203 --elf build/controller/controller.elf \\
                          --out docs/engineering/hil-runs/2026-11-07-nucleo_f446re-controller \\
                          [--soak 600] [--no-flash] [--tool-prefix arm-none-eabi-]

examples/controller prints one TLM line a second and answers commands on its console. This tool
keeps the port open through the whole run, so no boot or reset line is lost, and runs these
phases in order. Each phase passes or fails on what the TLM lines and replies say:

- boot: flash, then the boot banner, the reset's cause and "CTL ready". A crash record here is a
  previous run's, kept across the flash: noted, not failed.
- normal: the sampling rate holds, with no deadline miss, missed sample, dropped sample or ADC
  overrun; the temperature is plausible.
- stacks: the status command's per-thread and ISR stack use, each under its size.
- overload: "load 150" asks for more work than a period holds; the controller enters degraded
  mode, sheds every other sample, and its processed samples keep their deadline.
- recovery: "load 0"; it leaves degraded mode by itself.
- fast: "rate 1000" with "load 50", then "rate 100": the rate holds at 1 kHz without a miss.
- soak: --soak seconds at the default rate with load 30: nothing missed, dropped or reset.
- fault: "fault" traps in the command thread; the board reboots and reports a crash record,
  which tools/crashdec decodes against the ELF to a hardware fault in the command code.
- hang: "hang" stops the control thread; the watchdog resets the board, which says so.

--out receives controller.log (the console as received, with each command marked) and
controller.json (the record: board, port, image hash, git state, times, each phase's facts and
outcome, the decoded crash record). Exit status 0 when every phase passed, 1 otherwise, 2 on a
usage error.
"""
import argparse
import hashlib
import json
import os
import re
import select
import subprocess
import sys
import termios
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_hil  # noqa: E402  (open_console, git_state)

FORMAT = "emb-controller-session v1"
ROOT = Path(__file__).resolve().parents[2]
TLM = re.compile(r"^TLM (.*)\r?\n", re.M)  # complete lines only: the last may still be arriving
STACK = re.compile(r"^STACK (\S+) used=(\d+) size=(\d+)", re.M)
RESET = re.compile(r"^CTL reset(.*)$", re.M)
CRASH_LINE = re.compile(r"^EMB_CRASH [0-9a-f]+", re.M)
TEXT_KEYS = ("mode", "rx")


def parse_tlm(line):
    """One TLM line's fields: integers, with temp in thousandths and mode and rx as text."""
    out = {}
    for field in line.split():
        key, _, value = field.partition("=")
        if key in TEXT_KEYS:
            out[key] = value
        elif key == "temp":
            whole, _, frac = value.partition(".")
            if whole.lstrip("-").isdigit() and (frac.isdigit() or not frac):
                sign = -1 if whole.startswith("-") else 1
                out["temp_mc"] = sign * (int(whole.lstrip("-")) * 1000 + int(frac or "0") * 100)
        elif value.lstrip("-").isdigit():
            out[key] = int(value)
    return out


def tlm_lines(text):
    return [parse_tlm(m.group(1)) for m in TLM.finditer(text)]


def delta(lines, key):
    """How far a counter moved over the lines (0 for fewer than two)."""
    return lines[-1][key] - lines[0][key] if len(lines) >= 2 else 0


def rate_of(lines):
    """Processed plus shed samples per second over the lines."""
    if len(lines) < 2:
        return 0.0
    secs = lines[-1]["t"] - lines[0]["t"]
    return (delta(lines, "samples") + delta(lines, "shed")) / secs if secs else 0.0


def check_steady(lines, rate, tolerance=0.02):
    """Problems with a steady stretch at a rate: a list of strings, empty when it held."""
    problems = []
    if len(lines) < 2:
        return ["fewer than two TLM lines"]
    got = rate_of(lines)
    if abs(got - rate) > rate * tolerance + 1:
        problems.append(f"sampled at {got:.1f} Hz, not {rate} Hz")
    for key in ("deadline_miss", "missed", "queue_drops", "adc_overruns", "rx_drops", "shed"):
        if delta(lines, key):
            problems.append(f"{key} rose by {delta(lines, key)}")
    if any(line.get("mode") != "normal" for line in lines):
        problems.append("left normal mode")
    return problems


def check_degraded(lines):
    """Problems with an overload stretch: it must reach degraded mode and shed samples."""
    problems = []
    if not any(line.get("mode") == "degraded" for line in lines):
        problems.append("never entered degraded mode")
    if delta(lines, "degraded") < 1 and not (lines and lines[0].get("mode") == "degraded"):
        problems.append("the degraded-mode entry count did not rise")
    if delta(lines, "shed") == 0:
        problems.append("no sample shed")
    for key in ("missed", "queue_drops", "adc_overruns"):
        if delta(lines, key):
            problems.append(f"{key} rose by {delta(lines, key)}")
    return problems


class Console:
    """The board's console: everything received, and commands marked where they were sent."""

    def __init__(self, port, baud=115200):
        self.fd = run_hil.open_console(port, baud)
        attrs = termios.tcgetattr(self.fd)
        attrs[2] &= ~getattr(termios, "CRTSCTS", 0)  # the ST-LINK's port has no flow control
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.log = ""
        self.mark = 0

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if select.select([self.fd], [], [], 0.1)[0]:
                try:
                    chunk = os.read(self.fd, 4096).decode(errors="replace")
                except BlockingIOError:
                    continue
                self.log += chunk
                sys.stdout.write(chunk)
                sys.stdout.flush()

    def since(self):
        return self.log[self.mark:]

    def begin(self):
        self.mark = len(self.log)

    def wait(self, pattern, timeout):
        """The first match of pattern since begin(), waiting up to timeout seconds; else None."""
        rx = re.compile(pattern, re.M)
        end = time.time() + timeout
        while True:
            m = rx.search(self.log, self.mark)
            if m or time.time() >= end:
                return m
            self.pump(0.2)

    def wait_tlm(self, count, timeout):
        """count TLM lines since begin(), waiting up to timeout seconds."""
        end = time.time() + timeout
        while len(tlm_lines(self.since())) < count and time.time() < end:
            self.pump(0.2)
        return tlm_lines(self.since())

    def send(self, command):
        self.log += f"\n>>> {command}\n"
        sys.stdout.write(f"\n>>> {command}\n")
        os.write(self.fd, (command + "\r").encode())

    def command(self, command, timeout=3.0):
        """Send a command and return its reply line (OK or ERR), or None."""
        self.begin()
        self.send(command)
        m = self.wait(r"^(OK|ERR) .*$", timeout)
        return m.group(0) if m else None


def phase(name, problems, facts):
    print(f"\n=== {name}: {'pass' if not problems else 'FAIL: ' + '; '.join(problems)}", flush=True)
    return {"name": name, "outcome": "pass" if not problems else "fail", "problems": problems, "facts": facts}


def boot_facts(text):
    resets = RESET.findall(text)
    return {"reset": resets[-1].split() if resets else None,
            "crash": "none" if "CTL crash none" in text else ("record" if CRASH_LINE.search(text) else None)}


def decode_crash(log_text, elf, prefix, workdir):
    """crashdec's JSON for the EMB_CRASH lines of log_text, or an error string."""
    path = Path(workdir) / "crash-input.log"
    path.write_text(log_text, encoding="utf-8")
    cmd = [sys.executable, str(ROOT / "tools" / "crashdec" / "crashdec.py"), str(path), "--elf", str(elf),
           "--tool-prefix", prefix, "--json"]
    done = subprocess.run(cmd, capture_output=True, text=True)
    path.unlink()
    if done.returncode != 0:
        return f"crashdec exited {done.returncode}: {done.stderr.strip()}"
    try:
        return json.loads(done.stdout)  # one object per record
    except json.JSONDecodeError as e:
        return f"crashdec's output is not JSON: {e}"


def run(args, con, out_dir):
    phases = []

    # boot
    con.begin()
    if not args.no_flash:
        cmd = ["openocd", "-f", args.openocd_cfg, "-c", f"program {args.elf} verify reset exit"]
        flashed = subprocess.run(cmd, capture_output=True, text=True)
        if "Verified OK" not in flashed.stderr:
            sys.stderr.write(flashed.stderr[-800:])
            return [phase("boot", ["flashing failed"], {})]
    else:
        con.send("status")  # no reset: ask for something, so the console is known to answer
    ready = con.wait(r"^CTL ready", 8.0) if not args.no_flash else con.wait(r"^OK status", 4.0)
    facts = boot_facts(con.since())
    problems = [] if ready else ["no CTL ready line" if not args.no_flash else "no reply to status"]
    # A record at the first boot is a previous run's, kept across the flash: reported and cleared
    # by the controller, so noted here rather than failed.
    facts["previous_record"] = facts["crash"] == "record"
    phases.append(phase("boot", problems, facts))
    if problems:
        return phases

    # normal
    con.begin()
    lines = con.wait_tlm(args.normal + 1, args.normal + 4)
    temps = [line["temp_mc"] for line in lines if "temp_mc" in line]
    problems = check_steady(lines[1:], 100)
    if not temps or not all(10000 <= t <= 90000 for t in temps):
        problems.append(f"implausible chip temperature: {temps}")
    phases.append(phase("normal", problems, {"tlm": lines, "rate_hz": round(rate_of(lines[1:]), 2)}))

    # stacks
    reply = con.command("status")
    stacks = {m.group(1): {"used": int(m.group(2)), "size": int(m.group(3))} for m in STACK.finditer(con.since())}
    problems = [] if reply and reply.startswith("OK status") else ["no reply to status"]
    if set(stacks) != {"control", "command", "telemetry", "isr"}:
        problems.append(f"stack lines for {sorted(stacks)}")
    problems += [f"{k} used {v['used']} of {v['size']}" for k, v in stacks.items()
                 if not 0 < v["used"] < v["size"]]
    phases.append(phase("stacks", problems, {"stacks": stacks}))

    # overload
    reply = con.command("load 150")
    con.begin()
    lines = con.wait_tlm(6, 10)
    problems = [] if reply == "OK load 150" else [f"reply {reply!r}"]
    problems += check_degraded(lines)
    tail = [line for line in lines if line.get("mode") == "degraded"][1:]
    if len(tail) >= 2 and delta(tail, "deadline_miss"):
        problems.append(f"deadline misses continued in degraded mode: {delta(tail, 'deadline_miss')}")
    phases.append(phase("overload", problems, {"tlm": lines}))

    # recovery
    reply = con.command("load 0")
    con.begin()
    m = con.wait(r"^TLM \S+ mode=normal", 8)
    lines = con.wait_tlm(len(tlm_lines(con.since())) + 3, 5)
    normal = [line for line in lines if line.get("mode") == "normal"]
    problems = [] if reply == "OK load 0" else [f"reply {reply!r}"]
    if not m:
        problems.append("did not return to normal mode")
    else:
        problems += check_steady(normal[1:] if len(normal) > 2 else normal, 100)
    phases.append(phase("recovery", problems, {"tlm": lines, "seconds_to_normal": len(lines) - len(normal) + 1}))

    # fast
    replies = [con.command("rate 1000"), con.command("load 50")]
    con.begin()
    lines = con.wait_tlm(args.normal + 1, args.normal + 4)
    problems = [] if replies == ["OK rate 1000", "OK load 50"] else [f"replies {replies}"]
    problems += check_steady(lines[1:], 1000)
    facts = {"tlm": lines, "rate_hz": round(rate_of(lines[1:]), 2)}
    replies = [con.command("rate 100"), con.command("load 30")]
    if replies != ["OK rate 100", "OK load 30"]:
        problems.append(f"replies {replies}")
    phases.append(phase("fast", problems, facts))

    # soak
    con.begin()
    lines = con.wait_tlm(args.soak + 2, args.soak + 10)
    problems = check_steady(lines[2:], 100)  # the first line straddles the rate change
    if "CTL boot" in con.since():
        problems.append("the board rebooted")
    worst = max((line.get("proc_max_us", 0) for line in lines[2:]), default=0)
    lat = max((line.get("lat_max_us", 0) for line in lines[2:]), default=0)
    phases.append(phase("soak", problems, {"seconds": len(lines) - 2, "lat_max_us": lat, "proc_max_us": worst,
                                           "first": lines[2] if len(lines) > 2 else None,
                                           "last": lines[-1] if lines else None}))
    con.command("load 0")

    # fault
    con.begin()
    con.send("fault")
    ready = con.wait(r"^CTL ready", 8.0)
    text = con.since()
    facts = boot_facts(text)
    problems = [] if ready else ["the board did not come back"]
    if facts["crash"] != "record":
        problems.append("no crash record reported")
    decoded = decode_crash(text, args.elf, args.tool_prefix, out_dir) if facts["crash"] == "record" else None
    if isinstance(decoded, str):
        problems.append(decoded)
    elif decoded:
        rec = decoded[0]
        facts["decoded"] = rec
        if rec.get("fault_class_name") != "hardware":
            problems.append(f"fault class {rec.get('fault_class_name')}")
        if rec.get("build_match") is not True:
            problems.append("the record's build id does not match the ELF")
        fn = (rec.get("symbol") or {}).get("function", "")
        if fn not in ("execute", "command"):
            problems.append(f"pc symbolized to {fn!r}, not the command code")
    if facts["reset"] is not None and "software" not in facts["reset"]:
        problems.append(f"reset cause {facts['reset']}, not software")
    phases.append(phase("fault", problems, facts))

    # hang
    if ready:
        con.wait_tlm(1, 3)
    con.begin()
    reply = con.command("hang")
    t0 = time.time()
    ready = con.wait(r"^CTL ready", 8.0)
    text = con.since()
    facts = boot_facts(text)
    facts["seconds_to_reset"] = round(time.time() - t0, 1)
    problems = [] if reply and reply.startswith("OK hang") else [f"reply {reply!r}"]
    if not ready:
        problems.append("the board did not come back")
    if not facts["reset"] or "watchdog" not in facts["reset"]:
        problems.append(f"reset cause {facts['reset']}, not the watchdog")
    if facts["crash"] != "none":
        problems.append(f"crash report {facts['crash']} after a watchdog reset")
    phases.append(phase("hang", problems, facts))
    if ready:
        con.wait_tlm(2, 4)  # the controller runs again after the reset
    return phases


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", required=True)
    ap.add_argument("--elf", required=True, help="the controller image (flashed unless --no-flash)")
    ap.add_argument("--out", required=True, help="directory for controller.log and controller.json")
    ap.add_argument("--board", default="nucleo_f446re")
    ap.add_argument("--openocd-cfg", default="board/st_nucleo_f4.cfg")
    ap.add_argument("--tool-prefix", default="arm-none-eabi-")
    ap.add_argument("--normal", type=int, default=5, help="seconds of each steady phase (default 5)")
    ap.add_argument("--soak", type=int, default=60, help="seconds of the soak (default 60)")
    ap.add_argument("--no-flash", action="store_true", help="use the image already on the board")
    args = ap.parse_args(argv)
    if not Path(args.elf).is_file():
        print(f"controller_session: no {args.elf}", file=sys.stderr)
        return 2
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    started = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    con = Console(args.port)
    try:
        phases = run(args, con, out_dir)
    except Exception as e:  # keep what the board said, and fail the run, whatever went wrong here
        phases = [phase("session", [f"the tool failed: {type(e).__name__}: {e}"], {})]
        (out_dir / "controller.log").write_text(con.log, encoding="utf-8")
        raise
    finally:
        os.close(con.fd)
    passed = sum(p["outcome"] == "pass" for p in phases)
    rec = {"format": FORMAT, "board": args.board, "port": args.port, "image": Path(args.elf).name,
           "image_sha256": hashlib.sha256(Path(args.elf).read_bytes()).hexdigest(),
           "flashed": not args.no_flash, "git": run_hil.git_state(), "started": started,
           "ended": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "soak_seconds": args.soak,
           "outcome": "pass" if passed == len(phases) == 9 else "fail",
           "passed": passed, "total": 9, "phases": phases, "log": "controller.log"}
    (out_dir / "controller.log").write_text(con.log, encoding="utf-8")
    (out_dir / "controller.json").write_text(json.dumps(rec, indent=2) + "\n", encoding="utf-8")
    print(f"\ncontroller_session: {passed}/9 phases passed -> {out_dir}")
    return 0 if rec["outcome"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
