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


if __name__ == "__main__":
    unittest.main()
