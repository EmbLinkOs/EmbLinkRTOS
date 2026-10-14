# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check DCO certification cannot be satisfied by an unrelated or quoted trailer."""
import unittest

from check_dco import has_author_signoff, resolve


class DCOIntegrity(unittest.TestCase):
    def test_matching_author_trailer_is_accepted(self):
        self.assertTrue(has_author_signoff("Contributor", "dev@example.org",
                                          "Change\n\nSigned-off-by: Contributor <dev@example.org>\n"))

    def test_another_person_cannot_certify_the_author(self):
        self.assertFalse(has_author_signoff("Contributor", "dev@example.org",
                                           "Change\n\nSigned-off-by: Reviewer <review@example.org>\n"))

    def test_quoted_body_text_is_not_a_trailer(self):
        self.assertFalse(has_author_signoff("Contributor", "dev@example.org",
                                           "Example\n\nSigned-off-by: Contributor <dev@example.org>\n"
                                           "\nThis is body text following the example.\n"))

    def test_missing_trailer_is_rejected(self):
        self.assertFalse(has_author_signoff("Contributor", "dev@example.org", "Change\n"))

    def test_untrusted_revision_cannot_be_a_git_option(self):
        with self.assertRaisesRegex(ValueError, "full 40-character"):
            resolve("--all")


if __name__ == "__main__":
    unittest.main()

