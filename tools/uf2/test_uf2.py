# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""UF2 writer tests (A79): block layout, page filling across segments, the flash bounds, and
refusals."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import uf2  # noqa: E402


def elf(segments):
    """A minimal ELF32 with one PT_LOAD per (load address, bytes)."""
    phoff, n = 52, len(segments)
    data_off = phoff + 32 * n
    hdr = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    hdr += struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, 0, phoff, 0, 0, 52, 32, n, 40, 0, 0)
    ph, body = b"", b""
    for addr, data in segments:
        ph += struct.pack("<IIIIIIII", 1, data_off + len(body), addr, addr, len(data), len(data), 5, 4)
        body += data
    return hdr + ph + body


def blocks(img):
    return [img[i:i + 512] for i in range(0, len(img), 512)]


VECTORS = b"\x40\x1f\x08\x20" + b"\xf5\x20\x00\x10" + bytes(0x140 - 8)
BLOCK = struct.pack("<5I", 0xFFFFDED3, 0x10210142, 0x000001FF, 0, 0xAB123579)


class Uf2(unittest.TestCase):
    def test_rp2350_needs_its_image_def(self):
        good = uf2.segments(elf([(0x10000000, VECTORS + BLOCK)]))
        self.assertEqual(uf2.image_def(good), (0x140, 0x1021))
        self.assertIsNone(uf2.image_def(uf2.segments(elf([(0x10000000, VECTORS)]))))
        broken = BLOCK[:-4] + b"\0\0\0\0"  # no end marker
        self.assertIsNone(uf2.image_def(uf2.segments(elf([(0x10000000, VECTORS + broken)]))))
        late = uf2.segments(elf([(0x10000000, VECTORS + bytes(4096) + BLOCK)]))
        self.assertIsNone(uf2.image_def(late))  # past the first 4 KiB
        d = Path(tempfile.mkdtemp())
        (d / "a.elf").write_bytes(elf([(0x10000000, VECTORS)]))
        self.assertEqual(uf2.main([str(d / "a.elf"), "-o", str(d / "a.uf2"), "--family", "0xE48BFF59"]), 2)
        self.assertFalse((d / "a.uf2").exists())
        self.assertEqual(uf2.main([str(d / "a.elf"), "-o", str(d / "a.uf2"), "--family", "0x12345678"]), 0)

    def test_blocks(self):
        img = uf2.uf2(uf2.segments(elf([(0x10000000, bytes(range(256)) * 2), (0x10000300, b"\xaa" * 4)])), 0xE48BFF59)
        bs = blocks(img)
        self.assertEqual(len(bs), 3)
        for i, b in enumerate(bs):
            m0, m1, flags, addr, size, no, count, fam = struct.unpack_from("<IIIIIIII", b)
            self.assertEqual((m0, m1, flags, size, no, count, fam),
                             (0x0A324655, 0x9E5D5157, 0x2000, 256, i, 3, 0xE48BFF59))
            self.assertEqual(struct.unpack_from("<I", b, 508)[0], 0x0AB16F30)
            self.assertEqual(addr, [0x10000000, 0x10000100, 0x10000300][i])
        self.assertEqual(bs[0][32:288], bytes(range(256)))
        self.assertEqual(bs[2][32:36], b"\xaa" * 4)
        self.assertEqual(bs[2][36:288], bytes(252))  # the page's rest is zero

    def test_two_segments_share_a_page(self):
        img = uf2.uf2(uf2.segments(elf([(0x10000000, b"\x01" * 8), (0x10000010, b"\x02" * 8)])), 1)
        (b,) = blocks(img)
        self.assertEqual(b[32:56], b"\x01" * 8 + bytes(8) + b"\x02" * 8)

    def test_cli_bounds_and_refusals(self):
        d = Path(tempfile.mkdtemp())
        (d / "a.elf").write_bytes(elf([(0x20000000, b"\x00" * 4)]))
        self.assertEqual(uf2.main([str(d / "a.elf"), "-o", str(d / "a.uf2"), "--family", "0xE48BFF59",
                                   "--base", "0x10000000", "--size", "0x400000"]), 2)
        (d / "b.elf").write_bytes(b"not an elf")
        self.assertEqual(uf2.main([str(d / "b.elf"), "-o", str(d / "b.uf2"), "--family", "1"]), 2)
        (d / "c.elf").write_bytes(elf([(0x10000000, b"\x00" * 4)]))
        self.assertEqual(uf2.main([str(d / "c.elf"), "-o", str(d / "c.uf2"), "--family", "1",
                                   "--base", "0x10000000", "--size", "0x400000"]), 0)
        self.assertEqual(len((d / "c.uf2").read_bytes()), 512)


if __name__ == "__main__":
    unittest.main()
