# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The changelog entry files: their check, and folding them into CHANGELOG.md."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changes  # noqa: E402

CHANGELOG = "# Changelog\n\nIntro.\n\n## Unreleased\n\n### Added (older, 2026-11-01)\n\n- Old.\n"


def entry(kind="Fixed", topic="the thing", date="2026-11-08", body="- It works."):
    return f"### {kind} ({topic}, {date})\n\n{body}\n"


class Check(unittest.TestCase):
    def files(self, **named):
        d = Path(tempfile.mkdtemp())
        for name, text in named.items():
            (d / name).write_text(text, encoding="utf-8")
        return d

    def test_a_good_entry(self):
        d = self.files(**{"fix-x.md": entry(), "README.md": "# not an entry\n"})
        self.assertEqual([p.name for p in changes.entries(d)], ["fix-x.md"])
        self.assertEqual(changes.problems(d / "fix-x.md"), [])

    def test_two_kinds_in_one_file(self):
        d = self.files(**{"x.md": entry() + "\n" + entry(kind="Added")})
        self.assertEqual(changes.problems(d / "x.md"), [])

    def test_bad_entries(self):
        d = self.files(**{"x.md": "### Fixed: the thing\n\n- It works.\n", "Y.md": entry(),
                          "z.md": entry(body="Just prose."), "r.md": "## 1.0.0\n\n" + entry()})
        self.assertTrue(any("first line" in p for p in changes.problems(d / "x.md")))
        self.assertTrue(any("lower case" in p for p in changes.problems(d / "Y.md")))
        self.assertTrue(any("no '- ' item" in p for p in changes.problems(d / "z.md")))
        self.assertTrue(any("release heading" in p for p in changes.problems(d / "r.md")))

    def test_the_trees_entries_are_well_formed(self):
        self.assertEqual([p for f in changes.entries() for p in changes.problems(f)], [])


class Collect(unittest.TestCase):
    def test_newest_first_then_by_name_and_the_files_go(self):
        d = Path(tempfile.mkdtemp())
        log = d / "CHANGELOG.md"
        log.write_text(CHANGELOG, encoding="utf-8")
        folder = d / "changes"
        folder.mkdir()
        (folder / "b.md").write_text(entry(topic="b", date="2026-11-08"), encoding="utf-8")
        (folder / "a.md").write_text(entry(topic="a", date="2026-11-08"), encoding="utf-8")
        (folder / "c.md").write_text(entry(topic="c", date="2026-11-09"), encoding="utf-8")
        (folder / "README.md").write_text("# kept\n", encoding="utf-8")
        self.assertEqual(changes.collect(folder, log), ["c.md", "a.md", "b.md"])
        text = log.read_text(encoding="utf-8")
        self.assertLess(text.index("(c, "), text.index("(a, "))
        self.assertLess(text.index("(a, "), text.index("(b, "))
        self.assertLess(text.index("(b, "), text.index("(older, "))
        self.assertEqual([p.name for p in folder.iterdir()], ["README.md"])

    def test_a_malformed_entry_stops_it(self):
        d = Path(tempfile.mkdtemp())
        (d / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
        (d / "changes").mkdir()
        (d / "changes" / "x.md").write_text("no heading\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            changes.collect(d / "changes", d / "CHANGELOG.md")
        self.assertEqual((d / "CHANGELOG.md").read_text(encoding="utf-8"), CHANGELOG)


if __name__ == "__main__":
    unittest.main()
