# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# RV32 with the riscv64-unknown-elf GCC and picolibc. The ISA and ABI flags come from
# the board (EMB_BOARD_C_FLAGS in board.cmake).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_C_COMPILER riscv64-unknown-elf-gcc)
set(CMAKE_ASM_COMPILER riscv64-unknown-elf-gcc)
set(CMAKE_OBJCOPY riscv64-unknown-elf-objcopy)
set(CMAKE_SIZE riscv64-unknown-elf-size)
set(CMAKE_NM riscv64-unknown-elf-nm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_TOOLCHAIN_C_FLAGS -Os -g -mcmodel=medany)
set(EMB_TOOLCHAIN_LINK_FLAGS -nostartfiles --specs=picolibc.specs -Wl,--gc-sections -Wl,--no-warn-rwx-segments)
set(EMB_TOOLCHAIN_DEFINES)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
