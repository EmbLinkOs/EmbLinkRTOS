#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Read an image's debug descriptor and list its threads and devices (SPEC-015 §5, OBS-008,
A107; SPEC-016 §9).

    descriptor.py IMAGE.elf [--dump ADDRESS:FILE ...] [--json]

The kernel exports `emb_debug_descriptor` (include/emb/debug.h): a header and a table of
addresses, field offsets and sizes. This module reads it from any memory, and from it the
threads, their states, priorities, stacks, owned mutexes and, for a switched-out thread,
the registers its port's switch frame holds. It knows nothing of the kernel's structures
beyond what the descriptor says.

Memory comes from a debugger (tools/descriptor/emb_gdb.py), or from an ELF and RAM dumps:
`--dump 0x20000000:ram.bin` lays a dump over the image, and whatever no dump covers is read
from the ELF's loaded sections (the constants, and the initial values of the data). Without
a dump the list is the image's state before it ran: useful only to check the descriptor.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = b"EMBD"
VERSION = 1
ABSENT = None  # a value of all ones

# The value indexes of include/emb/debug.h, in order (append-only; test_descriptor.py checks).
INDEXES = (
    "KERNEL_VERSION", "BUILD_ID", "PORT_NAME", "PORT_VARIANT", "PRIORITY_COUNT",
    "CPU", "CPU_CURRENT", "CPU_IRQ_NESTING", "CPU_SCHED_LOCK", "CPU_RESCHEDULE", "CPU_STATE",
    "THREAD_SIZE", "THREAD_LIST", "THREAD_LIST_NODE", "THREAD_TABLE", "THREAD_TABLE_COUNT", "IDLE_THREAD",
    "T_SP", "T_TYPE", "T_STATE", "T_FLAGS", "T_NAME", "T_BASE_PRIO", "T_EFF_PRIO", "T_WAIT_STATE",
    "T_WAIT_REASON", "T_TFLAGS", "T_INDEX", "T_WAIT_QUEUE", "T_DEADLINE", "T_OWNED", "T_STACK_BASE",
    "T_STACK_SIZE", "T_NOTIFY_BITS", "TICK_SIZE", "NOTIFY_SIZE",
    "M_NAME", "M_OWNER", "M_OWNED_NEXT", "M_COUNT", "MUTEX_LIST", "MUTEX_LIST_NODE",
    "TYPE_BASE", "TYPE_NAMES", "TYPE_COUNT",
    "STACK_FILL", "STACK_GUARD", "ISR_STACK", "ISR_STACK_SIZE",
    "FRAME_KIND", "FRAME_FLAGS", "FRAME_SIZE", "FRAME_TABLE", "FRAME_TABLE_COUNT",
    "DEVICE_TABLE", "DT_DEVICES", "DT_COUNT", "DT_EARLY", "D_NAME", "D_STATE", "D_CLASS",
    "DS_LIFECYCLE", "DS_LAST_STATUS",
)
FLAGS = {0x0001: "checked", 0x0002: "sched_table", 0x0004: "thread_list", 0x0008: "obj_names",
         0x0010: "obj_list", 0x0020: "stack_stats", 0x0040: "stack_check", 0x0080: "index_packed",
         0x0100: "idle_thread"}
TF_STARTED, TF_TERMINATED, TF_SUSPENDED, TF_KERNEL = 0x01, 0x02, 0x10, 0x40
KIND_NONE, KIND_CORTEX_M, KIND_TABLE = 0, 1, 2
FRAME_F_PSPLIM, FRAME_F_FPU = 0x1, 0x2
REG_EXC_RETURN = 0xFE
STATES = ("inactive", "ready", "running", "blocked", "terminated")  # emb_thread_state()
REASONS = ("none", "start", "suspend", "sleep", "object", "join", "notify")  # EMB_WAIT_REASON_*
LIFECYCLES = ("uninit", "initializing", "ready", "suspended", "failed")  # EMB_DEVICE_* (<emb/device.h>)
CLASSES = ("none", "uart", "spi", "i2c", "dma", "watchdog", "flash", "rtc", "entropy", "clock", "gpio")

