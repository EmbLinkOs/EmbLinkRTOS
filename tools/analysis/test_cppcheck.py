# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The cppcheck runner's parsing, compiler probe and suppressions file, without cppcheck."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cppcheck as cc  # noqa: E402


class CompilerProbe(unittest.TestCase):
    def test_target_flags_kept_and_the_rest_dropped(self):
        args = ["/usr/bin/arm-none-eabi-gcc", "-DX=1", "-I/r/include", "-mcpu=cortex-m4", "-mthumb",
                "-mfloat-abi=hard", "-std=c11", "-O2", "-Wall", "-ffile-prefix-map=/r=.", "-o", "a.o",
                "-c", "/r/kernel/a.c", "-MD", "-MF", "a.d"]
        self.assertEqual(cc.compiler_argv(args), ["/usr/bin/arm-none-eabi-gcc", "-mcpu=cortex-m4", "-mthumb",
                                                  "-mfloat-abi=hard", "-std=c11", "-O2"])

    def test_a_launcher_is_skipped(self):
        args = ["/usr/bin/python3", "-I", "/r/tools/toolchain/kernel_compile.py", "/opt/embcc/bin/embcc",
                "--target=thumbv7em-none-eabi", "-c", "x.c"]
        self.assertEqual(cc.compiler_argv(args), ["/opt/embcc/bin/embcc", "--target=thumbv7em-none-eabi"])
        self.assertEqual(cc.compiler_argv(["ccache", "clang", "--target=riscv32", "-c", "x.c"]),
                         ["clang", "--target=riscv32"])

    def test_a_flag_with_a_separate_value(self):
        self.assertEqual(cc.compiler_argv(["clang", "-target", "avr", "-isysroot", "/sdk", "-c", "x.c"]),
                         ["clang", "-target", "avr", "-isysroot", "/sdk"])

    def test_command_and_arguments_forms(self):
        self.assertEqual(cc.command({"command": "gcc -DA='a b' -c x.c"}), ["gcc", "-DA=a b", "-c", "x.c"])
        self.assertEqual(cc.command({"arguments": ["gcc", "-c", "x.c"]}), ["gcc", "-c", "x.c"])

    def test_feature_test_macros_are_left_to_cppcheck(self):
        self.assertTrue(cc.SKIP_PREDEF.match("#define __has_include(STR) __has_include__(STR)"))
        self.assertFalse(cc.SKIP_PREDEF.match("#define __GNUC__ 14"))


class Parse(unittest.TestCase):
    def test_a_finding_relative_to_the_root(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            line = f"{root}/kernel/irq/irq.c:29:5: style: Checking if unsigned expression 'level' is less than zero. [unsignedLessThanZero]"
            self.assertEqual(cc.finding(line, root), ("kernel/irq/irq.c", 29, 5, "unsignedLessThanZero",
                                                      "Checking if unsigned expression 'level' is less than zero."))

    def test_a_generated_header_is_named_without_its_build(self):
        f = cc.finding("/r/build/cm4-an386-clang/generated/include/emb/storage.h:17:5: style: x [misra-c2012-19.2]",
                       Path("/r"))
        self.assertEqual(f[0], "generated/include/emb/storage.h")
        self.assertEqual(f[4], "MISRA C:2012 rule 19.2")

    def test_other_lines_are_not_findings(self):
        self.assertIsNone(cc.finding("Checking kernel/irq/irq.c ...", Path("/r")))
        self.assertIsNone(cc.finding("    for (; e < stop; e++) {", Path("/r")))


class Baseline(unittest.TestCase):
    def test_counts_per_file_and_rule(self):
        fs = [("a.c", 1, 1, "misra-c2012-2.5", "m"), ("a.c", 1, 1, "misra-c2012-2.5", "m2"),
              ("a.c", 9, 1, "misra-c2012-2.5", "m"), ("a.c", 3, 2, "misra-c2012-8.7", "m"),
              ("b.c", 4, 1, "comparePointers", "m")]
        self.assertEqual(cc.misra_counts(fs), {"a.c": {"misra-c2012-2.5": 2, "misra-c2012-8.7": 1}})

    def test_gains_fail_and_losses_are_reported(self):
        now = {"a.c": {"misra-c2012-2.5": 3}, "c.c": {"misra-c2012-10.4": 1}}
        was = {"a.c": {"misra-c2012-2.5": 2, "misra-c2012-8.7": 1}}
        gains, losses = cc.against_baseline(now, was)
        self.assertEqual(gains, [("a.c", "misra-c2012-2.5", 3, 2), ("c.c", "misra-c2012-10.4", 1, 0)])
        self.assertEqual(losses, [("a.c", "misra-c2012-8.7", 0, 1)])


class Suppressions(unittest.TestCase):
    def write(self, text):
        d = tempfile.mkdtemp()
        p = Path(d) / ".cppcheck-suppressions"
        p.write_text(text, encoding="utf-8")
        return p

    def test_ids_after_their_reason(self):
        p = self.write("# header\n\n# reason\nconstParameterPointer\nconstVariablePointer\n")
        self.assertEqual(cc.tree_wide(p), ["constParameterPointer", "constVariablePointer"])

    def test_an_entry_naming_a_file_is_refused(self):
        with self.assertRaises(SystemExit):
            cc.tree_wide(self.write("# reason\ncomparePointers:kernel/exec/exec.c\n"))

    def test_an_entry_without_a_reason_is_refused(self):
        with self.assertRaises(SystemExit):
            cc.tree_wide(self.write("# reason\nconstParameterPointer\n\nvariableScope\n"))

    def test_the_tree_file_is_well_formed(self):
        self.assertTrue(cc.tree_wide())


class Units(unittest.TestCase):
    def test_product_c_units_only(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            build = root / "build" / "x"
            build.mkdir(parents=True)
            entries = [{"directory": str(build), "file": str(root / f), "command": "cc -c"} for f in
                       ("kernel/a.c", "tests/t.c", "build/x/generated/g.c", "arch/s.S", "kernel/a.c")]
            (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
            self.assertEqual(list(cc.units(build, root)), ["kernel/a.c"])


if __name__ == "__main__":
    unittest.main()
