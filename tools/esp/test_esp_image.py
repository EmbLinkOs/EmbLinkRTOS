# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""esp_image.py: the application image format the ESP32-C3's ROM checks (ESP-IDF's
esp_app_format.h): the header's fields, each segment, the checksum at the end of a 16-byte
block and the SHA-256 after it."""
import hashlib
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import esp_image  # noqa: E402


def elf(entry, segments):
    """A minimal ELF32 with one PT_LOAD per (address, data, memsz) segment"""
    phoff = 52
    data_off = phoff + 32 * len(segments)
    hdr = bytearray(b"\x7fELF\x01\x01\x01" + b"\0" * 9)
    hdr += struct.pack("<HHIIIIIHHHHHH", 2, 243, 1, entry, phoff, 0, 0, 52, 32, len(segments), 40, 0, 0)
    ph = bytearray()
    body = bytearray()
    for addr, data, memsz in segments:
        ph += struct.pack("<IIIIIIII", 1, data_off + len(body), addr, addr, len(data), memsz, 5, 4)
        body += data
    return bytes(hdr + ph + body)


class ImageTest(unittest.TestCase):
    def test_layout(self):
        code = bytes(range(10))  # padded to 12
        data = b"\xAA\xBB\xCC\xDD"
        f = elf(0x40380104, [(0x40380000, code, 10), (0x3FCA0000, data, 4), (0x3FCA0100, b"", 64)])
        segs, entry = esp_image.load_segments(f)
        self.assertEqual(entry, 0x40380104)
        self.assertEqual([a for a, _ in segs], [0x3FCA0000, 0x40380000])  # the empty .bss is left out
        img = esp_image.image(segs, entry)
        magic, count, mode, speed_size, ent = struct.unpack_from("<BBBBI", img, 0)
        self.assertEqual((magic, count, mode, speed_size, ent), (0xE9, 2, 2, 0x2F, 0x40380104))
        wp, _drv, chip, minrev, minfull, maxfull, _res, hashed = struct.unpack_from("<B3sHBHH4sB", img, 8)
        self.assertEqual((wp, chip, minrev, minfull, maxfull, hashed), (0xEE, 5, 0, 0, 199, 1))
        off = 24
        expect = [(0x3FCA0000, data), (0x40380000, code + b"\0\0")]
        checksum = 0xEF
        for addr, body in expect:
            self.assertEqual(struct.unpack_from("<II", img, off), (addr, len(body)))
            self.assertEqual(img[off + 8:off + 8 + len(body)], body)
            for b in body:
                checksum ^= b
            off += 8 + len(body)
        end = len(img) - 32
        self.assertEqual(end % 16, 0)
        self.assertTrue(all(b == 0 for b in img[off:end - 1]))
        self.assertEqual(img[end - 1], checksum)
        self.assertEqual(img[end:], hashlib.sha256(img[:end]).digest())

    def test_refuses_what_is_not_an_elf(self):
        with self.assertRaises(ValueError):
            esp_image.load_segments(b"\0" * 64)


if __name__ == "__main__":
    unittest.main()