REG_NAMES = {
    "cortex_m": {**{i: f"r{i}" for i in range(13)}, 13: "sp", 14: "lr", 15: "pc", 25: "xpsr"},
    "riscv": {1: "ra", 2: "sp", 5: "t0", 6: "t1", 7: "t2", 8: "s0", 9: "s1",
              **{10 + i: f"a{i}" for i in range(8)}, **{18 + i: f"s{2 + i}" for i in range(10)},
              **{28 + i: f"t{3 + i}" for i in range(4)}, 32: "pc"},
}


class DescriptorError(Exception):
    pass


# ---- memory ------------------------------------------------------------------------------

class Elf:
    """An ELF's symbols and loaded sections, read without external tools."""

    def __init__(self, path):
        self.data = Path(path).read_bytes()
        d = self.data
        if d[:4] != b"\x7fELF":
            raise DescriptorError(f"{path}: not an ELF file")
        self.cls, self.end = d[4], "<" if d[5] == 1 else ">"
        e = self.end
        self.machine = struct.unpack_from(e + "H", d, 18)[0]
        if self.cls == 1:
            shoff, = struct.unpack_from(e + "I", d, 0x20)
            shentsize, shnum = struct.unpack_from(e + "HH", d, 0x2E)
            fmt, self.symfmt, self.symsize = e + "IIIIIIIIII", e + "IIIBBH", 16
        else:
            shoff, = struct.unpack_from(e + "Q", d, 0x28)
            shentsize, shnum = struct.unpack_from(e + "HH", d, 0x3A)
            fmt, self.symfmt, self.symsize = e + "IIQQQQIIQQ", e + "IBBHQQ", 24
        # (type, flags, addr, offset, size, link)
        self.sections = [(s[1], s[2], s[3], s[4], s[5], s[6])
                         for s in (struct.unpack_from(fmt, d, shoff + i * shentsize) for i in range(shnum))]

    def symbols(self):
        d = self.data
        for typ, _, _, off, size, link in self.sections:
            if typ != 2:  # SHT_SYMTAB
                continue
            stroff = self.sections[link][3]
            for o in range(off, off + size, self.symsize):
                f = struct.unpack_from(self.symfmt, d, o)
                if self.cls == 1:
                    name, value, size_, _, _, shndx = f
                else:
                    name, _, _, shndx, value, size_ = f
                end = d.index(b"\0", stroff + name)
                yield d[stroff + name:end].decode(errors="replace"), value, size_, shndx

    def symbol(self, wanted):
        for name, value, size, shndx in self.symbols():
            if name == wanted and shndx != 0:
                return value, size
        return None

    def read(self, addr, n):
        """The section contents at @addr; None where no section with contents covers it."""
        for typ, flags, saddr, off, size, _ in self.sections:
            if flags & 0x2 and typ != 8 and saddr <= addr and addr + n <= saddr + size:  # ALLOC, not NOBITS
                return self.data[off + addr - saddr: off + addr - saddr + n]
        for typ, flags, saddr, off, size, _ in self.sections:
            if flags & 0x2 and typ == 8 and saddr <= addr and addr + n <= saddr + size:
                return bytes(n)  # .bss before it ran
        return None


class Memory:
    """RAM dumps over an ELF's sections."""

    def __init__(self, elf=None, dumps=()):
        self.elf = elf
        self.dumps = list(dumps)  # (address, bytes)

    def read(self, addr, n):
        for base, blob in self.dumps:
            if base <= addr and addr + n <= base + len(blob):
                return blob[addr - base: addr - base + n]
        got = self.elf.read(addr, n) if self.elf else None
        if got is None:
            raise DescriptorError(f"no memory at {addr:#x} ({n} bytes)")
        return got


# ---- the descriptor ----------------------------------------------------------------------

