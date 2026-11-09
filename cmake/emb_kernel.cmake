# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# emb_kernel(): the kernel, the port and the board for EMB_BOARD, in the calling project. The
# RTOS's own CMakeLists.txt calls it, and so does an application through find_package
# (cmake/EmbLinkRTOSConfig.cmake, docs/user/application.md). A macro: the CONFIG_EMB_*
# variables and the board's EMB_BOARD_* land in the caller's scope, where
# emb_add_executable() reads them. EMB_BASE is the RTOS's directory.
set(EmbLinkRTOS_VERSION 0.2.0)

macro(emb_kernel)
  if(NOT DEFINED EMB_BASE)
    message(FATAL_ERROR "emb_kernel(): EMB_BASE is not set (find_package(EmbLinkRTOS) sets it)")
  endif()
  set(EMB_BOARD "native" CACHE STRING "Board name: a directory boards/<vendor>/<board>/")
  set(EMB_PROFILE "" CACHE STRING "Reference profile: tiny or base (default from the board)")
  set(EMB_CONF "" CACHE STRING "Extra configuration fragments, semicolon separated, applied after the profile")
  set(EMB_SET "" CACHE STRING "Symbol overrides SYMBOL=value, semicolon separated, applied last")
  option(EMB_WERROR "Treat warnings as errors (CODING-STANDARD CS-13.1)" ON)
  # compile_commands.json for clangd and clang-tidy
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  list(APPEND CMAKE_MODULE_PATH "${EMB_BASE}/cmake")
  include(emb_config)          # board lookup, embconfig run, CONFIG_EMB_* variables
  include(emb_native_compiler) # optional explicit EmbCC kernel / GCC host boundary
  include(emb_flags)           # warning set and per-toolchain flags
  include(emb_target)          # emb_add_executable(), footprint targets

  # the RTOS's own build keeps its binary layout; an application's gets one subdirectory
  if("${EMB_BASE}" STREQUAL "${CMAKE_SOURCE_DIR}")
    set(_emb_bin "${CMAKE_BINARY_DIR}")
  else()
    set(_emb_bin "${CMAKE_BINARY_DIR}/emblinkrtos")
  endif()
  add_subdirectory("${EMB_BASE}/include" "${_emb_bin}/include")
  add_subdirectory("${EMB_BASE}/arch/${CONFIG_EMB_ARCH}" "${_emb_bin}/arch/${CONFIG_EMB_ARCH}")
  add_subdirectory("${EMB_BASE}/kernel" "${_emb_bin}/kernel")
  add_subdirectory("${EMB_BASE}/subsys" "${_emb_bin}/subsys")
  emb_check_native_abi()
  add_subdirectory("${EMB_BOARD_DIR}" "${_emb_bin}/boards/${EMB_BOARD}")

  # the board's QEMU machine, for run_<image> and the test runner (SIM-002)
  if(NOT CONFIG_EMB_ARCH STREQUAL "native" AND EMB_BOARD_QEMU_ARGS)
    set(_qemu_for_avr qemu-system-avr)
    set(_qemu_for_cortex_m qemu-system-arm)
    set(_qemu_for_riscv qemu-system-riscv32)
    find_program(EMB_QEMU ${_qemu_for_${CONFIG_EMB_ARCH}})
  endif()
  emb_print_summary()
endmacro()
