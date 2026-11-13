# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""tools/logdec: the record layout, printf's subset, the stream's own records and the frames
around them (A127). The images' own streams are checked end to end by backends_agree.py."""
import io
import struct
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "transport"))
import frame  # noqa: E402
import logdec  # noqa: E402
from logdec import T_INT, T_UINT, T_LONG, T_LLONG, T_ULLONG, T_DOUBLE, T_STR, T_PTR, T_HEX  # noqa: E402


def sig(*types):
    return sum(t << (4 * i) for i, t in enumerate(types))


class FakeDb:
    """A 32-bit little-endian image's layout, with call sites by identifier."""

    def __init__(self, metas, build_id=b"\x11" * 20):
        self.id_bytes, self.timestamps = 2, True
        self.long_size, self.ptr_size, self.double_size, self.str_max = 4, 4, 8, 32
        self.end, self.build_id, self.metas = "<", build_id, metas

    def meta(self, rid):
        return self.metas[rid]


def record(rid, ts, *fields):
    out = struct.pack("<HI", rid, ts)
    for f in fields:
        out += f
    return out


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


class Format(unittest.TestCase):
    def f(self, fmt, *args):
        return logdec.format_c(fmt, list(args))

    def test_integers(self):
        self.assertEqual(self.f("a %d b %u", (T_INT, -5, 32), (T_UINT, 7, 32)), "a -5 b 7")
        self.assertEqual(self.f("[%5d|%-5d|%05d|%+d]", *[(T_INT, 42, 32)] * 4), "[   42|42   |00042|+42]")
        self.assertEqual(self.f("%x %X %#x %o", *[(T_UINT, 255, 32)] * 3, (T_UINT, 8, 32)), "ff FF 0xff 10")
        self.assertEqual(self.f("%u", (T_INT, -1, 32)), "4294967295")  # %u of an int, as C
        self.assertEqual(self.f("%lld %llu", (T_LLONG, -1, 64), (T_ULLONG, 2**64 - 1, 64)),
                         "-1 18446744073709551615")
        self.assertEqual(self.f("%ld", (T_LONG, -70000, 32)), "-70000")

    def test_narrowing(self):
        self.assertEqual(self.f("%hhx %hd", (T_INT, 0x1FF, 32), (T_INT, 70000, 32)), "ff 4464")

    def test_strings_chars_pointers(self):
        self.assertEqual(self.f("<%.3s|%4s|%-3s>", (T_STR, "abcdef", 0), (T_STR, "z", 0), (T_STR, "y", 0)),
                         "<abc|   z|y  >")
        self.assertEqual(self.f("%c%%", (T_INT, ord("z"), 32)), "z%")
        self.assertEqual(self.f("%p", (T_PTR, 0x20001000, 32)), "0x20001000")

    def test_doubles_as_the_target_prints_them(self):
        self.assertEqual(self.f("%.2f %.0f %.0f %f", (T_DOUBLE, 3.14159, 64), (T_DOUBLE, 2.5, 64),
                                (T_DOUBLE, 3.5, 64), (T_DOUBLE, -0.5, 64)), "3.14 2 4 -0.500000")
        self.assertEqual(self.f("%e", (T_DOUBLE, 1.5, 64)), "1.500000")  # %e prints as %f (A127)

    def test_missing_arguments_do_not_raise(self):
        self.assertEqual(self.f("%d %s", (T_INT, 1, 32)), "1 0")


class Records(unittest.TestCase):
    def setUp(self):
        self.db = FakeDb({
            8: logdec.Meta("a %d b %u c %s", "main.c", "app", 10, sig(T_INT, T_UINT, T_STR), 3),
            40: logdec.Meta("rx", "main.c", "app", 11, T_HEX, 4),
            72: logdec.Meta("%f %p", "main.c", "net", 12, sig(T_DOUBLE, T_PTR), 1),
        })

    def test_a_frame_of_records(self):
        payload = (record(2, 0, b"\x11" * 20, u32(1_000_000))  # start: 1 ms ticks
                   + record(8, 1234, u32(-5), u32(7), b"\x03xyz")
                   + record(40, 1235, struct.pack("<H", 2), b"\xde\xad")
                   + record(72, 1236, struct.pack("<d", 0.25), u32(0x2000))
                   + record(1, 1237, struct.pack("<H", 3)))
        s = logdec.Stream(self.db)
        self.assertEqual(s.records(payload), [
            "[1.234] I app: a -5 b 7 c xyz",
            "[1.235] D app: rx: de ad",
            "[1.236] E net: 0.250000 0x2000",
            "[1.237] W emb_log: 3 records dropped",
        ])
        self.assertTrue(s.started)

    def test_where(self):
        s = logdec.Stream(self.db, where=True, tick_ns=1_000_000)
        self.assertEqual(s.records(record(8, 0, u32(1), u32(2), b"\x00")),
                         ["[0.000] I app: a 1 b 2 c   (main.c:10)"])

    def test_another_images_stream_is_refused(self):
        with self.assertRaises(logdec.LogError) as e:
            logdec.Stream(self.db).records(record(2, 0, b"\x22" * 20, u32(1_000_000)))
        self.assertIn("build", str(e.exception))

    def test_a_cut_record_is_an_error(self):
        with self.assertRaises(logdec.LogError):
            logdec.Stream(self.db).records(record(8, 0, u32(1))[:-2])

    def test_framed_console(self):
        data = (b"boot ROM says hi\n\x00"
                + frame.encode(0, record(2, 0, b"\x11" * 20, u32(1_000_000)) + record(8, 5, u32(1), u32(2), b"\x01q"))
                + b"\x00" + frame.encode(3, b"logging: done\n"))
        out = io.StringIO()
        stats = logdec.decode_stream(self.db, data, out)
        self.assertEqual(out.getvalue(), "boot ROM says hi\n[0.005] I app: a 1 b 2 c q\nlogging: done\n")
        self.assertEqual((stats["frames"], stats["errors"], stats["started"]), (2, 0, True))


class FeedAll(unittest.TestCase):
    def test_text_and_frames_and_damage(self):
        good = frame.encode(3, b"hi")
        bad = bytearray(frame.encode(0, b"\x05\x06\x07"))
        bad[2] ^= 0xFF
        d = frame.Decoder()
        items = d.feed_all(b"plain text\x00" + good + b"\x00" + bytes(bad) + b"tail")
        items += d.finish()
        self.assertEqual(items, [("raw", b"plain text", None), ("frame", 3, b"hi"), ("raw", b"tail", None)])
        self.assertEqual((d.frames, d.errors), (1, 1))


if __name__ == "__main__":
    unittest.main()
