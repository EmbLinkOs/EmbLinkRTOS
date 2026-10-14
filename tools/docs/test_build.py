# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check invalid normative inputs cannot produce a misleading catalog."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import build

REQUIREMENT = """### API-001  Status result
**Statement.** Calls shall return an explicit status.
**Rationale.** Errors need defined handling.
**Status.** Accepted 2026-10-07
**Verification.** Test: a planned case, not a passing result.
**Trace.** SPEC-001 §4
"""


class CatalogIntegrity(unittest.TestCase):
    def extract(self, content):
        with tempfile.TemporaryDirectory() as temporary:
            docs = Path(temporary)
            (docs / "requirements").mkdir()
            (docs / "requirements/API.md").write_text(content)
            with patch.object(build, "DOCS", docs):
                return build.requirement_catalog()

    def test_design_acceptance_never_becomes_execution_evidence(self):
        record = self.extract(REQUIREMENT)[0]
        self.assertEqual(record["design_status"], "Accepted")
        self.assertEqual(record["evidence"], "not evaluated")

    def test_duplicate_ids_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.extract(REQUIREMENT + REQUIREMENT)

    def test_missing_verification_cannot_be_exported(self):
        content = REQUIREMENT.replace("**Verification.** Test: a planned case, not a passing result.\n", "")
        with self.assertRaisesRegex(ValueError, "missing Verification"):
            self.extract(content)

    def test_unknown_status_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown status"):
            self.extract(REQUIREMENT.replace("Accepted 2026-10-07", "Ready"))

    def test_malformed_heading_cannot_silently_drop_a_requirement(self):
        with self.assertRaisesRegex(ValueError, "malformed requirement heading"):
            self.extract(REQUIREMENT + REQUIREMENT.replace("API-001  Status", "API-002 Status"))

    def test_zero_requirements_is_a_failure(self):
        with self.assertRaisesRegex(ValueError, "No requirements"):
            self.extract("# An empty source\n")


class AnnotationIntegrity(unittest.TestCase):
    def test_header_normalization_preserves_empty_contracts_and_parameter_labels(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            headers = root / "include/emb"
            headers.mkdir(parents=True)
            (headers / "example.h").write_text("""/**
 * emb_example() - Wait.
 * @timeout: How long to wait.
 * @config
 */
""")
            output = root / "output"
            output.mkdir()
            with patch.object(build, "ROOT", root):
                build.prepare_headers(output)
            result = (output / "doxygen-input/example.h").read_text()
            self.assertIn("@param timeout How long to wait.", result)
            self.assertIn(" * @config\n", result)
            self.assertNotIn(r"\p config", result)

    def test_reflowed_contracts_keep_every_requirement_and_ignore_parameter_names(self):
        tags = build.contract_tags("""
 * emb_example() - Example.
 * @timeout: A parameter, not the time contract.
 * @ctx      thread
 * @time     O(waiters + owned)
 *           plus timeout work
 * @config
 * @req      KRN-SYNC-020
 *           KRN-SYNC-028
 * @stable   yes
 *
 * Return: EMB_OK.
""")
        self.assertEqual(tags["req"], "KRN-SYNC-020 KRN-SYNC-028")
        self.assertEqual(tags["time"], "O(waiters + owned) plus timeout work")
        self.assertEqual(tags["config"], "")
        self.assertNotIn("timeout", tags)
        self.assertEqual(tags["stable"], "yes")

    def record(self, **changes):
        return {"function": "emb_example", "missing_tags": [], "missing_parameters": [],
                "unresolved_requirement_ids": [], "evidence": "not evaluated", **changes}

    def test_each_documentation_omission_fails_the_build(self):
        for field in ("missing_tags", "missing_parameters", "unresolved_requirement_ids"):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "emb_example"):
                build.validate_api_annotations([self.record(**{field: ["missing"]})])

    def test_complete_metadata_does_not_change_execution_evidence(self):
        record = self.record()
        build.validate_api_annotations([record])
        self.assertEqual(record["evidence"], "not evaluated")


if __name__ == "__main__":
    unittest.main()
