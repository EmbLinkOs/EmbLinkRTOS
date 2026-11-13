#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
#
# Build the ESP32 bring-up probe with EmbCC (docs/ports/xtensa.md):
#   tools/esp/bringup/build.sh [OUTDIR]          (default build/esp32-bringup)
# then flash OUTDIR/esp32_hello.bin at 0x1000, where the ESP32's ROM boots from:
#   embflash OUTDIR/esp32_hello.bin --esp serial:/dev/cu.usbserial-XXXX --base 0x1000 --verify
# EMBCC and EMBLD name the compiler and linker (default: embcc, embld on PATH). With
# XTENSA_GCC set (Espressif's xtensa-esp32-elf-gcc, ADR-053), GCC compiles and GNU ld links
# with the same esp32.ld instead.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-"$root/build/esp32-bringup"}
EMBCC=${EMBCC:-embcc}
EMBLD=${EMBLD:-embld}
mkdir -p "$out"
if [ -n "${XTENSA_GCC:-}" ]; then
    "$XTENSA_GCC" -O2 -ffreestanding -nostdlib -T "$here/esp32.ld" "$here/esp32_hello.c" -lgcc -o "$out/esp32_hello.elf"
    echo "esp32-bringup: compiled and linked by $XTENSA_GCC with esp32.ld"
    python3 -I "$root/tools/esp/esp_image.py" "$out/esp32_hello.elf" -o "$out/esp32_hello.bin" --chip esp32
    echo "esp32-bringup: $out/esp32_hello.bin (flash at 0x1000)"
    exit 0
fi
"$EMBCC" --target=xtensa-none-elf -O2 -c "$here/esp32_hello.c" -o "$out/esp32_hello.o"
# esp32.ld puts the code in IRAM and the byte data (.rodata, .data) in DRAM (ADR-052). An embld
# older than EmbCC's D-024 takes no script for Xtensa: then the code goes at the start of the
# ROM-loadable IRAM and .data at the start of DRAM by -Ttext/-Tdata, which works for this probe
# only because its byte data is in .data; esp_image.py --chip esp32 places each segment at its
# run address either way.
if "$EMBLD" -T "$here/esp32.ld" "$out/esp32_hello.o" -o "$out/esp32_hello.elf" 2>"$out/ld.err"; then
    echo "esp32-bringup: linked by esp32.ld"
elif grep -q "linker script is supported for" "$out/ld.err"; then
    echo "esp32-bringup: $EMBLD predates EmbCC D-024 (no Xtensa scripts); linking with -Ttext/-Tdata"
    "$EMBLD" -e call_start_cpu0 -Ttext 0x40080400 -Tdata 0x3FFF0000 "$out/esp32_hello.o" -o "$out/esp32_hello.elf"
else
    cat "$out/ld.err" >&2
    exit 1
fi
python3 -I "$root/tools/esp/esp_image.py" "$out/esp32_hello.elf" -o "$out/esp32_hello.bin" --chip esp32
echo "esp32-bringup: $out/esp32_hello.bin (flash at 0x1000)"
