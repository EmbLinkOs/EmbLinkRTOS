#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""A long soak of the reference controller, checkpointed, that can end at any time (M3a).

    caffeinate -ims python3 controller_soak.py --port /dev/cu.usbmodem11103 --out <record directory> \\
                                               [--hours 24] [--rate 100] [--load 30] [--checkpoint 600]

controller_session.py runs the whole scenario with a soak of minutes. This runs only the soak,
for hours: it sets the rate and the load, reads the stacks' high-water marks, then reads the
controller's TLM lines until --hours have passed or it is stopped (Ctrl-C, SIGTERM). It then
reads the high-water marks again and writes the record. Every --checkpoint seconds it rewrites
the record with what has been seen so far, so a host that dies leaves at most one interval
unrecorded.

The controller's counters are cumulative and its t field counts its own seconds, so the board's
behaviour is known even across a gap in the host's reading (a sleeping host, a USB hiccup):
- a gap appears as a jump in t;
- a reset as a boot line, or as t starting again.
The soak passes when the board never reset and t advanced by one per line, and when its misses,
drops and overruns did not rise, and its rate held. Any gap the host had is reported with its
length.

The console as received goes to soak.log, appended as it arrives; soak.json is the record.
Exit status 0 when the soak passed, 1 otherwise, 2 on a usage error.
"""
import argparse
import json
import os
import re
import select
import signal
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import controller_session as cs  # noqa: E402  (Console, parse_tlm)
import run_hil  # noqa: E402  (git_state)

FORMAT = "emb-controller-soak v1"
COUNTERS = ("deadline_miss", "missed", "queue_drops", "adc_overruns", "rx_drops")
STACK = re.compile(r"^STACK (\S+) used=(\d+) size=(\d+)")


class Soak:
    """What the TLM lines have shown, kept as they arrive."""

    def __init__(self, rate):
        self.rate = rate
        self.first = None
        self.last = None
        self.lines = 0
        self.resets = []
        self.board_gaps = []  # jumps in t: lines the host did not receive
        self.host_gaps = []   # longer than 3 s between two received lines
        self.lat_max = 0
        self.proc_max = 0
        self.temp = [None, None]
        self.last_host = None

    def boot(self, host_time, line):
        self.resets.append({"host_time": host_time, "line": line})

    def tlm(self, host_time, fields):
        if "t" not in fields or "samples" not in fields:
            return
        if self.last is not None:
            if fields["t"] < self.last["t"]:
                self.resets.append({"host_time": host_time, "line": f"t went from {self.last['t']} to {fields['t']}"})
            elif fields["t"] != self.last["t"] + 1:
                self.board_gaps.append({"from_t": self.last["t"], "to_t": fields["t"]})
        if self.last_host is not None and host_time - self.last_host > 3.0:
            self.host_gaps.append({"at": host_time, "seconds": round(host_time - self.last_host, 1)})
        self.last_host = host_time
        if self.first is None:
            self.first = fields
        self.last = fields
        self.lines += 1
        self.lat_max = max(self.lat_max, fields.get("lat_max_us", 0))
        self.proc_max = max(self.proc_max, fields.get("proc_max_us", 0))
        t = fields.get("temp_mc")
        if t is not None:
            self.temp = [t if self.temp[0] is None else min(self.temp[0], t),
                         t if self.temp[1] is None else max(self.temp[1], t)]

    def summary(self):
        if self.first is None or self.last is None:
            return {"lines": 0, "problems": ["no TLM line"]}
        seconds = self.last["t"] - self.first["t"]
        processed = self.last["samples"] - self.first["samples"]
        shed = self.last.get("shed", 0) - self.first.get("shed", 0)
        rise = {k: self.last.get(k, 0) - self.first.get(k, 0) for k in COUNTERS}
        problems = [f"{k} rose by {v}" for k, v in rise.items() if v]
        if self.resets:
            problems.append(f"{len(self.resets)} reset(s)")
        rate = (processed + shed) / seconds if seconds else 0.0
        if seconds and abs(rate - self.rate) > self.rate * 0.02 + 1:
            problems.append(f"sampled at {rate:.2f} Hz, not {self.rate}")
        if self.last.get("mode") != "normal":
            problems.append(f"ended in mode {self.last.get('mode')}")
        return {"lines": self.lines, "board_seconds": seconds, "samples_processed": processed, "shed": shed,
                "rate_hz": round(rate, 3), "counter_rises": rise, "lat_max_us": self.lat_max,
                "proc_max_us": self.proc_max, "temp_mc_range": self.temp, "resets": self.resets,
                "board_gaps": self.board_gaps, "host_gaps": self.host_gaps,
                "first": self.first, "last": self.last, "problems": problems}


def stacks(con, log):
    reply = con.command("status", timeout=4.0)
    out = {}
    for line in con.since().splitlines():
        m = STACK.match(line.strip())
        if m:
            out[m.group(1)] = {"used": int(m.group(2)), "size": int(m.group(3))}
    log.write(con.since())
    return out, reply


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--hours", type=float, default=24.0)
    ap.add_argument("--rate", type=int, default=100)
    ap.add_argument("--load", type=int, default=30)
    ap.add_argument("--checkpoint", type=float, default=600.0, help="seconds between record rewrites")
    ap.add_argument("--image", help="the ELF on the board, for the record's hash")
    args = ap.parse_args(argv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    stop = {"why": None}

    def on_signal(signum, _frame):
        stop["why"] = signal.Signals(signum).name

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)
    con = cs.Console(args.port)
    soak = Soak(args.rate)
    rec = {"format": FORMAT, "board": "nucleo_f446re", "port": args.port, "git": run_hil.git_state(),
           "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "planned_hours": args.hours,
           "rate_hz": args.rate, "load_pct": args.load, "checkpoint_s": args.checkpoint}
    if args.image:
        import hashlib
        rec["image"] = Path(args.image).name
        rec["image_sha256"] = hashlib.sha256(Path(args.image).read_bytes()).hexdigest()

    def write(final):
        r = dict(rec, soak=soak.summary(), final=final, updated=time.strftime("%Y-%m-%dT%H:%M:%S%z"))
        problems = r["soak"].get("problems", [])
        r["outcome"] = "pass" if final and not problems else ("fail" if final else "running")
        tmp = out / "soak.json.tmp"
        tmp.write_text(json.dumps(r, indent=2) + "\n", encoding="utf-8")
        tmp.replace(out / "soak.json")
        return r

    with open(out / "soak.log", "a", encoding="utf-8") as log:
        con.pump(1.5)
        replies = [con.command(f"rate {args.rate}"), con.command(f"load {args.load}")]
        log.write(con.log)
        con.pump(3.0)
        rec["replies"] = replies
        rec["stacks_start"], _ = stacks(con, log)
        start = time.time()
        next_checkpoint = start + args.checkpoint
        partial = ""
        con.log = ""
        while stop["why"] is None and time.time() - start < args.hours * 3600:
            if select.select([con.fd], [], [], 0.5)[0]:
                try:
                    chunk = os.read(con.fd, 4096).decode(errors="replace")
                except (BlockingIOError, InterruptedError):
                    chunk = ""
                except OSError as e:  # the port went away: a USB reset
                    log.write(f"\n>>> the port failed: {e}\n")
                    time.sleep(2.0)
                    try:
                        os.close(con.fd)
                        con.fd = cs.run_hil.open_console(args.port, 115200)
                    except OSError:
                        pass
                    continue
                log.write(chunk)
                partial += chunk
                *complete, partial = partial.split("\n")
                now = time.time()
                for line in complete:
                    line = line.strip("\r")
                    if line.startswith("CTL boot"):
                        soak.boot(now, line)
                    elif line.startswith("TLM "):
                        soak.tlm(now, cs.parse_tlm(line[4:]))
            if time.time() >= next_checkpoint:
                log.flush()
                r = write(False)
                s = r["soak"]
                print(f"{time.strftime('%H:%M:%S')} soak: {s.get('board_seconds', 0)} s, "
                      f"{s.get('samples_processed', 0)} samples, problems {s.get('problems')}", flush=True)
                next_checkpoint += args.checkpoint
        rec["ended_because"] = stop["why"] or "the planned duration"
        con.log = ""
        rec["stacks_end"], _ = stacks(con, log)
    os.close(con.fd)
    r = write(True)
    s = r["soak"]
    print(f"controller_soak: {r['outcome']}: {s.get('board_seconds', 0)} board seconds, "
          f"{s.get('samples_processed', 0)} samples, problems {s.get('problems')} -> {out}")
    return 0 if r["outcome"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
