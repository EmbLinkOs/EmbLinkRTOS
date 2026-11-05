#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check the ESP32 bring-up probe's flash image, whichever toolchain built it (ADR-052, ADR-053):
an ESP32 image (magic 0xE9, chip ID 0), every segment inside the IRAM or DRAM region esp32.ld
names, the entry in IRAM, and the probe's message in a DRAM segment, since IRAM takes only
32-bit loads.

    check_probe.py build/esp32-bringup/esp32_hello.bin
"""
import struct
import sys

IRAM = (0x40080400, 0x40080400 + 0xFC00)
DRAM = (0x3FFF0000, 0x3FFF0000 + 0x6000)
MESSAGE = b"emb: hello from the ESP32"


def check(img):
    problems = []
    if len(img) < 24 or img[0] != 0xE9:
        return ["not an ESP image (magic 0xE9)"]
    count = img[1]
    entry, = struct.unpack_from("<I", img, 4)
    chip, = struct.unpack_from("<H", img, 12)
    if chip != 0:
        problems.append(f"chip ID {chip}, not the ESP32's 0")
    if not IRAM[0] <= entry < IRAM[1]:
        problems.append(f"entry 0x{entry:08X} is not in IRAM")
    off, message = 24, False
    for _ in range(count):
        addr, size = struct.unpack_from("<II", img, off)
        data = img[off + 8:off + 8 + size]
        inside = [r for r in (IRAM, DRAM) if r[0] <= addr and addr + size <= r[1]]
        if not inside:
            problems.append(f"segment 0x{addr:08X}+{size} is in neither IRAM nor DRAM")
        elif MESSAGE in data:
            if inside[0] is IRAM:
                problems.append("the message is in IRAM, where a byte load faults")
            message = True
        off += 8 + size
    if not message:
        problems.append("the probe's message is in no segment")
    return problems


def main(argv):
    img = open(argv[1], "rb").read()
    problems = check(img)
    for p in problems:
        print(f"check_probe.py: {argv[1]}: {p}")
    if not problems:
        print(f"check_probe.py: {argv[1]}: an ESP32 image, IRAM code, the message in DRAM")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