class Descriptor:
    def __init__(self, mem, address):
        self.mem, self.address = mem, address
        head = mem.read(address, 16)
        if head[:4] != MAGIC:
            raise DescriptorError(f"no descriptor at {address:#x}: magic {head[:4]!r}")
        self.end = ">" if head[9] == 1 else "<"
        self.version, self.count = struct.unpack_from(self.end + "HH", head, 4)
        self.psize = head[8]
        self.flags, = struct.unpack_from(self.end + "I", head, 12)
        if self.version != VERSION:
            raise DescriptorError(f"descriptor version {self.version}; this reader knows {VERSION}")
        if self.psize not in (2, 4, 8):
            raise DescriptorError(f"pointer size {self.psize}")
        raw = mem.read(address + 16, self.count * self.psize)
        absent = (1 << (8 * self.psize)) - 1
        self.v = {}
        for i, name in enumerate(INDEXES):
            if i < self.count:
                x = int.from_bytes(raw[i * self.psize:(i + 1) * self.psize], "big" if self.end == ">" else "little")
                self.v[name] = ABSENT if x == absent else x
            else:
                self.v[name] = ABSENT  # an older kernel: the value did not exist yet
        self.port = self.string(self.v["PORT_NAME"])
        self.variant = self.string(self.v["PORT_VARIANT"])

    # memory helpers
    def u(self, addr, n):
        return int.from_bytes(self.mem.read(addr, n), "big" if self.end == ">" else "little")

    def ptr(self, addr):
        return self.u(addr, self.psize)

    def string(self, addr, limit=64):
        if addr in (ABSENT, 0):
            return None
        out = bytearray()
        while len(out) < limit:
            c = self.mem.read(addr + len(out), 1)[0]
            if c == 0:
                break
            out.append(c)
        return out.decode(errors="replace")

    def field(self, base, name, n=1):
        off = self.v[name]
        return None if off is ABSENT else self.u(base + off, n)

    def header(self):
        ver = self.v["KERNEL_VERSION"]
        return {"address": self.address, "version": self.version, "count": self.count,
                "pointer_size": self.psize, "endian": "big" if self.end == ">" else "little",
                "flags": [n for b, n in FLAGS.items() if self.flags & b],
                "kernel": f"{ver >> 16}.{(ver >> 8) & 0xFF}.{ver & 0xFF}", "port": self.port,
                "variant": self.variant, "priorities": self.v["PRIORITY_COUNT"],
                "build_id": None if self.v["BUILD_ID"] is ABSENT else self.mem.read(self.v["BUILD_ID"], 20).hex()}

    # threads
    def thread_addresses(self):
        out = []
        if self.v["THREAD_LIST"] is not ABSENT:
            head = self.v["THREAD_LIST"]
            node = self.ptr(head)
            while node != head:
                if node == 0 or len(out) > 256:
                    raise DescriptorError("the thread list is broken")
                out.append(node - self.v["THREAD_LIST_NODE"])
                node = self.ptr(node)
        elif self.v["THREAD_TABLE"] is not ABSENT:
            for i in range(self.v["THREAD_TABLE_COUNT"]):
                t = self.ptr(self.v["THREAD_TABLE"] + i * self.psize)
                if t:
                    out.append(t)
        idle = self.v["IDLE_THREAD"]
        if self.flags & 0x0100 and idle not in (ABSENT, 0) and idle not in out:
            out.append(idle)  # a full idle thread; the slim idle context has only the prefix (ADR-050)
        return out

    def current(self):
        return self.ptr(self.v["CPU"] + self.v["CPU_CURRENT"])

    def type_name(self, tag):
        i = tag - self.v["TYPE_BASE"]
        if self.v["TYPE_NAMES"] is ABSENT or not 0 <= i < self.v["TYPE_COUNT"]:
            return f"type {tag:#x}"
        return self.string(self.ptr(self.v["TYPE_NAMES"] + i * self.psize))

    def index(self, t):
        if self.v["T_INDEX"] is ABSENT:
            packed = self.field(t, "T_FLAGS") >> 5
            return 0xFE if packed == 7 else packed
        return self.field(t, "T_INDEX")

    def state(self, t):
        """emb_thread_state()'s rule."""
        tf = self.field(t, "T_TFLAGS")
        if not tf & TF_STARTED:
            return "inactive", "none"
        if tf & TF_TERMINATED:
            return "terminated", "none"
        if t == self.current():
            return "running", "none"
        if tf & TF_SUSPENDED:
            return "blocked", "suspend"
        if self.field(t, "T_WAIT_STATE") == 2:
            r = self.field(t, "T_WAIT_REASON")
            return "blocked", REASONS[r] if r < len(REASONS) else str(r)
        return "ready", "none"

    def owned(self, t):
        out = []
        m = self.field(t, "T_OWNED", self.psize) if self.v["T_OWNED"] is not ABSENT else 0
        while m and len(out) < 64:
            name = None if self.v["M_NAME"] is ABSENT else self.string(self.ptr(m + self.v["M_NAME"]))
            out.append({"address": m, "name": name, "count": self.u(m + self.v["M_COUNT"], 1)})
            m = self.ptr(m + self.v["M_OWNED_NEXT"])
        return out

    def stack_high_water(self, base, size):
        """Bytes used at most, from the fill pattern; None when the stacks are not filled."""
        if not self.flags & 0x0020:
            return None
        fill = self.v["STACK_FILL"]
        blob = self.mem.read(base, size)
        n = 0
        while n < size and blob[n] == fill:
            n += 1
        return size - n

    def registers(self, t):
        """The registers a switched-out thread's frame holds, by name; None where not readable."""
        kind = self.v["FRAME_KIND"]
        if kind in (ABSENT, KIND_NONE) or self.v["FRAME_TABLE"] is ABSENT or t == self.current():
            return None
        sp = self.field(t, "T_SP", self.psize)
        names = REG_NAMES.get(self.port, {})
        regs, exc = {}, None
        for i in range(self.v["FRAME_TABLE_COUNT"]):
            num, word = self.mem.read(self.v["FRAME_TABLE"] + 2 * i, 2)
            value = self.u(sp + 4 * word, 4)
            if num == REG_EXC_RETURN:
                exc = value
            else:
                regs[names.get(num, f"reg{num}")] = value
        p = sp + self.v["FRAME_SIZE"]
        if kind == KIND_CORTEX_M:
            fp = bool(self.v["FRAME_FLAGS"] & FRAME_F_FPU) and exc is not None and not exc & 0x10
            if fp:
                for i in range(16):
                    regs[f"s{16 + i}"] = self.u(p + 4 * i, 4)
                p += 64
            for i, n in enumerate(("r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr")):
                regs[n] = self.u(p + 4 * i, 4)
            p += 32
            if fp:
                for i in range(16):
                    regs[f"s{i}"] = self.u(p + 4 * i, 4)
                regs["fpscr"] = self.u(p + 64, 4)
                p += 72
            if regs["xpsr"] & (1 << 9):
                p += 4  # the hardware aligned the stack
            regs["exc_return"] = exc
        regs["sp"] = p
        return regs

    def thread(self, t):
        base = self.field(t, "T_STACK_BASE", self.psize)
        size = self.field(t, "T_STACK_SIZE", self.psize)
        state, reason = self.state(t)
        name = None if self.v["T_NAME"] is ABSENT else self.string(self.field(t, "T_NAME", self.psize))
        info = {"address": t, "index": self.index(t), "name": name,
                "type": self.type_name(self.field(t, "T_TYPE")), "state": state, "reason": reason,
                "priority": self.field(t, "T_BASE_PRIO"), "effective_priority": self.field(t, "T_EFF_PRIO"),
                "kernel": bool(self.field(t, "T_TFLAGS") & TF_KERNEL),
                "stack_base": base, "stack_size": size, "sp": self.field(t, "T_SP", self.psize),
                "stack_high_water": None, "stack_in_use": None, "owned": self.owned(t), "registers": None}
        if base and size:
            try:
                info["stack_high_water"] = self.stack_high_water(base, size)
            except DescriptorError:
                pass
        if state in ("ready", "blocked"):
            try:
                info["registers"] = self.registers(t)
            except DescriptorError:
                pass
            if base and size and base <= info["sp"] <= base + size and self.v["FRAME_KIND"] != KIND_NONE:
                info["stack_in_use"] = base + size - info["sp"]  # at the switch, its frame included
        if self.v["T_NOTIFY_BITS"] is not ABSENT:
            info["notify_bits"] = self.field(t, "T_NOTIFY_BITS", self.v["NOTIFY_SIZE"])
        return info

    def threads(self):
        return [self.thread(t) for t in self.thread_addresses()]

    # devices (SPEC-016 §9)
    def devices(self):
        """The board's devices in initialization order, each with its lifecycle and the status
        that made it FAILED; empty for a kernel without devices or one older than them."""
        table = self.v["DEVICE_TABLE"]
        if table is ABSENT:
            return []
        array = self.ptr(table + self.v["DT_DEVICES"])
        count = self.u(table + self.v["DT_COUNT"], 2)
        early = self.u(table + self.v["DT_EARLY"], 2)
        out = []
        for i in range(count):
            dev = self.ptr(array + i * self.psize)
            state = self.ptr(dev + self.v["D_STATE"])
            life = self.u(state + self.v["DS_LIFECYCLE"], 1)
            status = self.u(state + self.v["DS_LAST_STATUS"], 2)
            cls = self.u(dev + self.v["D_CLASS"], 2)
            out.append({"address": dev, "name": self.string(self.ptr(dev + self.v["D_NAME"])),
                        "class": CLASSES[cls] if cls < len(CLASSES) else str(cls),
                        "level": "early" if i < early else "kernel",
                        "lifecycle": LIFECYCLES[life] if life < len(LIFECYCLES) else str(life),
                        "last_status": status - 0x10000 if status & 0x8000 else status})
        return out


