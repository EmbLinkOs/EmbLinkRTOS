# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# find_package(EmbLinkRTOS): the RTOS as a CMake package, for an application kept outside its
# tree (docs/user/application.md, A91). Call it before project(): when no toolchain file is
# given, it picks the one for EMB_BOARD's architecture, which project() then loads. After
# project(), emb_kernel() builds the kernel, the port and the board, and emb_add_executable()
# links an image against them.
#
#   cmake_minimum_required(VERSION 3.20)
#   find_package(EmbLinkRTOS REQUIRED)
#   project(my_app LANGUAGES C)
#   emb_kernel()
#   emb_add_executable(app main.c)
get_filename_component(EMB_BASE "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${EMB_BASE}/cmake/emb_kernel.cmake")
set(EmbLinkRTOS_FOUND TRUE)

set(EMB_BOARD "native" CACHE STRING "Board name: a directory boards/<vendor>/<board>/")
set(EMB_BOARD_ROOT "" CACHE STRING "Extra board roots, semicolon separated: each holds boards/<vendor>/<board>/")

if(NOT CMAKE_TOOLCHAIN_FILE)
  if(CMAKE_C_COMPILER_LOADED)
    message(FATAL_ERROR "find_package(EmbLinkRTOS) after project(): call it before project(), "
                        "or give -DCMAKE_TOOLCHAIN_FILE for EMB_BOARD's architecture")
  endif()
  # the board's architecture, from its board.cmake, without configuring anything yet
  set(_emb_roots "${EMB_BASE}")
  foreach(_r ${EMB_BOARD_ROOT})
    get_filename_component(_r "${_r}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    list(APPEND _emb_roots "${_r}")
  endforeach()
  set(_emb_found "")
  foreach(_r ${_emb_roots})
    file(GLOB _f "${_r}/boards/*/${EMB_BOARD}/board.cmake" "${_r}/boards/*/${EMB_BOARD}/generated/board.cmake")
    list(APPEND _emb_found ${_f})
  endforeach()
  list(LENGTH _emb_found _emb_n)
  if(NOT _emb_n EQUAL 1)
    message(FATAL_ERROR "EMB_BOARD='${EMB_BOARD}': not one board under ${_emb_roots} (found ${_emb_n})")
  endif()
  file(STRINGS "${_emb_found}" _emb_arch_line REGEX "^set\\(EMB_BOARD_ARCH ")
  string(REGEX REPLACE "^set\\(EMB_BOARD_ARCH ([a-z_]+)\\).*" "\\1" _emb_arch "${_emb_arch_line}")
  set(_emb_toolchain_native native-gcc)
  set(_emb_toolchain_cortex_m arm-none-eabi-gcc)
  set(_emb_toolchain_riscv riscv-gcc)
  set(_emb_toolchain_avr avr-gcc)
  set(_emb_toolchain_xtensa embcc-xtensa)
  if(NOT DEFINED _emb_toolchain_${_emb_arch})
    message(FATAL_ERROR "EMB_BOARD='${EMB_BOARD}': no default toolchain for architecture '${_emb_arch}'; "
                        "give -DCMAKE_TOOLCHAIN_FILE")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "${EMB_BASE}/cmake/toolchains/${_emb_toolchain_${_emb_arch}}.cmake"
      CACHE FILEPATH "The toolchain for EMB_BOARD's architecture (find_package(EmbLinkRTOS))")
  message(STATUS "EmbLinkRTOS: board ${EMB_BOARD} (${_emb_arch}), toolchain ${CMAKE_TOOLCHAIN_FILE}")
endif()
