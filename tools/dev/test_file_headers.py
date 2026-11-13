# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""tools/dev/file_headers.py: each shape of description becomes the same header, nothing of it is
lost, a converted file is left alone, the check finds what is missing; and # files take the notice."""
import pathlib
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import file_headers as fh  # noqa: E402

HEAD = [fh.SPDX, "/* Copyright 2026 Someone Else */"]
ORIGIN = ("Someone Else", "2026-10-01")


def convert(rel, desc, rest=("#include <x.h>", ""), names=()):
    with mock.patch.object(fh, "origin", return_value=ORIGIN):
        return fh.convert(rel, HEAD + list(desc) + list(rest), list(names) or [rel])


def words(lines):
    return " ".join(lines).replace("*", " ").split()


class Convert(unittest.TestCase):
    def test_block_comment_splits_brief_and_details(self):
        out = convert("kernel/a.c", ["/*", " * The thing (SPEC-001 §2). It does more,", " * at length.", " *",
                                     " * A second paragraph.", " */"])
        self.assertEqual(out[:2], HEAD)
        self.assertEqual(out[2:6], fh.NOTICE)
        self.assertIn(" * @file    a.c", out)
        self.assertIn(" * @brief   The thing (SPEC-001 §2).", out)
        self.assertIn(" * It does more, at length.", out)
        self.assertIn(" * A second paragraph.", out)
        self.assertIn(" * @author  Someone Else", out)
        self.assertIn(" * @date    2026-10-01 (created)", out)
        self.assertEqual(out[-2:], ["#include <x.h>", ""])

    def test_one_line_comment(self):
        out = convert("a.h", ["/* Status codes (SPEC-001 §4). */"])
        self.assertIn(" * @brief   Status codes (SPEC-001 §4).", out)
        self.assertNotIn(" * @details", out)

    def test_comment_starting_on_its_first_line(self):
        out = convert("a.c", ["/* SHA-256 (FIPS 180-4), the class's function. The rounds per", " * block. */"])
        self.assertIn(" * @brief   SHA-256 (FIPS 180-4), the class's function.", out)
        self.assertIn(" * The rounds per block.", out)

    def test_long_first_sentence_is_cut_at_its_colon(self):
        tail = "the message schedule, " * 6 + "and the padding."
        out = convert("a.c", ["/*", " * SHA-256 (FIPS 180-4 §6.2: the hash): " + tail, " */"])
        self.assertIn(" * @brief   SHA-256 (FIPS 180-4 §6.2: the hash).", out)  # not the colon in parentheses
        self.assertTrue(any(l.startswith(" * The message schedule,") for l in out))

    def test_identifiers_keep_their_case(self):
        out = convert("a.c", ["/*", " * The port's services, of which there are many and more: " + "emb_x() " * 12 + "are some.", " */"])
        self.assertTrue(any(l.startswith(" * emb_x()") for l in out))

    def test_abbreviations_do_not_end_the_brief(self):
        out = convert("a.c", ["/* Tables, e.g. the pins, from the description. More. */"])
        self.assertIn(" * @brief   Tables, e.g. the pins, from the description.", out)

    def test_nothing_of_the_description_is_lost(self):
        desc = ["/*", " * First sentence here. Then a list:", " * - one item", " * - another, longer item", " *",
                " * Lock: the scheduler's.", " */"]
        out = convert("a.c", desc)
        block = out[out.index("/**"):out.index(" */", out.index("/**"))]
        self.assertEqual(words(desc[1:-1]), [w for w in words(block) if not w.startswith("@")][2:-4])

    def test_blank_line_before_the_description(self):
        out = convert("a.c", ["", "/* The probe. */"])
        self.assertIn(" * @brief   The probe.", out)

    def test_no_description_is_refused(self):
        with self.assertRaises(ValueError):
            convert("a.c", ["#include <y.h>"])

    def test_companion(self):
        names = ["kernel/thread/thread.c", "include/emb/thread.h", "kernel/sync/sem.c", "include/emb/sem.h",
                 "a/x.c", "b/x.c", "b/x.h"]
        self.assertEqual(fh.companion("include/emb/thread.h", names), "kernel/thread/thread.c")
        self.assertEqual(fh.companion("b/x.h", names), None)  # two x.c: no guess
        self.assertEqual(fh.companion("kernel/sync/sem.c", names), "include/emb/sem.h")
        names.append("kernel/sync/sem.h")
        self.assertEqual(fh.companion("kernel/sync/sem.c", names), None)


class Check(unittest.TestCase):
    def test_converted_file_passes_and_is_left_alone(self):
        out = convert("kernel/a.c", ["/* The thing. */"])
        self.assertTrue(fh.converted(out))
        self.assertEqual(fh.problems("kernel/a.c", out), [])

    def test_old_header_fails(self):
        self.assertTrue(fh.problems("a.c", HEAD + ["/* The thing. */"]))

    def test_wrong_name_and_missing_tags(self):
        out = convert("kernel/a.c", ["/* The thing. */"])
        self.assertIn("@file names a.c, not b.c", fh.problems("kernel/b.c", out))
        no_date = [l for l in out if "@date" not in l]
        self.assertIn("no @date YYYY-MM-DD (created)", fh.problems("kernel/a.c", no_date))

    def test_generated_file(self):
        good = [fh.SPDX] + fh.NOTICE + ["", "/**", " * @file    hw_config.h", " * @brief   Hardware.", " */"]
        self.assertEqual(fh.generated_problems("boards/x/generated/hw_config.h", good), [])
        self.assertTrue(fh.generated_problems("boards/x/generated/hw_config.h", [fh.SPDX, "/*", " * Generated"]))

    def test_the_generator_writes_the_same_notice(self):
        sys.path.insert(0, str(fh.ROOT / "hw/gen"))
        from emb_hwgen import emit
        self.assertEqual(emit.NOTICE, "\n".join(fh.NOTICE) + "\n")


class Hash(unittest.TestCase):
    HEAD = [fh.HASH_SPDX, "# Copyright 2026 Someone Else"]

    def test_notice_then_a_blank_line(self):
        out = fh.hash_convert(self.HEAD + ['"""The module."""', ""])
        self.assertEqual(out, self.HEAD + fh.HASH_NOTICE + ["", '"""The module."""', ""])
        self.assertEqual(fh.hash_problems("a.py", out), [])

    def test_after_a_shebang(self):
        out = fh.hash_convert(["#!/bin/sh"] + self.HEAD + ["set -eu"])
        self.assertEqual(out[:6], ["#!/bin/sh"] + self.HEAD + fh.HASH_NOTICE)

    def test_a_bare_comment_line_already_separates(self):
        out = fh.hash_convert(self.HEAD + ["#", "# What it does."])
        self.assertEqual(out, self.HEAD + fh.HASH_NOTICE + ["#", "# What it does."])

    def test_converted_once(self):
        out = fh.hash_convert(self.HEAD + ["x: 1"])
        self.assertTrue(fh.hash_converted(out))
        self.assertTrue(fh.hash_problems("a.yaml", self.HEAD + ["x: 1"]))

    def test_the_generator_writes_the_same_notice(self):
        sys.path.insert(0, str(fh.ROOT / "hw/gen"))
        from emb_hwgen import emit
        self.assertEqual(emit.HASH_NOTICE, "\n".join(fh.HASH_NOTICE) + "\n")
        self.assertEqual(fh.hash_generated_problems("g/board.conf", [fh.HASH_SPDX] + fh.HASH_NOTICE + ["#"]), [])


if __name__ == "__main__":
    unittest.main()
