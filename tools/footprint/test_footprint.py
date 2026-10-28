# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import footprint  # noqa: E402

MAP = """\
Archive member included to satisfy reference by file (symbol)

Linker script and memory map

 .text          0x00000000       0x40 kernel/libemb_kernel.a(sched.c.obj)
 .text.emb_thread_init
                0x00000040       0x20 kernel/libemb_kernel.a(thread.c.obj)
 .progmem.data  0x00000060       0x10 kernel/libemb_kernel.a(word.c.obj)
 .text          0x00000070       0x30 arch/avr/libemb_arch.a(avr_port.c.obj)
 .text          0x000000a0       0x08 samples/footprint_t1/CMakeFiles/footprint_t1.dir/main.c.obj
 .data          0x00800100        0x2 kernel/libemb_kernel.a(fault.c.obj)
 .bss.embk_idle_thread
                0x00800102       0x20 kernel/libemb_kernel.a(sched.c.obj)
 .bss           0x00800122        0x4 arch/avr/libemb_arch.a(avr_context.c.obj)
 .bss           0x00800126        0x6 kernel/libemb_kernel.a(exec.c.obj)
 .debug_info    0x00000000      0x100 kernel/libemb_kernel.a(sched.c.obj)
OUTPUT(footprint_t1.elf elf32-avr)
"""

STORAGE_H = """\
#define EMB_SEM_STORAGE_SIZE  12u
#define EMB_THREAD_STORAGE_SIZE  32u
#define EMB_THREAD_STORAGE_ALIGN 1u
"""


class Footprint(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, text):
        path = os.path.join(self.dir.name, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path

    def run_main(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            rc = footprint.main(list(args))
        return rc, out.getvalue()

    def test_map_attributes_sections_to_components(self):
        sums = footprint.parse_map(self.write("a.map", MAP))
        self.assertEqual(sums["kernel/sched"], {"text": 0x40, "data": 0, "bss": 0x20})
        self.assertEqual(sums["kernel/thread"]["text"], 0x20)
        self.assertEqual(sums["kernel/word"]["text"], 0x10)  # flash tables count as text
        self.assertEqual(sums["kernel/fault"]["data"], 2)
        self.assertEqual(sums["arch"], {"text": 0x30, "data": 0, "bss": 4})
        self.assertEqual(sums["app"]["text"], 8)
        self.assertNotIn("debug", "".join(sums))

    def test_control_block_comes_from_the_storage_header(self):
        self.assertEqual(footprint.thread_storage_size(self.write("s.h", STORAGE_H)), 32)
        self.assertIsNone(footprint.thread_storage_size(self.write("t.h", "#define X 1u\n")))

    def test_report_and_thresholds(self):
        m, h = self.write("a.map", MAP), self.write("s.h", STORAGE_H)
        report = os.path.join(self.dir.name, "r.json")
        ok = self.write("ok.json", json.dumps({"kernel_text_max": 0x70, "kernel_ram_max": 40, "tcb_max": 32}))
        rc, out = self.run_main(m, "--storage-h", h, "--json", report, "--thresholds", ok)
        self.assertEqual(rc, 0, out)
        with open(report, encoding="utf-8") as f:
            r = json.load(f)
        self.assertEqual(r["kernel"], {"text": 0x70, "data": 2, "bss": 0x26})
        self.assertEqual((r["kernel_ram"], r["tcb"], r["tcb_source"]), (40, 32, "EMB_THREAD_STORAGE_SIZE"))
        tight = self.write("tight.json", json.dumps({"kernel_ram_max": 39, "tcb_max": 31}))
        rc, out = self.run_main(m, "--storage-h", h, "--thresholds", tight)
        self.assertEqual(rc, 1)
        self.assertIn("kernel_ram_max = 39, measured 40", out)
        self.assertIn("tcb_max = 31, measured 32", out)

    def test_storage_header_without_the_size_is_an_error(self):
        rc, _ = self.run_main(self.write("a.map", MAP), "--storage-h", self.write("t.h", "\n"))
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main()
