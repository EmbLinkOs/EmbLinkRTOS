#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# Build the ESP32 bring-up probe with EmbCC (docs/ports/xtensa.md):
#   tools/esp/bringup/build.sh [OUTDIR]          (default build/esp32-bringup)
# then flash OUTDIR/esp32_hello.bin at 0x1000, where the ESP32's ROM boots from:
#   embflash OUTDIR/esp32_hello.bin --esp serial:/dev/cu.usbserial-XXXX --base 0x1000 --verify
# EMBCC and EMBLD name the compiler and linker (default: embcc, embld on PATH).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-"$root/build/esp32-bringup"}
EMBCC=${EMBCC:-embcc}
EMBLD=${EMBLD:-embld}
mkdir -p "$out"
"$EMBCC" --target=xtensa-none-elf -O2 -c "$here/esp32_hello.c" -o "$out/esp32_hello.o"
# embld takes no linker script for Xtensa: the code goes at the start of the
# ROM-loadable IRAM (ESP-IDF's bootloader.ld iram_seg) and .data at the start
# of its dram_seg; esp_image.py --chip esp32 places each segment at its run address
"$EMBLD" -e call_start_cpu0 -Ttext 0x40080400 -Tdata 0x3FFF0000 "$out/esp32_hello.o" -o "$out/esp32_hello.elf"
python3 -I "$root/tools/esp/esp_image.py" "$out/esp32_hello.elf" -o "$out/esp32_hello.bin" --chip esp32
echo "esp32-bringup: $out/esp32_hello.bin (flash at 0x1000)"
