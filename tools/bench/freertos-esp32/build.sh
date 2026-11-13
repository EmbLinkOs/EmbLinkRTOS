#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
#
# Build bench_compare against ESP-IDF's FreeRTOS for the classic ESP32 (A96):
#   . $IDF_PATH/export.sh && tools/bench/freertos-esp32/build.sh [OUTDIR]
# (default build/freertos-esp32). ESP-IDF must be installed for the esp32 target; CI uses
# Espressif's espressif/idf container instead. Flash the three images it lists, then read the
# console (run_hil.py --flash-cmd true --until BENCH_END --bench, or any terminal):
#   embflash OUT/bootloader/bootloader.bin --esp serial:PORT --base 0x1000
#   embflash OUT/partition_table/partition-table.bin --esp serial:PORT --base 0x8000
#   embflash OUT/bench_compare_freertos.bin --esp serial:PORT --base 0x10000
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-"$root/build/freertos-esp32"}
mkdir -p "$out"
out=$(cd "$out" && pwd) # absolute: ESP-IDF reads SDKCONFIG relative to the project
: "${IDF_PATH:?source export.sh from ESP-IDF first}"
idf.py -C "$here" -B "$out" -DSDKCONFIG="$out/sdkconfig" -DSDKCONFIG_DEFAULTS="$here/sdkconfig.defaults" build
echo "freertos-esp32: $out/bootloader/bootloader.bin at 0x1000, $out/partition_table/partition-table.bin at 0x8000, $out/bench_compare_freertos.bin at 0x10000"