def locate(elf, mem=None):
    """The descriptor's address: by its symbol, else by scanning the image for the magic."""
    sym = elf.symbol("emb_debug_descriptor")
    if sym:
        return sym[0]
    for typ, flags, addr, off, size, _ in elf.sections:
        if flags & 0x2 and typ != 8:
            i = elf.data.find(MAGIC, off, off + size)
            while i >= 0:
                if (i - off) % 4 == 0:
                    return addr + i - off
                i = elf.data.find(MAGIC, i + 1, off + size)
    raise DescriptorError("no emb_debug_descriptor in the image (CONFIG_EMB_DEBUG_DESCRIPTOR=n?)")


def table(threads):
    rows = [("idx", "name", "state", "prio", "stack now/max/size", "sp", "pc", "owns")]
    for t in threads:
        state = t["state"] + (f" ({t['reason']})" if t["reason"] != "none" else "")
        prio = str(t["priority"]) + (f"->{t['effective_priority']}" if t["effective_priority"] != t["priority"] else "")
        now = "-" if t["stack_in_use"] is None else str(t["stack_in_use"])
        used = "-" if t["stack_high_water"] is None else str(t["stack_high_water"])
        pc = t["registers"]["pc"] if t["registers"] else None
        rows.append((str(t["index"]), t["name"] or f"<{t['address']:#x}>", state, prio,
                     f"{now}/{used}/{t['stack_size']}", f"{t['sp']:#x}", "-" if pc is None else f"{pc:#x}",
                     ", ".join(m["name"] or f"{m['address']:#x}" for m in t["owned"]) or "-"))
    widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    return "\n".join("  ".join(c.ljust(w) for c, w in zip(r, widths)).rstrip() for r in rows)


