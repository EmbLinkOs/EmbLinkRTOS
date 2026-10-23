#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Turn a linked ELF into an ESP32-C3 flash image the boot ROM loads (the RISC-V port's
ESP32-C3 boards; docs/ports/riscv.md).

The image is Espressif's application image format (ESP-IDF's esp_app_format.h, the
format esptool's elf2image writes): a 24-byte header (magic 0xE9, the segment count, the
flash mode, frequency and size the ROM sets up, the entry address, the chip ID 5 for the
ESP32-C3 and the revisions it accepts), then each PT_LOAD segment with file contents as
its load address, its length and its bytes, padded to 4; then zeros up to the last byte
of a 16-byte block, which holds the checksum (0xEF XOR every segment byte); then the
SHA-256 of all of it. The ROM checks both and jumps to the entry. Written at flash
offset 0, it is what the ROM boots, with no second-stage bootloader: everything runs from
internal SRAM.

    esp_image.py IMAGE.elf -o IMAGE.bin
"""
import argparse
import hashlib
import struct
import sys

MAGIC = 0xE9
CHIP_ESP32C3 = 5
SPI_MODE_DIO = 2
SPI_SPEED_DIV_1 = 0xF  # 80 MHz
FLASH_SIZE_4MB = 2
MAX_CHIP_REV_FULL = 199  # v1.99: every ESP32-C3 so far
CHECKSUM_SEED = 0xEF
PT_LOAD = 1


def load_segments(elf: bytes):
    """[(address, bytes)] of the ELF's PT_LOAD segments with file contents, and its entry"""
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise ValueError("not a 32-bit little-endian ELF")
    entry, phoff = struct.unpack_from("<II", elf, 24)
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    segs = []
    for i in range(phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from("<IIIII", elf, phoff + i * phentsize)
        if p_type == PT_LOAD and p_filesz:
            segs.append((p_paddr, elf[p_offset:p_offset + p_filesz]))
    if not segs:
        raise ValueError("no loadable segment")
    if len(segs) > 16:
        raise ValueError(f"{len(segs)} segments, more than the format's 16")
    return sorted(segs), entry


def image(segs, entry: int) -> bytes:
    out = bytearray(struct.pack("<BBBBI", MAGIC, len(segs), SPI_MODE_DIO,
                                (FLASH_SIZE_4MB << 4) | SPI_SPEED_DIV_1, entry))
    # wp_pin, spi_pin_drv[3], chip_id, min_chip_rev, min_chip_rev_full, max_chip_rev_full,
    # reserved[4], hash_appended
    out += struct.pack("<B3sHBHH4sB", 0xEE, b"\0\0\0", CHIP_ESP32C3, 0, 0, MAX_CHIP_REV_FULL, b"\0" * 4, 1)
    checksum = CHECKSUM_SEED
    for addr, data in segs:
        data = data + b"\0" * (-len(data) % 4)
        out += struct.pack("<II", addr, len(data)) + data
        for b in data:
            checksum ^= b
    out += b"\0" * (15 - len(out) % 16)
    out.append(checksum)
    out += hashlib.sha256(out).digest()
    return bytes(out)


def main(argv) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("elf")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args(argv)
    with open(args.elf, "rb") as f:
        segs, entry = load_segments(f.read())
    with open(args.output, "wb") as f:
        f.write(image(segs, entry))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
