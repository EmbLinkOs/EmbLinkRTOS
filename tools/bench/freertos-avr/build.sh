#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# FreeRTOS's build of tests/benchmarks/bench_compare.c for the Arduino Uno: the comparison of
# R-003 §6 (the review's F14). Fetches FreeRTOS-Kernel V11.1.0 at its pinned commit, uses its
# ATmega port with the tick on Timer0 at 1 kHz (the port's default is the watchdog at 15 ms;
# EmbLinkRTOS ticks at 1 kHz, so both pay a similar tick), and compiles with the flags of
# cmake/toolchains/avr-gcc.cmake. Writes BUILD/bench_compare.elf and .hex.
#
#   tools/bench/freertos-avr/build.sh [BUILD]     # default build/freertos-avr
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-"$root/build/freertos-avr"}
tag=V11.1.0
commit=dbf70559b27d39c1fdb68dfb9a32140b6a6777a0
src="$out/FreeRTOS-Kernel"
mkdir -p "$out"
if [ ! -d "$src" ]; then
    git clone -q --depth 1 --branch "$tag" https://github.com/FreeRTOS/FreeRTOS-Kernel.git "$src"
fi
got=$(git -C "$src" rev-parse HEAD)
[ "$got" = "$commit" ] || { echo "freertos-avr: $src is at $got, not $commit ($tag)" >&2; exit 1; }
port="$out/port"
mkdir -p "$port"
cp "$src/portable/ThirdParty/GCC/ATmega/port.c" "$port/"
# the tick on Timer0 instead of the watchdog: the one line that chooses it
sed 's/#if defined( WDIE ) \&\& defined( WDIF ).*/#if 0 \/* bench_compare: the Timer0 tick *\//' \
    "$src/portable/ThirdParty/GCC/ATmega/portmacro.h" > "$port/portmacro.h"
grep -q '#if 0 /\* bench_compare' "$port/portmacro.h" || { echo "freertos-avr: the tick patch did not apply" >&2; exit 1; }
CC=${AVR_GCC:-avr-gcc}
flags="-mmcu=atmega328p -DF_CPU=16000000UL -Os -g -fno-short-enums -mrelax -mcall-prologues -std=gnu11 \
 -ffunction-sections -fdata-sections -Wall -DBENCH_FREERTOS -I$here -I$port -I$src/include"
# shellcheck disable=SC2086
# BENCH_LTO=1: link-time optimization, as an EMB_LTO build of the EmbLinkRTOS side (ADR-056)
[ "${BENCH_LTO:-0}" = 1 ] && flags="$flags -flto"
$CC $flags "$root/tests/benchmarks/bench_compare.c" "$src/tasks.c" "$src/queue.c" "$src/list.c" \
    "$port/port.c" -Wl,--gc-sections -Wl,-Map="$out/bench_compare.elf.map" -o "$out/bench_compare.elf"
${AVR_OBJCOPY:-avr-objcopy} -O ihex -R .eeprom "$out/bench_compare.elf" "$out/bench_compare.hex"
${AVR_SIZE:-avr-size} "$out/bench_compare.elf"
echo "freertos-avr: $out/bench_compare.hex (FreeRTOS $tag, $commit)"
