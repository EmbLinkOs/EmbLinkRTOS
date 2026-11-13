# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""EmbLink thread and device awareness for GDB, from the image's debug descriptor (A107).

    (gdb) source tools/descriptor/emb_gdb.py
    (gdb) emb info                  the descriptor's header
    (gdb) emb threads [--json]      every thread: state, priorities, stack, pc, owned mutexes
    (gdb) emb bt INDEX [FRAMES]     the backtrace of the thread with that index (64 frames at most)
    (gdb) emb devices [--json]      the board's devices: class, level, lifecycle, last status

GDB itself sees one context: whatever the core runs. `emb bt` shows a switched-out
thread's stack by loading the registers its switch frame saved, running `bt`, and putting
the core's registers back, so the target must be halted (any GDB server: OpenOCD, pyOCD,
QEMU's gdbstub). The descriptor is read through GDB, so the kernel's layout comes from the
image itself and no structure is hard-coded here (tools/descriptor/descriptor.py).
"""
import json
import os
import sys

import gdb  # noqa: I001  (provided by GDB)

sys.path.insert(0, os.path.dirname(os.path.abspath(os.path.expanduser(__file__))))
import descriptor as dd  # noqa: E402


class GdbMemory:
    def read(self, addr, n):
        try:
            return bytes(gdb.selected_inferior().read_memory(addr, n))
        except gdb.MemoryError as e:
            raise dd.DescriptorError(str(e)) from e


def load():
    try:
        addr = int(gdb.parse_and_eval("(unsigned long long)&emb_debug_descriptor"))
    except gdb.error as e:
        raise gdb.GdbError(f"no emb_debug_descriptor symbol: {e} (CONFIG_EMB_DEBUG_DESCRIPTOR=n?)")
    try:
        return dd.Descriptor(GdbMemory(), addr)
    except dd.DescriptorError as e:
        raise gdb.GdbError(str(e))


class Emb(gdb.Command):
    """EmbLink kernel awareness: emb info, emb threads [--json], emb bt INDEX, emb devices [--json]."""

    def __init__(self):
        super().__init__("emb", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True)


class EmbInfo(gdb.Command):
    """The debug descriptor's header."""

    def __init__(self):
        super().__init__("emb info", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        h = load().header()
        for k, v in h.items():
            gdb.write(f"{k:13} {hex(v) if isinstance(v, int) and k == 'address' else v}\n")


class EmbThreads(gdb.Command):
    """Every thread the kernel knows: emb threads [--json]."""

    def __init__(self):
        super().__init__("emb threads", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        d = load()
        try:
            threads = d.threads()
        except dd.DescriptorError as e:
            raise gdb.GdbError(str(e))
        if arg.strip() == "--json":
            gdb.write("EMB_THREADS " + json.dumps(threads) + "\n")
        else:
            gdb.write(dd.table(threads) + "\n")


class EmbDevices(gdb.Command):
    """The board's devices in initialization order (SPEC-016 §9): emb devices [--json]."""

    def __init__(self):
        super().__init__("emb devices", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        devices = load().devices()
        if arg.strip() == "--json":
            gdb.write("EMB_DEVICES " + json.dumps(devices) + "\n")
        elif not devices:
            gdb.write("no devices (CONFIG_EMB_DEVICE=n, or none on this board)\n")
        else:
            gdb.write(dd.device_table(devices) + "\n")


# GDB's register names for what the frames hold
SWITCH_REGS = {
    "cortex_m": ("r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12",
                 "sp", "lr", "pc", "xpsr"),
    "riscv": ("ra", "sp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
              "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6", "pc"),
}


class EmbBt(gdb.Command):
    """The backtrace of a thread: emb bt INDEX [FRAMES] (the index `emb threads` lists).

    At most FRAMES frames, 64 by default: GDB's own unwinding can repeat a frame forever
    at a thread's root (seen with Clang's Cortex-M images)."""

    def __init__(self):
        super().__init__("emb bt", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        d = load()
        try:
            words = arg.split()
            want = int(words[0], 0)
            frames = int(words[1], 0) if len(words) > 1 else 64
        except (ValueError, IndexError):
            raise gdb.GdbError("usage: emb bt INDEX [FRAMES]")
        found = [t for t in d.thread_addresses() if d.index(t) == want]
        if not found:
            raise gdb.GdbError(f"no thread with index {want}")
        t = found[0]
        if t == d.current():
            gdb.execute(f"bt {frames}")
            return
        regs = d.registers(t)
        if regs is None:
            raise gdb.GdbError(f"thread {want}'s registers are not readable on this port ({d.port})")
        names = SWITCH_REGS.get(d.port)
        if names is None:
            raise gdb.GdbError(f"no register map for port {d.port}")
        frame = gdb.selected_frame()
        # the server's own name for each register: OpenOCD says xPSR where QEMU says xpsr
        regmap = {}
        for n in names:
            for alias in (n, n.upper() if n != "xpsr" else "xPSR"):
                try:
                    regmap[n] = (alias, int(frame.read_register(alias)) & 0xFFFFFFFF)
                    break
                except (ValueError, gdb.error):
                    continue
        want = dict(regs)
        if d.port == "cortex_m" and "sp" in regs:
            # Threads run on the process stack. A server that shows msp and psp apart from sp
            # (OpenOCD) needs psp set too, or GDB's unwinder can't tell which stack sp is.
            try:
                regmap["psp"] = ("psp", int(frame.read_register("psp")) & 0xFFFFFFFF)
                want["psp"] = regs["sp"]
            except (ValueError, gdb.error):
                pass
        try:
            for n, (alias, _) in regmap.items():
                if n in want:
                    gdb.execute(f"set ${alias} = {want[n]:#x}", to_string=True)
            gdb.execute("maintenance flush register-cache", to_string=True)
            gdb.execute(f"bt {frames}")
        finally:
            for n, (alias, value) in regmap.items():
                gdb.execute(f"set ${alias} = {value:#x}", to_string=True)
            gdb.execute("maintenance flush register-cache", to_string=True)


Emb()
EmbInfo()
EmbThreads()
EmbBt()
EmbDevices()
