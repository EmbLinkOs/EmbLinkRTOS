# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import collect  # noqa: E402
import matrix  # noqa: E402

LOG = """\
Start testing: Oct 27 18:30 CET
----------------------------------------------------------
1/2 Testing: test_sem
1/2 Test: test_sem
Command: "/build/tests/conformance/test_sem"
Output:
----------------------------------------------------------
EVIDENCE v1 suite=test_sem config=abc123 profile=base board=native checked=1
TEST sem_counts
  -> ok
  req KRN-SYNC-020 KRN-SYNC-021
CASE sem_counts outcome=passed asserts=8 skips=0
TEST sem_misuse
emb native: kernel fault class 5 code 3 argument 1 thread 0 context 0 at emb_sem_init, halting
  skip: fault check needs fork
  -> ok
  req API-015
CASE sem_misuse outcome=skipped asserts=0 skips=1
3 tests, 0 failed, 1 skipped, 0 ticks
<end of output>
Test time =   0.01 sec
----------------------------------------------------------
Test Passed.
"test_sem" end time: Oct 27 18:30 CET
----------------------------------------------------------
2/2 Testing: test_crash
2/2 Test: test_crash
Output:
----------------------------------------------------------
EVIDENCE v1 suite=test_crash config=abc123 profile=base board=native checked=1
TEST crash_case
  req KRN-OBJ-007
<end of output>
Test Failed.
"""


class Collect(unittest.TestCase):
    def test_cases_reqs_and_outcomes(self):
        targets = collect.parse_log(LOG)
        self.assertEqual([(t["name"], t["ctest"], t["suite"]) for t in targets],
                         [("test_sem", "passed", "test_sem"), ("test_crash", "failed", "test_crash")])
        sem = targets[0]["cases"]
        self.assertEqual(sem[0], {"name": "sem_counts", "outcome": "passed", "asserts": 8, "skips": 0,
                                  "skip_reasons": [], "reqs": ["KRN-SYNC-020", "KRN-SYNC-021"]})
        self.assertEqual((sem[1]["outcome"], sem[1]["skip_reasons"], sem[1]["reqs"]),
                         ("skipped", ["fault check needs fork"], ["API-015"]))
        self.assertEqual(targets[0]["identity"]["config"], "abc123")

    def test_case_without_end_is_incomplete(self):
        crash = collect.parse_log(LOG)[1]["cases"]
        self.assertEqual([(c["name"], c["outcome"]) for c in crash], [("crash_case", "incomplete")])


class Matrix(unittest.TestCase):
    def test_cell_needs_an_executed_assertion(self):
        passed = {"outcome": "passed", "asserts": 2}
        self.assertEqual(matrix.cell([]), "no case")
        self.assertEqual(matrix.cell([passed]), "passed")
        self.assertEqual(matrix.cell([{"outcome": "passed", "asserts": 0}]), "no assertion")
        self.assertEqual(matrix.cell([{"outcome": "skipped", "asserts": 0}]), "skipped")
        self.assertEqual(matrix.cell([passed, {"outcome": "failed", "asserts": 1}]), "failed")
        self.assertEqual(matrix.cell([passed, {"outcome": "incomplete", "asserts": 0}]), "failed")

    def test_rows_keep_design_status_and_unrun_reasons_apart(self):
        records = [{"id": "KRN-SYNC-020", "design_status": "Accepted"}]
        evidence = [{"preset": "native-gcc", "targets": collect.parse_log(LOG)}]
        configs = {"native-gcc": {"scope": "host"}, "board": {"scope": "hardware", "unrun": "no board"}}
        rows = {r["configuration"]: r for r in matrix.build_matrix(records, evidence, configs)}
        self.assertEqual((rows["native-gcc"]["design_status"], rows["native-gcc"]["evidence_outcome"],
                          rows["native-gcc"]["execution_scope"], rows["native-gcc"]["cases"]),
                         ("Accepted", "passed", "host", "test_sem:sem_counts"))
        self.assertEqual((rows["board"]["evidence_outcome"], rows["board"]["execution_scope"]),
                         ("not run", "unrun: no board"))

    def test_pending_groups_come_from_the_requirements_readme(self):
        pending = matrix.pending_groups()
        self.assertIn("KRN-RQ", pending)
        self.assertNotIn("KRN-SYNC", pending)
        self.assertNotIn("KRN-SCH", pending)  # migrated 2026-10-31


if __name__ == "__main__":
    unittest.main()


import check_bounds  # noqa: E402


class Bounds(unittest.TestCase):
    BOUND = {"id": "waiters", "input": "x", "bound": "y", "limit": "z", "enforcement": "structural",
             "masked": True, "profiles": "p", "evidence": "e"}

    def test_every_non_constant_cost_needs_known_inputs(self):
        inv = {"bounds": [self.BOUND], "apis": {"emb_a": ["waiters"], "emb_b": ["nonsense"], "emb_c": ["waiters"]}}
        errs = check_bounds.problems(inv, {"emb_a": "O(waiters)", "emb_b": "O(n)", "emb_d": "O(n)"})
        self.assertTrue(any("emb_d" in e and "no inventory entry" in e for e in errs))
        self.assertTrue(any("emb_b" in e and "nonsense" in e for e in errs))
        self.assertTrue(any("emb_c" in e and "O(1) or it is not public" in e for e in errs))
        self.assertFalse(any("emb_a" in e for e in errs))

    def test_entries_need_every_field_and_a_known_enforcement(self):
        bad = dict(self.BOUND, enforcement="hope", limit="")
        errs = check_bounds.problems({"bounds": [bad], "apis": {}}, {})
        self.assertTrue(any("missing limit" in e for e in errs))
        self.assertTrue(any("enforcement 'hope'" in e for e in errs))

    def test_the_repository_inventory_is_complete(self):
        import json
        inv = json.loads(check_bounds.INVENTORY.read_text(encoding="utf-8"))
        self.assertEqual(check_bounds.problems(inv, check_bounds.annotated_costs()), [])
