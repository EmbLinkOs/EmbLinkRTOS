# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The classic ESP32's Xtensa LX6 with Espressif's Clang (espressif/llvm-project), its newlib
# and LLD (ADR-053, ADR-054): clang from the clang-esp release on PATH, with the libs-clang-esp
# and lld-esp archives unpacked beside it. Upstream LLVM's Xtensa target is experimental and
# not built into common distributions; Espressif's fork is the one ESP-IDF uses.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)
set(CMAKE_C_COMPILER clang)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_C_COMPILER_TARGET xtensa-esp-elf)
set(CMAKE_ASM_COMPILER_TARGET xtensa-esp-elf)
set(CMAKE_OBJCOPY llvm-objcopy)
set(CMAKE_SIZE llvm-size)
set(CMAKE_NM llvm-nm)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_TOOLCHAIN_C_FLAGS -mcpu=esp32 -Os -g)
set(EMB_TOOLCHAIN_LINK_FLAGS -mcpu=esp32 -fuse-ld=lld -nostartfiles -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
