# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Cortex-M with Clang and LLD (ADR-053): Clang compiles for --target=arm-none-eabi against the C
# library of the Arm GNU toolchain (newlib's headers), and links with LLD against that toolchain's
# multilib newlib-nano and libgcc for the board's CPU flags (cmake/emb_flags.cmake finds the
# directory). EMB_CLANG names the clang (default: clang on PATH); ld.lld and arm-none-eabi-gcc
# must be on PATH.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
if(NOT CMAKE_C_COMPILER)
  if(DEFINED ENV{EMB_CLANG})
    set(CMAKE_C_COMPILER "$ENV{EMB_CLANG}")
  else()
    set(CMAKE_C_COMPILER clang)
  endif()
endif()
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
find_program(EMB_ARM_GCC arm-none-eabi-gcc REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/gnu-libc-headers.cmake")
emb_gnu_libc_include("${EMB_ARM_GCC}" _emb_arm_inc) # newlib's headers
find_program(CMAKE_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(CMAKE_SIZE arm-none-eabi-size REQUIRED)
find_program(CMAKE_NM arm-none-eabi-nm REQUIRED)
# -fshort-enums: the Arm GNU toolchain's bare-metal ABI, which its newlib and libgcc are built with
set(EMB_TOOLCHAIN_C_FLAGS --target=arm-none-eabi -mthumb -fshort-enums -Os -g -nostdlibinc
                          -isystem "${_emb_arm_inc}")
set(EMB_TOOLCHAIN_LINK_FLAGS --target=arm-none-eabi -mthumb -fuse-ld=lld -nostdlib -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
set(EMB_TOOLCHAIN_GCC_LIBS "${EMB_ARM_GCC}")
set(EMB_TOOLCHAIN_GCC_LIBS_FLAGS -mthumb)
set(EMB_TOOLCHAIN_GCC_LIBS_NAMES libc_nano.a)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
