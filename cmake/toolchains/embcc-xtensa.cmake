# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The classic ESP32's Xtensa LX6 with EmbCC and embld (ADR-052, ADR-054): the windowed ABI, a
# GNU ld script per board. Needs an embld that takes Xtensa scripts (EmbCC D-024). EMBCC names
# the compiler driver when it is not embcc on PATH; the binary tools are found beside it.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)
if(NOT CMAKE_C_COMPILER)
  if(DEFINED ENV{EMBCC})
    set(CMAKE_C_COMPILER "$ENV{EMBCC}")
  else()
    set(CMAKE_C_COMPILER embcc)
  endif()
endif()
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
get_filename_component(_emb_xt_bin "${CMAKE_C_COMPILER}" DIRECTORY)
if(_emb_xt_bin)
  set(_emb_xt_bin "${_emb_xt_bin}/")
endif()
set(CMAKE_OBJCOPY "${_emb_xt_bin}embcc-objcopy")
set(CMAKE_SIZE "${_emb_xt_bin}embcc-size")
set(CMAKE_NM "${_emb_xt_bin}embcc-nm")
set(CMAKE_AR "${_emb_xt_bin}embcc-ar")
set(CMAKE_RANLIB "${_emb_xt_bin}embcc-ranlib")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_TOOLCHAIN_C_FLAGS --target=xtensa-none-elf -Os -g)
set(EMB_TOOLCHAIN_LINK_FLAGS --target=xtensa-none-elf -nostartfiles -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
