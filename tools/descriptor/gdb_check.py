#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Thread awareness under a real debugger: OBS-008's consumer test on an emulated port (A107).

    gdb_check.py --qemu qemu-system-arm --qemu-args "-M mps2-an386 -kernel" --gdb gdb-multiarch \\
                 build/cm4-an386/tests/conformance/test_debug_descriptor.elf

It starts test_debug_descriptor under QEMU halted at reset, attaches GDB through QEMU's
gdbstub, loads tools/descriptor/emb_gdb.py and stops at debug_descriptor_checkpoint(), where
the test has put its threads in known states. Through the descriptor alone it then lists the
threads and takes the backtrace of each switched-out one, which unwinds only when the frame
the port described is read right: the sleeper must show sleeper(), the suspended thread
parked(), the mutex waiter waiter(). It lists the board's devices too, each of which must
be READY by then (SPEC-016 §9). Exit status 0 when every check holds, 1 when one fails,
2 when the run itself could not be made.
"""
import argparse
import json
import os
import shlex
import socket
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import descriptor as dd  # noqa: E402

GDB_SCRIPT = r'''
import gdb, json, sys
sys.path.insert(0, "%(here)s")  # a script sourced from a script sees the outer one's __file__
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("set backtrace limit 64")  # GDB can repeat a Clang image's root frame forever
gdb.execute("target remote :%(port)d")
gdb.execute("source %(emb_gdb)s")
gdb.execute("break debug_descriptor_checkpoint")
gdb.execute("continue")
out = {"info": gdb.execute("emb info", to_string=True),
       "table": gdb.execute("emb threads", to_string=True)}
line = gdb.execute("emb threads --json", to_string=True)
out["threads"] = json.loads(line.split("EMB_THREADS ", 1)[1])
line = gdb.execute("emb devices --json", to_string=True)
out["devices"] = json.loads(line.split("EMB_DEVICES ", 1)[1])
out["bt"] = {}
for t in out["threads"]:
    try:
        out["bt"][str(t["index"])] = gdb.execute("emb bt %%d" %% t["index"], to_string=True)
    except gdb.error as e:
        out["bt"][str(t["index"])] = "ERROR " + str(e)
out["pc_after"] = int(gdb.parse_and_eval("$pc"))
gdb.execute("finish", to_string=True)  # the target still runs after the backtraces
out["finished"] = True
print("EMB_GDB_CHECK " + json.dumps(out))
gdb.execute("kill")
'''


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def check(result):
    problems = []
    # by name, or by the priority the test gave each where names are compiled out (tiny)
    by_prio = {4: "dd_sleeper", 3: "dd_parked", 5: "dd_waiter", 6: "dd_fpu"}
    named = any(t["name"] for t in result["threads"])
    threads = {(t["name"] if named else by_prio.get(t["priority"])): t for t in result["threads"]}
    bt = result["bt"]

    def expect(name, state, reason, frame):
        t = threads.get(name)
        if t is None:
            problems.append(f"{name}: not listed")
            return
        if (t["state"], t["reason"]) != (state, reason):
            problems.append(f"{name}: {t['state']} ({t['reason']}), expected {state} ({reason})")
        trace = bt.get(str(t["index"]), "")
        if frame not in trace:
            problems.append(f"{name}: backtrace without {frame}():\n{trace}")

    expect("dd_sleeper", "blocked", "sleep", "sleeper")
    expect("dd_parked", "blocked", "suspend", "parked")
    running = [t for t in result["threads"] if t["state"] == "running"]
    if len(running) != 1:
        problems.append(f"{len(running)} running threads")
    elif "debug_descriptor_checkpoint" not in bt.get(str(running[0]["index"]), ""):
        problems.append("the running thread's backtrace is not at the checkpoint")
    if "dd_fpu" in threads:  # a floating-point frame (Cortex-M with an FPU)
        expect("dd_fpu", "blocked", "sleep", "fpu_user")
        regs = threads["dd_fpu"]["registers"] or {}
        if "s16" not in regs:
            problems.append("dd_fpu: its frame was not read as a floating-point one")
    if "dd_waiter" in threads:
        expect("dd_waiter", "blocked", "object", "waiter")
        owner = running[0] if running else {"owned": [], "effective_priority": None}
        if [m["name"] for m in owner["owned"]] != ["dd_lock" if named else None]:
            problems.append(f"the running thread owns {owner['owned']}, expected dd_lock")
        if owner["effective_priority"] != 5:
            problems.append(f"the owner's effective priority is {owner['effective_priority']}, expected 5")
    # SPEC-016 §9: the board's devices, which the kernel initialized before the first thread ran
    for dev in result.get("devices", []):
        if dev["lifecycle"] != "ready":
            problems.append(f"device {dev['name']}: {dev['lifecycle']} (status {dev['last_status']}), expected ready")
    if not result.get("finished"):
        problems.append("the target did not run on after the backtraces")
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("elf")
    ap.add_argument("--qemu", required=True)
    ap.add_argument("--qemu-args", required=True, help="machine arguments ending with -kernel or -bios")
    ap.add_argument("--gdb", default="gdb-multiarch")
    ap.add_argument("--timeout", type=float, default=120.0)
    args = ap.parse_args(argv)
    port = free_port()
    qemu = subprocess.Popen([args.qemu] + shlex.split(args.qemu_args) +
                            [args.elf, "-nographic", "-monitor", "none", "-S", "-gdb", f"tcp:127.0.0.1:{port}"],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False) as f:
            f.write(GDB_SCRIPT % {"port": port, "emb_gdb": HERE / "emb_gdb.py", "here": HERE})
            script = f.name
        try:
            run = subprocess.run([args.gdb, "-nx", "-batch", "-ex", f"file {args.elf}", "-x", script],
                                 capture_output=True, text=True, timeout=args.timeout)
        finally:
            os.unlink(script)
    except (OSError, subprocess.TimeoutExpired) as e:
        print(f"gdb_check: {e}", file=sys.stderr)
        return 2
    finally:
        qemu.kill()
        qemu.wait()
    lines = [ln for ln in run.stdout.splitlines() if ln.startswith("EMB_GDB_CHECK ")]
    if not lines:
        print(run.stdout[-3000:] + run.stderr[-3000:], file=sys.stderr)
        print("gdb_check: GDB did not reach the checkpoint", file=sys.stderr)
        return 2
    result = json.loads(lines[-1].split(" ", 1)[1])
    print(result["info"].rstrip())
    print(result["table"].rstrip())
    for t in result["threads"]:
        if t["state"] == "blocked":
            print(f"--- emb bt {t['index']} ({t['name']})")
            print(result["bt"][str(t["index"])].rstrip())
    if result.get("devices"):
        print(dd.device_table(result["devices"]))
    problems = check(result)
    for p in problems:
        print(f"FAIL {p}")
    print(f"gdb_check: {'pass' if not problems else 'fail'}: {len(result['threads'])} threads")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
