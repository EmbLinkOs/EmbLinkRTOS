#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# Builds the NUCLEO-F446RE UART relay (relay.c) and, with --flash, programs it through the
# board's ST-LINK with OpenOCD.
#
#   tools/hil/nucleo-uart-relay/build.sh [BUILD] [--flash]   # default build/nucleo-uart-relay
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
bd="$root/boards/st/nucleo_f446re/generated"
out="$root/build/nucleo-uart-relay"
flash=0
for a in "$@"; do
    case "$a" in
        --flash) flash=1 ;;
        *) out=$(mkdir -p "$a" && cd "$a" && pwd) ;;
    esac
done
mkdir -p "$out"
CC=${ARM_GCC:-arm-none-eabi-gcc}
$CC -mthumb -mcpu=cortex-m4 -mfloat-abi=soft -Os -g -std=c11 -ffunction-sections -fdata-sections \
    -Wall -Wextra -Werror -I"$bd/include" -I"$root/drivers/st/include" -I"$root/soc/st/stm32f4/include" \
    "$here/relay.c" "$root/drivers/st/stm32f4_rcc.c" "$root/drivers/st/stm32f4_gpio.c" \
    "$root/drivers/st/stm32_usart.c" -nostartfiles --specs=nano.specs -Wl,--gc-sections \
    -Wl,--no-warn-rwx-segments -T"$bd/linker/regions.ld" -o "$out/relay.elf"
${ARM_SIZE:-arm-none-eabi-size} "$out/relay.elf"
if [ "$flash" = 1 ]; then
    openocd -f board/st_nucleo_f4.cfg -c "program $out/relay.elf verify reset exit"
fi
echo "nucleo-uart-relay: $out/relay.elf"
