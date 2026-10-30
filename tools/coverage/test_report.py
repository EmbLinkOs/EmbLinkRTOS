# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Unit tests for the coverage report's gcov parsing and totals."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import report  # noqa: E402

SAMPLE = """File '/x/kernel/sched/sched.c'
Lines executed:50.00% of 10
Branches executed:100.00% of 4
Taken at least once:75.00% of 4
No calls

File '/x/kernel/include/embk/list.h'
Lines executed:100.00% of 3
No branches
No calls
"""


class Report(unittest.TestCase):
    def test_gcov_blocks_parse_lines_and_branches(self):
        blocks = {m.group(1): m.groups()[1:] for m in report.BLOCK.finditer(SAMPLE)}
        self.assertEqual(blocks["/x/kernel/sched/sched.c"], ("50.00", "10", "75.00", "4"))
        self.assertEqual(blocks["/x/kernel/include/embk/list.h"][:2], ("100.00", "3"))

    def test_exclusions_leave_the_totals(self):
        files = {"kernel/a.c": dict(lines=10, lines_hit=5, branches=4, branches_taken=3, executed=True),
                 "kernel/b.c": dict(lines=10, lines_hit=0, branches=0, branches_taken=0, executed=False)}
        md, total = report.render(files, {"kernel/b.c": "tiny profile only"}, "t")
        self.assertEqual((total["lines"], total["line_pct"], total["branch_pct"]), (10, 50.0, 75.0))
        self.assertIn("excluded: tiny profile only", md)


if __name__ == "__main__":
    unittest.main()
