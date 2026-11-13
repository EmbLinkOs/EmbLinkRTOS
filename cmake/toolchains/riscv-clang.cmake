# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# RV32 with Clang and LLD (ADR-053): Clang compiles for --target=riscv32-unknown-elf against
# picolibc's headers, and links with LLD against picolibc and libgcc from the riscv64-unknown-elf
# GCC's multilib for the board's ISA and ABI (cmake/emb_flags.cmake finds the directory). The
# ISA and ABI flags come from the board (EMB_BOARD_C_FLAGS in board.cmake), as with riscv-gcc.
# EMB_CLANG names the clang (default: clang on PATH); ld.lld and riscv64-unknown-elf-gcc, with
# picolibc, must be on PATH.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
if(NOT CMAKE_C_COMPILER)
  if(DEFINED ENV{EMB_CLANG})
    set(CMAKE_C_COMPILER "$ENV{EMB_CLANG}")
  else()
    set(CMAKE_C_COMPILER clang)
  endif()
endif()
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
find_program(EMB_RISCV_GCC riscv64-unknown-elf-gcc REQUIRED)
find_program(CMAKE_OBJCOPY riscv64-unknown-elf-objcopy REQUIRED)
find_program(CMAKE_SIZE riscv64-unknown-elf-size REQUIRED)
find_program(CMAKE_NM riscv64-unknown-elf-nm REQUIRED)
find_program(CMAKE_OBJDUMP riscv64-unknown-elf-objdump REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/gnu-libc-headers.cmake")
emb_gnu_libc_include("${EMB_RISCV_GCC}" _emb_rv_inc --specs=picolibc.specs) # picolibc's headers
# -ftls-model=local-exec: what picolibc.specs gives GCC, for errno's thread-local block (A88)
set(EMB_TOOLCHAIN_C_FLAGS --target=riscv32-unknown-elf -Os -g -mcmodel=medany -ftls-model=local-exec
                          -nostdlibinc -isystem "${_emb_rv_inc}")
set(EMB_TOOLCHAIN_LINK_FLAGS --target=riscv32-unknown-elf -fuse-ld=lld -nostdlib -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
set(EMB_TOOLCHAIN_GCC_LIBS "${EMB_RISCV_GCC}")
set(EMB_TOOLCHAIN_GCC_LIBS_FLAGS --specs=picolibc.specs)
set(EMB_TOOLCHAIN_GCC_LIBS_NAMES libc.a)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
