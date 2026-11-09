# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The descriptor reader on synthetic memory, and its index list against include/emb/debug.h."""
import re
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import descriptor as dd  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


class Mem:
    def __init__(self):
        self.blobs = {}

    def put(self, addr, data):
        self.blobs[addr] = bytes(data)

    def read(self, addr, n):
        for base, blob in self.blobs.items():
            if base <= addr and addr + n <= base + len(blob):
                return blob[addr - base:addr - base + n]
        raise dd.DescriptorError(f"no memory at {addr:#x}")


# a made-up layout, as a kernel could have it
OFF = {"T_SP": 0, "T_TYPE": 4, "T_STATE": 5, "T_FLAGS": 6, "T_NAME": 8, "T_BASE_PRIO": 12, "T_EFF_PRIO": 13,
       "T_WAIT_STATE": 14, "T_WAIT_REASON": 15, "T_TFLAGS": 16, "T_INDEX": 17, "T_WAIT_QUEUE": 20,
       "T_DEADLINE": 24, "T_OWNED": 32, "T_STACK_BASE": 36, "T_STACK_SIZE": 40, "THREAD_LIST_NODE": 44}
DESC, CPU, LIST, NAMES, TYPES, FRAME = 0x1000, 0x2000, 0x2100, 0x3000, 0x3100, 0x3200
T1, T2, MUTEX, STACK = 0x4000, 0x4100, 0x4200, 0x8000


def build(fp=False, flags=0x0004 | 0x0008):
    m = Mem()
    v = {n: 0xFFFFFFFF for n in dd.INDEXES}
    v.update({k: o for k, o in OFF.items() if o is not None})
    v.update({"KERNEL_VERSION": 0x000200, "PORT_NAME": NAMES, "PORT_VARIANT": NAMES + 16, "PRIORITY_COUNT": 32,
              "CPU": CPU, "CPU_CURRENT": 0, "CPU_STATE": 7, "THREAD_LIST": LIST, "TYPE_BASE": 0x50,
              "TYPE_NAMES": TYPES, "TYPE_COUNT": 2, "M_NAME": 4, "M_OWNER": 8, "M_OWNED_NEXT": 12, "M_COUNT": 16,
              "STACK_FILL": 0xEB, "FRAME_KIND": dd.KIND_CORTEX_M, "FRAME_FLAGS": dd.FRAME_F_FPU,
              "FRAME_SIZE": 36, "FRAME_TABLE": FRAME, "FRAME_TABLE_COUNT": 9})
    m.put(DESC, b"EMBD" + struct.pack("<HHBBHI", 1, len(dd.INDEXES), 4, 0, 0, flags) +
          b"".join(struct.pack("<I", v[n]) for n in dd.INDEXES))
    m.put(NAMES, b"cortex_m\0\0\0\0\0\0\0\0armv7em\0")
    m.put(TYPES, struct.pack("<II", NAMES + 32, NAMES + 40))
    m.put(NAMES + 32, b"idle\0\0\0\0thread\0\0waiter\0\0runner\0\0lock\0")
    m.put(CPU, struct.pack("<I", T1) + bytes([0, 0, 0, 2]))
    m.put(LIST, struct.pack("<II", T1 + 44, T2 + 44))
    m.put(FRAME, bytes([4, 0, 5, 1, 6, 2, 7, 3, 8, 4, 9, 5, 10, 6, 11, 7, 0xFE, 8]))

    def tcb(name, prio, eff, wait_state, reason, tflags, index, owned, sp, nxt):
        b = bytearray(48)
        struct.pack_into("<I", b, 0, sp)
        b[4], b[5] = 0x51, 1
        struct.pack_into("<I", b, 8, name)
        b[12], b[13], b[14], b[15], b[16], b[17] = prio, eff, wait_state, reason, tflags, index
        struct.pack_into("<III", b, 32, owned, STACK, 1024)
        struct.pack_into("<I", b, 44, nxt)
        return bytes(b)
    sp = STACK + 1024 - 36 - (64 + 32 + 72 if fp else 32)
    m.put(T1, tcb(NAMES + 56, 1, 5, 0, 0, 0x01, 0, MUTEX, 0, T2 + 44))
    m.put(T2, tcb(NAMES + 48, 5, 5, 2, 4, 0x01, 1, 0, sp, LIST))
    m.put(MUTEX, struct.pack("<IIIIB", 0, NAMES + 64, T1, 0, 1))
    exc = 0xFFFFFFED if fp else 0xFFFFFFFD
    frame = struct.pack("<9I", *range(0x44, 0x4C), exc)
    if fp:
        frame += struct.pack("<16I", *range(0x1600, 0x1610))
    frame += struct.pack("<8I", 0xA0, 0xA1, 0xA2, 0xA3, 0xAC, 0x1235, 0x1234, 0x01000000)
    if fp:
        frame += struct.pack("<18I", *range(0x0600, 0x0612))
    stack = bytearray([0xEB]) * 1024
    stack[sp - STACK:sp - STACK + len(frame)] = frame
    m.put(STACK, bytes(stack))
    return m


