# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The stack bound's prologue reading, call graph and depth, on synthetic disassembly."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import memory_report  # noqa: E402
import stack_bound as sb  # noqa: E402


def fn(start, *insns):
    return {"start": start, "insns": [(start + 2 * i, op, args) for i, (op, args) in enumerate(insns)]}


class Prologue(unittest.TestCase):
    def test_push_and_sub(self):
        f = fn(0x100, ("push", "{r4, r5, r6, lr}"), ("sub", "sp, #16"), ("bl", "200 <x>"))
        self.assertEqual(sb.prologue_frame(f["insns"]), 32)

    def test_stmdb_range_and_vpush(self):
        f = fn(0x100, ("stmdb", "sp!, {r4-r11, lr}"), ("vpush", "{d8-d9}"), ("sub.w", "sp, sp, #0x20"))
        self.assertEqual(sb.prologue_frame(f["insns"]), 36 + 16 + 32)

    def test_stops_at_branch(self):
        f = fn(0x100, ("b.n", "104 <y>"), ("push", "{r4, lr}"))
        self.assertEqual(sb.prologue_frame(f["insns"]), 0)


class Graph(unittest.TestCase):
    def funcs(self):
        return {
            "a": fn(0x100, ("push", "{lr}"), ("bl", "200 <b>"), ("blx", "r3")),
            "b": fn(0x200, ("push", "{r4, lr}"), ("b.w", "300 <c>")),  # a tail call
            "c": fn(0x300, ("vadd.f32", "s0, s1, s2"), ("bx", "lr")),
            "d": fn(0x400, ("bne.n", "404 <d+0x4>"), ("bl", "100 <a>")),
        }

    def test_edges(self):
        g = sb.call_graph(self.funcs())
        self.assertEqual(g["a"]["calls"], {"b"})
        self.assertTrue(g["a"]["indirect"])
        self.assertEqual(g["b"]["calls"], {"c"})
        self.assertTrue(g["c"]["fp"])
        self.assertEqual(g["d"]["calls"], {"a"})  # a branch inside d is not a call

    def test_depth_resolves_indirect(self):
        funcs = self.funcs()
        g = sb.call_graph(funcs)
        frames = {n: sb.prologue_frame(f["insns"]) for n, f in funcs.items()}
        b = sb.Bound(g, frames, {"a": ["d2"]})
        g["d2"] = {"calls": set(), "indirect": False, "fp": False}
        frames["d2"] = 40
        depth, path, fp = b.depth("a")
        self.assertEqual(depth, 4 + 40)  # a's 4, then d2's 40 beats b's 8
        self.assertEqual(path, ["a", "d2"])
        self.assertTrue(fp)  # c, through b, uses the FPU

    def test_unresolved_and_recursion(self):
        g = {"x": {"calls": {"y"}, "indirect": True, "fp": False},
             "y": {"calls": {"x"}, "indirect": False, "fp": False}}
        b = sb.Bound(g, {"x": 8, "y": 8}, {})
        b.depth("x")
        self.assertTrue(any("indirect" in p for p in b.problems))
        self.assertTrue(any("recursion" in p for p in b.problems))

    def test_clone_names(self):
        self.assertEqual(sb.base_name("raise_info.constprop.0"), "raise_info")
        self.assertEqual(sb.base_name("send_common.isra.0"), "send_common")
        self.assertEqual(sb.base_name("embk_sched_select"), "embk_sched_select")

    def test_an_inlined_indirect_call_resolves_through_its_chain(self):
        # under LTO, embk_isr_dispatch (named in the roots) is inlined into the vector's entry
        funcs = {"entry": fn(0x100, ("push", "{lr}"), ("blx", "r3"), ("blx", "r4")),
                 "isr_a": fn(0x200, ("push", "{r4, lr}"), ("bx", "lr"))}
        g = sb.call_graph(funcs)
        self.assertEqual(g["entry"]["indirect"], [0x102, 0x104])
        chains = {0x102: ["embk_isr_dispatch", "entry"], 0x104: ["helper", "entry"]}
        sb.resolve_indirect(g, {"embk_isr_dispatch": ["isr_a"]}, lambda addrs: {a: chains[a] for a in addrs})
        self.assertEqual(g["entry"]["targets"], {"isr_a"})
        self.assertEqual(g["entry"]["unresolved"], [0x104])  # helper is not in the roots: a problem
        b = sb.Bound(g, {"entry": 4, "isr_a": 8}, {})
        self.assertEqual(b.depth("entry")[1], ["entry", "isr_a"])
        self.assertTrue(any("0x104" in p for p in b.problems))

    def test_a_clone_uses_its_functions_roots(self):
        g = sb.call_graph({"raise_info.constprop.0": fn(0x100, ("blx", "r3"))})
        sb.resolve_indirect(g, {"raise_info": []}, lambda addrs: self.fail("no chain needed"))
        self.assertEqual(g["raise_info.constprop.0"]["unresolved"], [])

    def test_bytes_after_a_function_are_not_its_code(self):
        # data after the last function in a section, disassembled as if it were code
        funcs = {"pendsv": fn(0x100, ("push", "{lr}"), ("bx", "lr"), ("cmp", "r0, r0"), ("bx", "sl"))}
        trimmed = sb.trim(funcs, {"pendsv": 4})
        self.assertEqual([op for _, op, _ in trimmed["pendsv"]["insns"]], ["push", "bx"])
        self.assertEqual(sb.call_graph(trimmed)["pendsv"]["indirect"], [])

    def test_regs(self):
        self.assertEqual(sb.regs_in("{r4-r11, lr}"), 9)
        self.assertEqual(sb.regs_in("sp!, {r3}"), 1)


class Report(unittest.TestCase):
    def test_regions(self):
        text = """Memory Configuration

Name             Origin             Length             Attributes
FLASH            0x08000000         0x00080000         xr
RAM              0x20000000         0x00020000         xrw
*default*        0x00000000         0xffffffff

Linker script and memory map
"""
        p = Path(self.id().replace(".", "_") + ".map")
        try:
            p.write_text(text, encoding="utf-8")
            regs = memory_report.regions(p)
        finally:
            p.unlink()
        self.assertEqual([(r["name"], r["length"]) for r in regs], [("FLASH", 0x80000), ("RAM", 0x20000)])


if __name__ == "__main__":
    unittest.main()
