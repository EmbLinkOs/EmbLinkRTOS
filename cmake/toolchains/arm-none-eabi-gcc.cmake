# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
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
# RAM holds code where a board runs some from it (.ramfunc: the RP2350's flash routines, run
# while XIP is off), so its load segment is writable and executable, as the core allows
set(EMB_TOOLCHAIN_LINK_FLAGS -mthumb -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,--no-warn-rwx-segments)
set(EMB_TOOLCHAIN_DEFINES)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
