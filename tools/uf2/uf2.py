#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Write an ELF's loadable bytes as a UF2 file (Microsoft's USB flashing format, A79), the image
a boot ROM's USB drive takes, as the RP2350's does.

    uf2.py IMAGE.elf -o IMAGE.uf2 --family 0xE48BFF59 [--base 0x10000000 --size 0x400000]

Each PT_LOAD segment's file bytes, at its load address (p_paddr), go in 256-byte payloads of
512-byte blocks, as pico-sdk's picotool and EmbCC's `embpack --format uf2` write them: flags
0x2000 (the family ID is present), the family ID, and gaps padded to whole 256-byte pages.
--base and --size refuse a segment outside the flash. For the RP2350's families the image must
hold a valid IMAGE_DEF block in its first 4 KiB, or the boot ROM would not start it: the file is
refused without one. Exit status 0, or 2 on an error.
"""
import argparse
import struct
import sys
from pathlib import Path

MAGIC0, MAGIC1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_FAMILY = 0x00002000
PAYLOAD = 256
RP2350_FAMILIES = {0xE48BFF59: "rp2350-arm-s", 0xE48BFF5A: "rp2350-riscv", 0xE48BFF5B: "rp2350-arm-ns"}
BLOCK_START, BLOCK_END = 0xFFFFDED3, 0xAB123579


class Uf2Error(Exception):
    pass


def segments(elf):
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise Uf2Error("not a little-endian ELF32 file")
    phoff, = struct.unpack_from("<I", elf, 28)
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    out = []
    for i in range(phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from("<IIIII", elf, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            out.append((p_paddr, elf[p_offset:p_offset + p_filesz]))
    if not out:
        raise Uf2Error("no loadable bytes")
    return sorted(out)


def pages(segs):
    """{page address: 256 bytes}: the segments' bytes, gaps inside a page filled with zeros."""
    mem = {}
    for addr, data in segs:
        for i, b in enumerate(data):
            a = addr + i
            page = mem.setdefault(a & ~(PAYLOAD - 1), bytearray(PAYLOAD))
            page[a & (PAYLOAD - 1)] = b
    return mem


def image_def(segs):
    """The first IMAGE_DEF block in the image's first 4 KiB, as the RP2350 boot ROM finds it
    (datasheet §5.9): (offset, image type), or None."""
    base, first = segs[0]
    for off in range(0, min(len(first), 4096) - 3, 4):
        if struct.unpack_from("<I", first, off)[0] != BLOCK_START:
            continue
        i = off + 4
        image_type = None
        while i + 4 <= len(first):
            item = first[i]
            size = struct.unpack_from("<H", first, i + 1)[0] if item & 0x7F == 0x7F else first[i + 1]
            if item == 0xFF:  # LAST: its size counts the words of the items before it
                if (i - off - 4) // 4 != size or i + 12 > len(first):
                    break
                if struct.unpack_from("<I", first, i + 8)[0] == BLOCK_END and image_type is not None:
                    return off, image_type
                break
            if item == 0x42:
                image_type = struct.unpack_from("<H", first, i + 2)[0]
            if size == 0:
                break
            i += 4 * size
    return None


def uf2(segs, family):
    mem = pages(segs)
    n = len(mem)
    out = bytearray()
    for i, addr in enumerate(sorted(mem)):
        head = struct.pack("<IIIIIIII", MAGIC0, MAGIC1, FLAG_FAMILY, addr, PAYLOAD, i, n, family)
        out += head + bytes(mem[addr]) + bytes(476 - PAYLOAD) + struct.pack("<I", MAGIC_END)
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--family", required=True, type=lambda s: int(s, 0), help="the UF2 family ID")
    ap.add_argument("--base", type=lambda s: int(s, 0), help="the flash's first address")
    ap.add_argument("--size", type=lambda s: int(s, 0), help="the flash's size")
    a = ap.parse_args(argv)
    try:
        segs = segments(Path(a.elf).read_bytes())
        if a.base is not None and a.size is not None:
            for addr, data in segs:
                if addr < a.base or addr + len(data) > a.base + a.size:
                    raise Uf2Error(f"bytes at 0x{addr:08X} to 0x{addr + len(data):08X} lie outside the flash "
                                   f"(0x{a.base:08X} + 0x{a.size:X})")
        if a.family in RP2350_FAMILIES:
            found = image_def(segs)
            if found is None:
                raise Uf2Error(f"no IMAGE_DEF block in the image's first 4 KiB: the {RP2350_FAMILIES[a.family]} "
                               "boot ROM would not start it")
        Path(a.out).write_bytes(uf2(segs, a.family))
        return 0
    except (Uf2Error, OSError, struct.error) as e:
        print(f"uf2.py: {a.elf}: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
