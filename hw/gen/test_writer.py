# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Generated files are editable (SPEC-014 §5.3, A126): the three-way merge, and the writer's
decision for each file, with its baseline."""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from emb_hwgen import writer  # noqa: E402


def lines(text):
    return text.splitlines(True)


def merge(base, ours, theirs):
    out, n = writer.merge3(lines(base), lines(ours), lines(theirs))
    return "".join(out), n


BASE = "a\nb\nc\nd\ne\n"


class Merge3(unittest.TestCase):
    def test_no_change(self):
        self.assertEqual(merge(BASE, BASE, BASE), (BASE, 0))

    def test_only_the_generator_changed(self):
        self.assertEqual(merge(BASE, BASE, "a\nB\nc\nd\ne\n"), ("a\nB\nc\nd\ne\n", 0))

    def test_only_the_author_changed(self):
        self.assertEqual(merge(BASE, "a\nb\nc\nmine\nd\ne\n", BASE), ("a\nb\nc\nmine\nd\ne\n", 0))

    def test_both_on_different_lines(self):
        ours = "a\nb\nmy line\nc\nd\ne\n"
        theirs = "a\nb\nc\nd\nE\nnew state\n"
        self.assertEqual(merge(BASE, ours, theirs), ("a\nb\nmy line\nc\nd\nE\nnew state\n", 0))

    def test_both_the_same_change(self):
        both = "a\nX\nc\nd\ne\n"
        self.assertEqual(merge(BASE, both, both), (both, 0))

    def test_both_on_the_same_lines_conflict(self):
        out, n = merge(BASE, "a\nmine\nc\nd\ne\n", "a\ntheirs\nc\nd\ne\n")
        self.assertEqual(n, 1)
        self.assertEqual(out, "a\n<<<<<<< yours\nmine\n||||||| generated before\nb\n=======\ntheirs\n"
                              ">>>>>>> generated now\nc\nd\ne\n")
        self.assertTrue(writer.has_conflict(out))

    def test_deletion_against_an_unrelated_insertion(self):
        self.assertEqual(merge(BASE, "a\nc\nd\ne\n", "a\nb\nc\nd\ne\nf\n"), ("a\nc\nd\ne\nf\n", 0))


class Write(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="hwgen-writer-"))
        self.addCleanup(shutil.rmtree, self.d)
        self.file, self.base = self.d / "src/main.c", self.d / ".base/src/main.c"

    def put(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def test_created_then_unchanged(self):
        self.assertEqual(writer.write(self.file, BASE, self.base), "created")
        self.assertEqual((self.file.read_text(), self.base.read_text()), (BASE, BASE))
        self.assertEqual(writer.write(self.file, BASE, self.base), "unchanged")

    def test_an_edit_is_kept_while_the_generated_text_does_not_change(self):
        writer.write(self.file, BASE, self.base)
        self.put(self.file, BASE + "my code\n")
        self.assertEqual(writer.write(self.file, BASE, self.base), "unchanged")
        self.assertEqual(self.file.read_text(), BASE + "my code\n")

    def test_not_edited_is_rewritten(self):
        writer.write(self.file, BASE, self.base)
        self.assertEqual(writer.write(self.file, "a\nB\nc\nd\ne\n", self.base), "rewritten")
        self.assertEqual(self.file.read_text(), "a\nB\nc\nd\ne\n")
        self.assertEqual(self.base.read_text(), "a\nB\nc\nd\ne\n")

    def test_edited_and_changed_is_merged(self):
        writer.write(self.file, BASE, self.base)
        self.put(self.file, "a\nb\nmy line\nc\nd\ne\n")
        self.assertEqual(writer.write(self.file, "a\nb\nc\nd\ne\nnew\n", self.base), "merged")
        self.assertEqual(self.file.read_text(), "a\nb\nmy line\nc\nd\ne\nnew\n")
        self.assertEqual(self.base.read_text(), "a\nb\nc\nd\ne\nnew\n")  # the baseline is the generator's

    def test_same_lines_are_a_conflict(self):
        writer.write(self.file, BASE, self.base)
        self.put(self.file, "a\nmine\nc\nd\ne\n")
        self.assertEqual(writer.write(self.file, "a\ntheirs\nc\nd\ne\n", self.base), "conflict")
        self.assertTrue(writer.has_conflict(self.file.read_text()))
        self.assertEqual(self.base.read_text(), "a\ntheirs\nc\nd\ne\n")

    def test_no_baseline_never_overwrites_an_edit(self):
        self.put(self.file, "a\nmine\nc\n")
        self.assertEqual(writer.write(self.file, "a\ntheirs\nc\n", self.base), "conflict")
        self.assertIn("mine\n", self.file.read_text())
        self.assertIn("theirs\n", self.file.read_text())


if __name__ == "__main__":
    unittest.main()
