# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
set(EMB_BOARD_ARCH avr)
set(EMB_BOARD_VARIANT atmega328p)
set(EMB_BOARD_MCU atmega328p)
set(EMB_BOARD_DEFAULT_PROFILE tiny)
set(EMB_BOARD_QEMU_ARGS -M arduino-uno -icount shift=6,sleep=off -bios) # ~16 MIPS, deterministic time
