# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# MPS2 an386 (Cortex-M4F) as QEMU models it (machine mps2-an386): the emulated Cortex-M reference.
set(EMB_BOARD_ARCH cortex_m)
set(EMB_BOARD_VARIANT armv7em)
set(EMB_BOARD_DEFAULT_PROFILE base)
set(EMB_BOARD_C_FLAGS -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
set(EMB_BOARD_QEMU_ARGS -M mps2-an386 -semihosting-config enable=on,target=native -kernel)
