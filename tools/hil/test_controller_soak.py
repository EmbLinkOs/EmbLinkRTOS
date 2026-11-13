# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The long soak's bookkeeping, on synthetic TLM lines (no board)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import controller_session as cs  # noqa: E402
import controller_soak as soak  # noqa: E402


def line(t, samples, deadline_miss=0, mode="normal"):
    return cs.parse_tlm(f"t={t} mode={mode} rate=100 temp=25.0 heater=1 samples={samples} missed=0 "
                        f"deadline_miss={deadline_miss} shed=0 degraded=0 queue_drops=0 adc_overruns=0 "
                        f"lat_max_us=9 proc_max_us=3010 load=30 rx=4/4 rx_drops=0")


class Bookkeeping(unittest.TestCase):
    def test_clean(self):
        s = soak.Soak(100)
        for i in range(10):
            s.tlm(1000.0 + i, line(100 + i, 10_000 + 100 * i))
        r = s.summary()
        self.assertEqual(r["problems"], [])
        self.assertEqual((r["board_seconds"], r["samples_processed"], r["rate_hz"]), (9, 900, 100.0))

    def test_host_gap_is_not_a_failure(self):
        s = soak.Soak(100)
        s.tlm(1000.0, line(100, 10_000))
        s.tlm(1060.0, line(160, 16_000))  # the host slept for a minute; the board went on
        r = s.summary()
        self.assertEqual(r["problems"], [])
        self.assertEqual(r["board_gaps"], [{"from_t": 100, "to_t": 160}])
        self.assertEqual(r["host_gaps"][0]["seconds"], 60.0)

    def test_reset_and_misses_fail(self):
        s = soak.Soak(100)
        s.tlm(1000.0, line(100, 10_000))
        s.tlm(1001.0, line(101, 10_100, deadline_miss=2))
        s.boot(1002.0, "CTL boot: ...")
        s.tlm(1003.0, line(1, 100, deadline_miss=0))
        problems = s.summary()["problems"]
        self.assertTrue(any("reset" in p for p in problems))

    def test_empty(self):
        self.assertEqual(soak.Soak(100).summary()["problems"], ["no TLM line"])


if __name__ == "__main__":
    unittest.main()
