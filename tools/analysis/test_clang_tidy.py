# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The clang-tidy runner's parsing and its choice of translation units, without clang-tidy."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import clang_tidy as ct  # noqa: E402


class Parse(unittest.TestCase):
    def test_a_finding_relative_to_the_root(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            line = f"{root}/kernel/sched/sched.c:176:32: error: do not cast [bugprone-x,-warnings-as-errors]"
            self.assertEqual(ct.finding(line, root), ("kernel/sched/sched.c", 176, 32, "bugprone-x", "do not cast"))

    def test_a_warning_and_a_dotted_check(self):
        f = ct.finding("/x/list.h:98:19: warning: Access to field 'next' [clang-analyzer-core.NullDereference]",
                       Path("/elsewhere"))
        self.assertEqual(f[3], "clang-analyzer-core.NullDereference")
        self.assertEqual(f[0], "/x/list.h")  # outside the root: kept as it is

    def test_a_compile_error_has_no_check(self):
        f = ct.finding("/r/arch/a.c:19:9: error: expected identifier or '('", Path("/r"))
        self.assertEqual((f[3], f[4]), ("clang-diagnostic-error", "expected identifier or '('"))

    def test_notes_and_context_are_not_findings(self):
        for line in ("/r/k.c:1:2: note: expanded from macro 'X'", "  382 |     o = (const embk_obj_t *)",
                     "      |                             ^", "2 warnings generated.", ""):
            self.assertIsNone(ct.finding(line, Path("/r")), line)


class Units(unittest.TestCase):
    def test_product_c_files_only_once_each(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            build = root / "build" / "x"
            build.mkdir(parents=True)
            entries = [
                {"directory": str(build), "file": str(root / "kernel/a.c")},
                {"directory": str(build), "file": str(root / "kernel/a.c")},  # a second target
                {"directory": str(root), "file": "arch/riscv/rv_port.c"},  # relative to its directory
                {"directory": str(build), "file": str(root / "arch/riscv/rv_switch.S")},  # assembly
                {"directory": str(build), "file": str(root / "tests/conformance/t.c")},  # a test
                {"directory": str(build), "file": str(root / "samples/s/main.c")},  # a sample
                {"directory": str(build), "file": str(build / "generated/build_id.c")},  # generated
                {"directory": str(build), "file": str(root / "examples/controller/main.c")},
                {"directory": str(build), "file": "/usr/lib/outside.c"},  # outside the tree
            ]
            (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")
            self.assertEqual(ct.units(build, root),
                             ["kernel/a.c", "arch/riscv/rv_port.c", "examples/controller/main.c"])

    def test_a_tree_without_a_database_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit):
                ct.units(Path(d), Path(d))


if __name__ == "__main__":
    unittest.main()