class Reader(unittest.TestCase):
    def test_indexes_match_the_header(self):
        text = (ROOT / "include/emb/debug.h").read_text(encoding="utf-8")
        body = text[text.index("enum emb_debug_index {"):]
        body = body[:body.index("};")]
        names = re.findall(r"^\s*EMB_DEBUG_([A-Z0-9_]+)(?:\s*=\s*0)?,", body, re.M)
        self.assertEqual(tuple(names), dd.INDEXES)

    def test_threads(self):
        d = dd.Descriptor(build(), DESC)
        self.assertEqual((d.port, d.variant), ("cortex_m", "armv7em"))
        ts = d.threads()
        self.assertEqual([t["name"] for t in ts], ["runner", "waiter"])
        runner, waiter = ts
        self.assertEqual((runner["state"], runner["effective_priority"]), ("running", 5))
        self.assertEqual(runner["owned"], [{"address": MUTEX, "name": "lock", "count": 1}])
        self.assertEqual((waiter["state"], waiter["reason"]), ("blocked", "object"))
        r = waiter["registers"]
        self.assertEqual((r["r4"], r["r11"], r["pc"], r["lr"], r["r12"]), (0x44, 0x4B, 0x1234, 0x1235, 0xAC))
        self.assertEqual(r["sp"], STACK + 1024)
        self.assertNotIn("s16", r)
        self.assertEqual(waiter["stack_in_use"], 36 + 32)

    def test_floating_point_frame(self):
        d = dd.Descriptor(build(fp=True), DESC)
        r = d.threads()[1]["registers"]
        self.assertEqual((r["s16"], r["s31"], r["s0"], r["fpscr"]), (0x1600, 0x160F, 0x0600, 0x0610))
        self.assertEqual((r["pc"], r["sp"]), (0x1234, STACK + 1024))

    def test_stack_high_water(self):
        d = dd.Descriptor(build(flags=0x0004 | 0x0008 | 0x0020), DESC)
        self.assertEqual(d.threads()[1]["stack_high_water"], 36 + 32)

    def test_refusals(self):
        m = build()
        m.put(DESC, b"EMBX" + bytes(12))
        with self.assertRaises(dd.DescriptorError):
            dd.Descriptor(m, DESC)
        m = build()
        blob = bytearray(m.blobs[DESC])
        blob[4] = 9  # version 9
        m.put(DESC, blob)
        with self.assertRaisesRegex(dd.DescriptorError, "version 9"):
            dd.Descriptor(m, DESC)

    def test_devices(self):
        """SPEC-016 §9: the board's devices in order, with lifecycle, level and last status."""
        m = build()
        blob = bytearray(m.blobs[DESC])
        table, array, d1, d2, s1, s2, names = 0x5000, 0x5010, 0x5020, 0x5040, 0x5060, 0x5070, 0x5080
        vals = {"DEVICE_TABLE": table, "DT_DEVICES": 0, "DT_COUNT": 4, "DT_EARLY": 6, "D_NAME": 0, "D_STATE": 12,
                "D_CLASS": 24, "DS_LIFECYCLE": 0, "DS_LAST_STATUS": 4}
        for name, value in vals.items():
            struct.pack_into("<I", blob, 16 + 4 * dd.INDEXES.index(name), value)
        m.put(DESC, bytes(blob))
        m.put(table, struct.pack("<IHH", array, 2, 1))
        m.put(array, struct.pack("<II", d1, d2))
        for dev, name, state, cls in ((d1, names, s1, 1), (d2, names + 8, s2, 5)):
            b = bytearray(32)
            struct.pack_into("<I", b, 0, name)
            struct.pack_into("<I", b, 12, state)
            struct.pack_into("<H", b, 24, cls)
            m.put(dev, bytes(b))
        m.put(s1, struct.pack("<BBBBh", 2, 0, 0, 0, 0) + bytes(4))
        m.put(s2, struct.pack("<BBBBh", 4, 0, 0, 0, -5) + bytes(4))
        m.put(names, b"uart0\0\0\0wdt\0")
        devs = dd.Descriptor(m, DESC).devices()
        self.assertEqual([(x["name"], x["class"], x["level"], x["lifecycle"], x["last_status"]) for x in devs],
                         [("uart0", "uart", "early", "ready", 0), ("wdt", "watchdog", "kernel", "failed", -5)])
        self.assertIn("wdt     watchdog  kernel  failed  -5", dd.device_table(devs))

    def test_no_devices(self):
        self.assertEqual(dd.Descriptor(build(), DESC).devices(), [])  # ABSENT: CONFIG_EMB_DEVICE=n

    def test_older_kernel_has_fewer_values(self):
        m = build()
        blob = bytearray(m.blobs[DESC])
        struct.pack_into("<H", blob, 6, 50)  # it knew only the first 50 values
        m.put(DESC, bytes(blob[:16 + 50 * 4]))
        d = dd.Descriptor(m, DESC)
        self.assertIsNone(d.v["FRAME_TABLE_COUNT"])
        self.assertIsNone(d.v["FRAME_SIZE"])


if __name__ == "__main__":
    unittest.main()
