# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""pcsample.py's folding: samples to functions, asleep samples apart."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pcsample  # noqa: E402


class Fold(unittest.TestCase):
    SYMS = [(0x1000, 0x20, "a"), (0x1020, 0x10, "b"), (0x2000, 0, "c")]

    def test_samples_land_in_their_functions(self):
        counts, asleep = pcsample.fold([0x1000, 0x101F, 0x1021, 0x1025, 0x2000, 0xFFFFFFFF], self.SYMS)
        self.assertEqual(counts, {"a": 2, "b": 2, "c": 1})  # Thumb bit ignored; size 0 covers 2 bytes
        self.assertEqual(asleep, 1)

    def test_outside_every_function_is_unknown(self):
        counts, _ = pcsample.fold([0x1030, 0x0800], self.SYMS)
        self.assertEqual(counts, {"?": 2})


if __name__ == "__main__":
    unittest.main()
