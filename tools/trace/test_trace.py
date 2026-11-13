# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""tools/trace: the trace ring's packets, the counter's wraps, and the CTF and Perfetto outputs
(A128). Babeltrace 2 reads a trace this tool wrote in CI's application job."""
import json
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "transport"))
import frame  # noqa: E402
import trace  # noqa: E402


def events_packet(first, events, hz=180_000_000, overwritten=0):
    body = b"".join(trace.RECORD.pack(*e) for e in events)
    return trace.HEADER.pack(1, trace.KIND_EVENTS, len(events), first, hz, overwritten) + body


def names_packet(names):
    body = b"".join(bytes([i, len(n)]) + n.encode() for i, n in names)
    return trace.HEADER.pack(1, trace.KIND_NAMES, len(names), 0, 180_000_000, 0) + body


class Catalogue(unittest.TestCase):
    def test_reads_the_enum(self):
        cat = trace.catalogue()
        self.assertEqual(cat[1][0], "switch")
        self.assertIn("from thread index", cat[1][1])
        self.assertEqual(cat[18][0], "sem_take")


class Packets(unittest.TestCase):
    def test_capture_and_wraps(self):
        evs = [(0xFFFF_FF00, 1, 0, 0, 1, 2, 0), (0x0000_0100, 18, 0, 0, 2, 0x2000, 0)]  # wraps
        data = (b"\x00" + frame.encode(3, b"boot text\n") + b"\x00"
                + frame.encode(1, names_packet([(1, "producer"), (2, "consumer")])) + b"\x00"
                + frame.encode(1, events_packet(10, evs)) + b"\x00"
                + frame.encode(1, events_packet(14, [(0x200, 19, 0, 1, 255, 0x2000, 1)])))
        tr, dec = trace.read_capture(data)
        self.assertEqual(len(tr.events), 3)
        self.assertEqual(tr.events[1].cycles - tr.events[0].cycles, 0x200)  # across the wrap
        self.assertEqual(tr.names, {1: "producer", 2: "consumer"})
        self.assertEqual(tr.gaps, 1)  # 12 and 13 are missing
        self.assertEqual(dec.errors, 0)

    def test_a_bad_packet_is_refused(self):
        with self.assertRaises(trace.TraceError):
            trace.Trace().packet(trace.HEADER.pack(2, 0, 0, 0, 1, 0))
        with self.assertRaises(trace.TraceError):
            trace.Trace().packet(trace.HEADER.pack(1, 0, 2, 0, 1, 0) + b"\x00" * 20)


class Outputs(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="trace-"))
        self.addCleanup(shutil.rmtree, self.d)
        tr = trace.Trace()
        tr.packet(names_packet([(1, "a"), (2, "b")]))
        tr.packet(events_packet(0, [(100, 1, 0, 0, 1, 2, 0), (280, 10, 0, 1, 15, 0, 0),
                                    (460, 11, 0, 1, 15, 0, 0), (640, 1, 0, 0, 2, 1, 0),
                                    (900, 99, 0, 0, 0, 0, 0)], hz=1_000_000))
        self.tr = tr
        self.cat = trace.catalogue()

    def test_ctf_layout(self):
        out = trace.ctf(self.tr, self.cat, self.d / "ctf")
        meta = (out / "metadata").read_text()
        self.assertTrue(meta.startswith("/* CTF 1.8 */"))
        self.assertIn("freq = 1000000;", meta)
        self.assertIn('name = "switch";', meta)
        self.assertIn('name = "event_99";', meta)  # an id the catalogue does not name
        stream = (out / "stream_0").read_bytes()
        magic, _, sid = struct.unpack_from("<I16sI", stream)
        self.assertEqual((magic, sid), (trace.CTF_MAGIC, 0))
        begin, end, content, size, discarded, cpu = struct.unpack_from("<QQQQQB", stream, 24)
        self.assertEqual((begin, end, discarded), (100, 900, 0))
        self.assertEqual(content, len(stream) * 8)
        self.assertEqual(len(stream), 24 + 41 + 5 * 23)

    def test_perfetto_tracks(self):
        doc = trace.perfetto(self.tr, self.cat)
        evs = doc["traceEvents"]
        begins = [e for e in evs if e["ph"] == "B"]
        self.assertEqual([e["tid"] for e in begins], [2, 1000, 1])  # b runs, an IRQ, then a
        self.assertEqual(begins[0]["name"], "b")
        self.assertTrue(any(e["ph"] == "i" and e["name"] == "event_99" for e in evs))
        json.dumps(doc)


if __name__ == "__main__":
    unittest.main()
