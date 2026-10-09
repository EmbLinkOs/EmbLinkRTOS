# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# MPS2 an385 (Cortex-M3) as QEMU models it (machine mps2-an385): the emulated Cortex-M reference.
set(EMB_BOARD_ARCH cortex_m)
set(EMB_BOARD_VARIANT armv7m)
set(EMB_BOARD_DEFAULT_PROFILE base)
set(EMB_BOARD_C_FLAGS -mcpu=cortex-m3 -mfloat-abi=soft)
set(EMB_BOARD_QEMU_ARGS -M mps2-an385 -semihosting-config enable=on,target=native -kernel)
