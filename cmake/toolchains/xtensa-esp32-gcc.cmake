# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The classic ESP32's Xtensa LX6 with Espressif's GCC and GNU ld (ADR-052, ADR-053, ADR-054):
# xtensa-esp32-elf-gcc from Espressif's crosstool-NG releases, the same board script as embld.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)
set(CMAKE_C_COMPILER xtensa-esp32-elf-gcc)
set(CMAKE_ASM_COMPILER xtensa-esp32-elf-gcc)
set(CMAKE_OBJCOPY xtensa-esp32-elf-objcopy)
set(CMAKE_SIZE xtensa-esp32-elf-size)
set(CMAKE_NM xtensa-esp32-elf-nm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_TOOLCHAIN_C_FLAGS -Os -g)
set(EMB_TOOLCHAIN_LINK_FLAGS -nostartfiles -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