def device_table(devices):
    rows = [("device", "class", "level", "state", "last status")]
    for x in devices:
        rows.append((x["name"] or f"<{x['address']:#x}>", x["class"], x["level"], x["lifecycle"], str(x["last_status"])))
    widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    return "\n".join("  ".join(c.ljust(w) for c, w in zip(r, widths)).rstrip() for r in rows)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("elf")
    ap.add_argument("--dump", action="append", default=[], metavar="ADDRESS:FILE",
                    help="a memory dump of the running image, laid over the ELF at ADDRESS")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    try:
        elf = Elf(args.elf)
        dumps = []
        for d in args.dump:
            addr, _, path = d.partition(":")
            dumps.append((int(addr, 0), Path(path).read_bytes()))
        d = Descriptor(Memory(elf, dumps), locate(elf))
        out = {"descriptor": d.header(), "threads": d.threads(), "devices": d.devices()}
    except (DescriptorError, OSError, ValueError) as e:
        print(f"descriptor: {e}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(out, indent=2))
    else:
        h = out["descriptor"]
        print(f"EmbLink {h['kernel']} on {h['port']} ({h['variant']}), descriptor v{h['version']} at "
              f"{h['address']:#x}, {', '.join(h['flags'])}")
        print(table(out["threads"]))
        if out["devices"]:
            print()
            print(device_table(out["devices"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
