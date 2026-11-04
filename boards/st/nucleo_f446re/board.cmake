# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# NUCLEO-F446RE (MB1136 C-03): STM32F446RET6, Cortex-M4F at 180 MHz from the ST-LINK's 8 MHz MCO.
set(EMB_BOARD_ARCH cortex_m)
set(EMB_BOARD_VARIANT armv7em)
set(EMB_BOARD_DEFAULT_PROFILE base)
set(EMB_BOARD_C_FLAGS -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
set(EMB_BOARD_EMBSIM_TARGET emblink/nucleo_f446re) # EMB_TEST_EMULATOR=embsim: EmbSIM's NUCLEO-F446RE
