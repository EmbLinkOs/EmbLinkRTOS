#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# FreeRTOS's build of tests/benchmarks/bench_compare.c for the Raspberry Pi Pico 2 W (RP2350,
# Cortex-M33), as tools/bench/freertos-cm4/build.sh for the NUCLEO: FreeRTOS-Kernel V11.1.0 at its
# pinned commit, its GCC/ARM_CM33_NTZ port in the Secure state, the flags of
# cmake/toolchains/arm-none-eabi-gcc.cmake and the board's CPU flags, linked by the board's own
# regions.ld with its IMAGE_DEF block. bench_platform.c is the vector table and reset; the
# board's own support (soc/rpi/rp2350/board.c) brings up the 150 MHz clock, the pins and UART0.
#
#   tools/bench/freertos-cm33/build.sh [BUILD]     # default build/freertos-cm33-pico2_w
#   BENCH_LTO=1 ...                                # link-time optimization (ADR-056)
#
# Writes BUILD/bench_compare.elf and .uf2 (picotool loads it; tools/hil/run_hil.py runs it).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
bd="$root/boards/rpi/pico2_w/generated"
hz=150000000
bits=4
out=${1:-"$root/build/freertos-cm33-pico2_w"}
mkdir -p "$out"
out=$(cd "$out" && pwd)
tag=V11.1.0
commit=dbf70559b27d39c1fdb68dfb9a32140b6a6777a0
src="$out/FreeRTOS-Kernel"
if [ ! -d "$src" ]; then
    git clone -q --depth 1 --branch "$tag" https://github.com/FreeRTOS/FreeRTOS-Kernel.git "$src"
fi
got=$(git -C "$src" rev-parse HEAD)
[ "$got" = "$commit" ] || { echo "freertos-cm33: $src is at $got, not $commit ($tag)" >&2; exit 1; }
irq=$(sed -n 's/^#define EMB_HW_TEST_IRQ  *\([0-9]*\)u.*/\1/p' "$bd/hw_config.h")
[ -n "$irq" ] || { echo "freertos-cm33: no EMB_HW_TEST_IRQ in $bd/hw_config.h" >&2; exit 1; }
port="$src/portable/GCC/ARM_CM33_NTZ/non_secure"
CC=${ARM_GCC:-arm-none-eabi-gcc}
flags="-mthumb -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -Os -g -std=gnu11 \
 -ffunction-sections -fdata-sections -Wall -DBENCH_FREERTOS -DBENCH_CPU_HZ=$hz -DBENCH_PRIO_BITS=$bits \
 -DBENCH_TEST_IRQ=$irq -DEMB_RP2350_EXIT_TO_BOOTSEL=1 -I$here -I$port -I$src/include -I$bd \
 -I$root/include -I$root/arch/cortex_m/include -I$root/soc/rpi/rp2350/include"
# BENCH_LTO=1: link-time optimization, as an EMB_LTO build of the EmbLinkRTOS side (ADR-056)
[ "${BENCH_LTO:-0}" = 1 ] && flags="$flags -flto"
$CC $flags "$root/tests/benchmarks/bench_compare.c" "$here/bench_platform.c" \
    "$root/soc/rpi/rp2350/board.c" "$bd/board_pins.c" "$bd/boot_block.c" \
    "$src/tasks.c" "$src/queue.c" "$src/list.c" "$port/port.c" "$port/portasm.c" \
    -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,--no-warn-rwx-segments -T"$bd/regions.ld" \
    -Wl,-Map="$out/bench_compare.elf.map" -o "$out/bench_compare.elf"
python3 "$root/tools/uf2/uf2.py" "$out/bench_compare.elf" -o "$out/bench_compare.uf2" --family 0xE48BFF59
${ARM_SIZE:-arm-none-eabi-size} "$out/bench_compare.elf"
echo "freertos-cm33: $out/bench_compare.elf (pico2_w, FreeRTOS $tag, $commit, test line $irq)"
