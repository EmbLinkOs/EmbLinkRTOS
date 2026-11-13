# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The memory report's readers of LLD's map and of a linker script's MEMORY block, without a build."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import footprint  # noqa: E402
import memory_report as mr  # noqa: E402

LLD_MAP = """\
     VMA      LMA     Size Align Out     In      Symbol
 8000000  8000000      200     4 .vectors
 8000000  8000000      200     4         emblinkrtos/arch/cortex_m/libemb_arch.a(cm_vectors.c.obj):(.vectors)
 8000000  8000000      200     1                 embn_cm_vector_table
 8000200  8000200      140     4 .text
 8000200  8000200       6c     2         CMakeFiles/controller.dir/main.c.obj:(.text.control_thread_init_)
 800026c  800026c       d4     2         emblinkrtos/kernel/libemb_kernel.a(sched.c.obj):(.text.embk_sched_select)
 8000340  8000340       20     1         <internal>:(.rodata.str1.1)
20000000  8000360       18     4 .data
20000000  8000360       18     4         emblinkrtos/kernel/libemb_kernel.a(exec.c.obj):(.data.embk_cpu)
20000018 20000018      100     8 .bss
20000018 20000018      100     8         CMakeFiles/controller.dir/main.c.obj:(.bss.samples_ring_)
       0        0      3b     1         <internal>:(.comment)
"""

GNU_MAP_HEAD = """\
Memory Configuration

Name             Origin             Length             Attributes
FLASH            0x08000000         0x00080000         xr
RAM              0x20000000         0x00020000         xrw
*default*        0x00000000         0xffffffff

Linker script and memory map
"""


class Lld(unittest.TestCase):
    def test_the_format_is_recognised(self):
        self.assertTrue(footprint.is_lld(LLD_MAP))
        self.assertFalse(footprint.is_lld(GNU_MAP_HEAD))

    def test_input_sections_and_the_linkers_own(self):
        own = list(footprint.lld_inputs(LLD_MAP))
        self.assertEqual(own[1], (".text.control_thread_init_", 0x6c, "CMakeFiles/controller.dir/main.c.obj"))
        self.assertNotIn("<internal>", [o for _, _, o in own])
        self.assertEqual(len(list(footprint.lld_inputs(LLD_MAP, internal=True))), len(own) + 2)

    def test_components_and_merged_strings(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.map"
            p.write_text(LLD_MAP, encoding="utf-8")
            merged = {}
            sums = footprint.parse_map(str(p), merged)
            owners = mr.owners(str(p))
        self.assertEqual(sums["kernel/sched"]["text"], 0xd4)
        self.assertEqual(sums["kernel/exec"]["data"], 0x18)
        self.assertEqual(sums["app"], {"text": 0x6c, "data": 0, "bss": 0x100})
        self.assertEqual(sums["arch"]["text"], 0x200)
        self.assertEqual(merged, {"<linker>": 0x20})  # after merging, in no component
        self.assertEqual(owners, {"embk_cpu": "kernel/exec", "samples_ring_": "app"})


class Regions(unittest.TestCase):
    def test_memory_block_with_include_and_suffixes(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, "regions.ld").write_text(
                "/* generated */\nMEMORY\n{\n  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K\n"
                "  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K\n}\n", encoding="utf-8")
            Path(d, "app.ld").write_text("INCLUDE regions.ld\nSECTIONS { }\n", encoding="utf-8")
            regs = mr.script_regions(Path(d, "app.ld"))
        self.assertEqual(regs, [{"name": "FLASH", "origin": 0x08000000, "length": 512 * 1024},
                                {"name": "RAM", "origin": 0x20000000, "length": 128 * 1024}])

    def test_an_lld_map_finds_the_script_from_build_ninja(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, "regions.ld").write_text("MEMORY { RAM : ORIGIN = 0x20000000, LENGTH = 2K }\n", encoding="utf-8")
            Path(d, "build.ninja").write_text(f"  LINK_FLAGS = -T{d}/regions.ld -Wl,--gc-sections\n", encoding="utf-8")
            Path(d, "x.map").write_text(LLD_MAP, encoding="utf-8")
            self.assertEqual(mr.regions(str(Path(d, "x.map"))), [{"name": "RAM", "origin": 0x20000000, "length": 2048}])

    def test_an_lld_map_without_a_script_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, "x.map").write_text(LLD_MAP, encoding="utf-8")
            with self.assertRaises(OSError):
                mr.regions(str(Path(d, "x.map")))

    def test_gnu_regions_still_come_from_the_map(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, "x.map").write_text(GNU_MAP_HEAD, encoding="utf-8")
            self.assertEqual([r["name"] for r in mr.regions(str(Path(d, "x.map")))], ["FLASH", "RAM"])


if __name__ == "__main__":
    unittest.main()
