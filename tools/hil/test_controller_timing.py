# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The timing tool's capture parsing and per-sample matching, on synthetic VCD (no analyzer)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import controller_timing as ct  # noqa: E402

HEAD = """$timescale 100 ps $end
$scope module libsigrok $end
$var wire 1 ! D1 $end
$var wire 1 " D2 $end
$var wire 1 # D3 $end
$var wire 1 $ D4 $end
$upscope $end
$enddefinitions $end
#0 0! 0" 0# 0$
"""


def vcd(events):
    """events: (time in ns, symbol, level), in order."""
    return HEAD + "".join(f"#{int(t * 10)} {v}{sym}\n" for t, sym, v in events)


def sample(t0, conv=22000, handler=4000, wake=5000, proc=2000, level=1):
    """One sample: the trigger toggles to level, then the handler, then the control thread."""
    i = t0 + conv
    c = i + handler + wake
    return [(t0, "!", level), (i, '"', 1), (i + handler, '"', 0), (c, "#", 1), (c + proc, "#", 0)]


class Parse(unittest.TestCase):
    def test_timescale_and_edges(self):
        edges, init = ct.load_vcd(vcd([(1000, "!", 1), (2500, "!", 0)]))
        self.assertEqual(init["D1"], 0)
        self.assertEqual(edges["D1"], [(1000.0, 1), (2500.0, 0)])

    def test_rate(self):
        self.assertEqual(ct.rate_hz("12m"), 12e6)
        self.assertEqual(ct.rate_hz("500k"), 5e5)
        with self.assertRaises(ValueError):
            ct.rate_hz("fast")


class Measure(unittest.TestCase):
    def test_chain(self):
        ev = sample(10_000) + sample(10_010_000, level=0)
        samples, trig, _ = ct.measure(*ct.load_vcd(vcd(ev)))
        self.assertEqual(len(samples), 2)
        s = samples[0]
        self.assertEqual(s["conversion"], 22000)
        self.assertEqual(s["handler"], 4000)
        self.assertEqual(s["wake"], 5000)
        self.assertEqual(s["latency"], 9000)
        self.assertEqual(s["processing"], 2000)
        self.assertEqual(trig[1] - trig[0], 10_000_000)

    def test_shed_sample_has_no_control(self):
        ev = sample(10_000)
        ev += [(10_010_000, "!", 0), (10_032_000, '"', 1), (10_036_000, '"', 0)]  # no control
        samples, _, _ = ct.measure(*ct.load_vcd(vcd(ev)))
        self.assertIn("latency", samples[0])
        self.assertNotIn("latency", samples[1])
        summary = ct.summarize(*ct.measure(*ct.load_vcd(vcd(ev))))
        self.assertEqual((summary["processed"], summary["unprocessed"]), (1, 1))

    def test_console_flag(self):
        ev = [(5_000, "$", 1)] + sample(10_000) + [(100_000, "$", 0)] + sample(10_010_000, level=0)
        samples, _, _ = ct.measure(*ct.load_vcd(vcd(sorted(ev))))
        self.assertEqual([s["console"] for s in samples], [1, 0])

    def test_stats(self):
        st = ct.stats([3, 1, 2, None])
        self.assertEqual((st["n"], st["min"], st["median"], st["max"]), (3, 1, 2, 3))
        self.assertIsNone(ct.stats([]))


class Images(unittest.TestCase):
    def test_svgs_render(self):
        edges, init = ct.load_vcd(vcd(sample(10_000) + sample(10_010_000, level=0)))
        samples, _, _ = ct.measure(edges, init)
        chain = ct.svg_chain(edges, init, samples[0], "t")
        self.assertTrue(chain.startswith("<svg") and "latency 9.00" in chain)
        hist = ct.svg_histograms([("normal", samples)], "t", "latency", 83.3)
        self.assertIn("n=2", hist)


if __name__ == "__main__":
    unittest.main()
