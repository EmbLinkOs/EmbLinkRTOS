#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# FreeRTOS's build of tests/benchmarks/bench_compare.c for a Cortex-M4 board, as the Uno's
# (tools/bench/freertos-avr/build.sh): FreeRTOS-Kernel V11.1.0 at its pinned commit, its
# GCC/ARM_CM4F port, the flags of cmake/toolchains/arm-none-eabi-gcc.cmake and the board's CPU
# flags, linked by the board's own regions.ld. bench_platform.c is the vector table, reset and
# console; on the NUCLEO-F446RE the board's own drivers set the 180 MHz clock and USART2.
#
#   tools/bench/freertos-cm4/build.sh nucleo_f446re [BUILD]   # the board (180 MHz)
#   tools/bench/freertos-cm4/build.sh mps2_an386 [BUILD]      # QEMU: a functional check only
#
# Writes BUILD/bench_compare.elf, .bin and .hex (default build/freertos-cm4-BOARD).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
board=${1:?board: nucleo_f446re or mps2_an386}
case "$board" in
    nucleo_f446re) bd="$root/boards/st/nucleo_f446re/generated"; nucleo=1; hz=180000000; bits=4;;
    mps2_an386) bd="$root/boards/arm/mps2_an386/generated"; nucleo=0; hz=25000000; bits=3;;
    *) echo "freertos-cm4: unknown board $board" >&2; exit 2;;
esac
out=${2:-"$root/build/freertos-cm4-$board"}
mkdir -p "$out"
out=$(cd "$out" && pwd)
tag=V11.1.0
commit=dbf70559b27d39c1fdb68dfb9a32140b6a6777a0
src="$out/FreeRTOS-Kernel"
if [ ! -d "$src" ]; then
    git clone -q --depth 1 --branch "$tag" https://github.com/FreeRTOS/FreeRTOS-Kernel.git "$src"
fi
got=$(git -C "$src" rev-parse HEAD)
[ "$got" = "$commit" ] || { echo "freertos-cm4: $src is at $got, not $commit ($tag)" >&2; exit 1; }
irq=$(sed -n 's/^#define EMB_HW_TEST_IRQ  *\([0-9]*\)u.*/\1/p' "$bd/hw_config.h")
[ -n "$irq" ] || { echo "freertos-cm4: no EMB_HW_TEST_IRQ in $bd/hw_config.h" >&2; exit 1; }
port="$src/portable/GCC/ARM_CM4F"
CC=${ARM_GCC:-arm-none-eabi-gcc}
flags="-mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os -g -std=gnu11 \
 -ffunction-sections -fdata-sections -Wall -DBENCH_FREERTOS -DBENCH_CPU_HZ=$hz -DBENCH_PRIO_BITS=$bits \
 -DBENCH_NUCLEO=$nucleo -DBENCH_TEST_IRQ=$irq -I$here -I$port -I$src/include -I$bd"
extra=""
if [ "$nucleo" = 1 ]; then
    flags="$flags -I$root/drivers/st/include -I$root/soc/st/stm32f4/include"
    extra="$root/drivers/st/stm32f4_rcc.c $root/drivers/st/stm32f4_gpio.c $root/drivers/st/stm32_usart.c $bd/board_pins.c"
fi
# shellcheck disable=SC2086
# BENCH_LTO=1: link-time optimization, as an EMB_LTO build of the EmbLinkRTOS side (ADR-056)
[ "${BENCH_LTO:-0}" = 1 ] && flags="$flags -flto"
$CC $flags "$root/tests/benchmarks/bench_compare.c" "$here/bench_platform.c" $extra \
    "$src/tasks.c" "$src/queue.c" "$src/list.c" "$port/port.c" \
    -nostartfiles --specs=nano.specs -Wl,--gc-sections -T"$bd/regions.ld" \
    -Wl,-Map="$out/bench_compare.elf.map" -o "$out/bench_compare.elf"
${ARM_OBJCOPY:-arm-none-eabi-objcopy} -O binary "$out/bench_compare.elf" "$out/bench_compare.bin"
${ARM_OBJCOPY:-arm-none-eabi-objcopy} -O ihex "$out/bench_compare.elf" "$out/bench_compare.hex"
${ARM_SIZE:-arm-none-eabi-size} "$out/bench_compare.elf"
echo "freertos-cm4: $out/bench_compare.elf ($board, FreeRTOS $tag, $commit, test line $irq)"
