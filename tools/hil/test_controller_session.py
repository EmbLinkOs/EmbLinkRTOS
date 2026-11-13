# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""The controller session's parsing and phase checks, on console text (no board)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import controller_session as cs  # noqa: E402


def tlm(t, mode="normal", samples=0, shed=0, degraded=0, deadline_miss=0, missed=0, temp="24.5"):
    return (f"TLM t={t} mode={mode} rate=100 temp={temp} heater=1 samples={samples} missed={missed} "
            f"deadline_miss={deadline_miss} shed={shed} degraded={degraded} queue_drops=0 adc_overruns=0 "
            f"lat_max_us=9 proc_max_us=12 load=0 rx=4/4 rx_drops=0")


def console(lines):
    """Lines as the console sends them: each ends with a newline."""
    return "".join(line + "\n" for line in lines)


class Parse(unittest.TestCase):
    def test_fields(self):
        f = cs.parse_tlm(tlm(3, samples=300)[4:])
        self.assertEqual(f["t"], 3)
        self.assertEqual(f["samples"], 300)
        self.assertEqual(f["mode"], "normal")
        self.assertEqual(f["rx"], "4/4")
        self.assertEqual(f["temp_mc"], 24500)

    def test_negative_temperature(self):
        self.assertEqual(cs.parse_tlm("temp=-1.5")["temp_mc"], -1500)

    def test_lines_from_console(self):
        text = "CTL ready\n" + tlm(1) + "\n>>> status\nOK status\n" + tlm(2, samples=100) + "\n"
        lines = cs.tlm_lines(text)
        self.assertEqual([line["t"] for line in lines], [1, 2])

    def test_partial_line_ignored(self):
        text = tlm(1) + "\n" + tlm(2)[:60]  # the second line is still arriving
        self.assertEqual([line["t"] for line in cs.tlm_lines(text)], [1])

    def test_cut_temperature(self):
        self.assertNotIn("temp_mc", cs.parse_tlm("t=5 temp= samples=3"))

    def test_boot_facts(self):
        f = cs.boot_facts("CTL boot\nCTL reset watchdog pin\nCTL crash none\n")
        self.assertEqual(f, {"reset": ["watchdog", "pin"], "crash": "none"})
        f = cs.boot_facts("CTL reset software pin\nCTL crash class=10\nEMB_CRASH 454d4243\n")
        self.assertEqual(f["crash"], "record")


class Checks(unittest.TestCase):
    def steady(self, **kw):
        return cs.tlm_lines(console(tlm(t, samples=100 * t, **kw) for t in range(1, 6)))

    def test_steady_holds(self):
        self.assertEqual(cs.check_steady(self.steady(), 100), [])

    def test_wrong_rate(self):
        self.assertIn("not 1000 Hz", cs.check_steady(self.steady(), 1000)[0])

    def test_miss_counted(self):
        lines = self.steady()
        lines[-1]["deadline_miss"] = 2
        self.assertEqual(cs.check_steady(lines, 100), ["deadline_miss rose by 2"])

    def test_too_few_lines(self):
        self.assertEqual(cs.check_steady([], 100), ["fewer than two TLM lines"])

    def test_degraded(self):
        lines = cs.tlm_lines(console([tlm(1, samples=100), tlm(2, "degraded", 150, 50, 1, 3),
                                        tlm(3, "degraded", 200, 100, 1, 3)]))
        self.assertEqual(cs.check_degraded(lines), [])

    def test_never_degraded(self):
        problems = cs.check_degraded(self.steady())
        self.assertIn("never entered degraded mode", problems)
        self.assertIn("no sample shed", problems)

    def test_rate_counts_shed(self):
        lines = cs.tlm_lines(console([tlm(1, "degraded", 100, 100), tlm(2, "degraded", 150, 150)]))
        self.assertEqual(cs.rate_of(lines), 100.0)


if __name__ == "__main__":
    unittest.main()
