# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The host's framing (SPEC-015 §7, OBS-019): the vectors tests/conformance/test_frame.c checks
the target's encoder against, round trips, and the stream decoder's resynchronization."""
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frame  # noqa: E402

# the same bytes as test_frame.c's v_empty, v_zero and v_hello
VECTORS = [
    (0, b"", bytes([0x01, 0x03, 0xF0, 0xE1, 0x00])),
    (1, b"\0", bytes([0x02, 0x01, 0x03, 0x3E, 0x2E, 0x00])),
    (2, b"hello", bytes([0x09, 0x02, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x32, 0x46, 0x00])),
]


class Frame(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(frame.crc16(b"123456789"), 0x29B1)  # CRC-16/IBM-3740's check

    def test_vectors(self):
        for ch, payload, wire in VECTORS:
            self.assertEqual(frame.encode(ch, payload), wire)
            self.assertEqual(frame.decode(wire), (ch, payload))

    def test_cobs_boundary(self):
        wire = frame.encode(7, bytes([0x11]) * 300)  # test_frame.c's frame_runs_at_the_cobs_boundary
        self.assertEqual((len(wire), wire[0], wire[255], wire[-3], wire[-2]), (306, 0xFF, 50, 0x0B, 0xC9))
        wire = frame.encode(5, bytes([0x22]) * 253)
        self.assertEqual((len(wire), wire[0], wire[255]), (259, 0xFF, 0x03))
        for n in range(0, 800, 7):
            wire = frame.encode(3, bytes(n))
            self.assertEqual(wire.count(0), 1)  # only the delimiter
            self.assertLessEqual(len(wire), n + 3 + (n + 3) // 254 + 2)  # EMB_FRAME_ENCODED_MAX
            self.assertEqual(frame.decode(wire), (3, bytes(n)))

    def test_refusals(self):
        with self.assertRaises(frame.FrameError):
            frame.encode(16, b"")
        bad = bytearray(frame.encode(0, b"abc"))
        bad[2] ^= 1
        with self.assertRaisesRegex(frame.FrameError, "CRC"):
            frame.decode(bytes(bad))
        with self.assertRaisesRegex(frame.FrameError, "cut short"):
            frame.decode(frame.encode(0, b"abc")[:-3])
        with self.assertRaisesRegex(frame.FrameError, "channel 16"):
            frame.decode(bytes([0x04, 0x10, 0xC1, 0xF3, 0x00]))

    def test_stream_resynchronizes(self):
        rnd = random.Random(1)
        dec = frame.Decoder()
        got = dec.feed(b"\x47\x13\x99\x05\x00")  # joined mid-frame, then a delimiter
        sent = []
        for _ in range(500):
            ch, payload = rnd.randrange(16), bytes(rnd.randrange(256) for _ in range(rnd.randrange(80)))
            wire = bytearray(frame.encode(ch, payload))
            if rnd.randrange(8) == 0:
                at = rnd.randrange(len(wire) - 1)
                wire[at] ^= rnd.randrange(1, 256)
            else:
                sent.append((ch, payload))
            if rnd.randrange(4) == 0:
                wire = b"\0" + wire  # a sender's leading zero
            got += dec.feed(bytes(wire[:5])) + dec.feed(bytes(wire[5:]))  # split across reads
        self.assertEqual(got, sent)  # every undamaged frame, and no damaged one
        self.assertEqual(dec.frames, len(sent))
        self.assertGreater(dec.errors, 0)

    def test_overrun(self):
        dec = frame.Decoder(max_payload=8)
        self.assertEqual(dec.feed(frame.encode(6, bytes([0x33]) * 20)), [])
        self.assertEqual(dec.overruns, 1)
        self.assertEqual(dec.feed(frame.encode(2, b"hello")), [(2, b"hello")])

    def test_tool_reads_a_capture(self):
        import io
        import tempfile
        from contextlib import redirect_stdout, redirect_stderr
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(b"\x99\x98\x00" + frame.encode(0, b"boot ok\n") + frame.encode(5, b"\x01\x02"))
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            self.assertEqual(frame.main([f.name]), 0)
        Path(f.name).unlink()
        self.assertEqual(out.getvalue().splitlines(), ["[log] boot ok", "[5] \x01\x02"])
        self.assertIn("2 frames, 1 errors, 0 overruns", err.getvalue())  # the noise counts once


if __name__ == "__main__":
    unittest.main()
