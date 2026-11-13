# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""check.py's tests: a log it accepts, one it refuses, and data that passes and fails."""
import hashlib
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check  # noqa: E402


def log_of(data, digest=None):
    lines = [f"ENTROPY {data[i:i + 32].hex()}" for i in range(0, len(data), 32)]
    lines.append(f"ENTROPY sample {len(data)} bytes sha256 {digest or hashlib.sha256(data).hexdigest()}")
    return "TEST entropy\n" + "\n".join(lines) + "\nCASE entropy outcome=passed\n"


class Sample(unittest.TestCase):
    def test_a_log_round_trips(self):
        data = random.Random(1).randbytes(4096)
        self.assertEqual(check.sample_from(log_of(data)), data)

    def test_a_changed_line_is_found(self):
        data = random.Random(2).randbytes(4096)
        log = log_of(data).replace(data[:32].hex(), (b"\x00" * 32).hex(), 1)
        with self.assertRaisesRegex(check.SampleError, "SHA-256 differs"):
            check.sample_from(log)

    def test_not_a_sample_log(self):
        with self.assertRaisesRegex(check.SampleError, "no 'ENTROPY sample"):
            check.sample_from("TEST other\nCASE other outcome=passed\n")


class RawSource(unittest.TestCase):
    def test_the_estimate_from_the_counts(self):
        h = check.raw_min_entropy("ENTROPY raw 262144 bytes, most common value 1105 times, chi-square x100 1\n")
        self.assertAlmostEqual(h, 7.75, places=1)  # p = 0.0042, its bound 0.0045

    def test_a_skewed_source_is_low(self):
        h = check.raw_min_entropy("ENTROPY raw 262144 bytes, most common value 65536 times, chi-square x100 1\n")
        self.assertLess(h, check.MIN_ENTROPY)  # one value in four

    def test_no_raw_line(self):
        self.assertIsNone(check.raw_min_entropy("TEST other\n"))


class Checks(unittest.TestCase):
    def test_random_bytes_pass(self):
        data = random.Random(3).randbytes(65536)
        self.assertTrue(all(ok for _, _, ok in check.checks(data)), check.checks(data))

    def test_a_biased_source_fails(self):
        rng = random.Random(4)
        data = bytes(rng.getrandbits(8) | 0x01 for _ in range(65536))  # one bit stuck
        failed = [name for name, _, ok in check.checks(data) if not ok]
        self.assertIn("monobit: ones of the bits", failed)

    def test_a_repeating_source_fails(self):
        data = random.Random(5).randbytes(256) * 256
        failed = [name for name, _, ok in check.checks(data) if not ok]
        self.assertIn("zlib's compression", failed)


if __name__ == "__main__":
    unittest.main()
