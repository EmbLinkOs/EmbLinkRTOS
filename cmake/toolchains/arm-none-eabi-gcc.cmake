# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# Cortex-M with the GNU Arm Embedded toolchain. The CPU and floating-point flags come
# from the board (EMB_BOARD_C_FLAGS in board.cmake), so one toolchain file serves every
# Cortex-M board.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)
set(CMAKE_NM arm-none-eabi-nm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_TOOLCHAIN_C_FLAGS -mthumb -Os -g)
set(EMB_TOOLCHAIN_LINK_FLAGS -mthumb -nostartfiles --specs=nano.specs -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
