# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# EmbLinkRTOS reference build (05 §2.2, BLD-001). Configure with a preset:
#   cmake --preset native-gcc && cmake --build --preset native-gcc && ctest --preset native-gcc
#   cmake --preset avr-uno    && cmake --build --preset avr-uno
# or by hand with -DEMB_BOARD=<board> -DEMB_PROFILE=<profile> and a toolchain file.
cmake_minimum_required(VERSION 3.20)

project(EmbLinkRTOS VERSION 0.2.0 LANGUAGES C)

set(EMB_BASE "${CMAKE_CURRENT_SOURCE_DIR}")
option(EMB_BUILD_TESTS "Build the test suites (native only)" ON)
option(EMB_BUILD_SAMPLES "Build the samples" ON)

# the kernel, the port and the board for EMB_BOARD: the same macro an application calls
# after find_package(EmbLinkRTOS) (cmake/emb_kernel.cmake, docs/user/application.md)
include(cmake/emb_kernel.cmake)
emb_kernel()
if(EMB_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()
if(EMB_BUILD_TESTS)
  if(CONFIG_EMB_ARCH STREQUAL "native")
    enable_testing()
  else()
    # the board's emulator runs the test images (SIM-002): its QEMU machine, or its
    # EmbSIM board (EMB_TEST_EMULATOR=embsim, EMB_BOARD_EMBSIM_TARGET in board.cmake)
    set(EMB_TEST_EMULATOR "qemu" CACHE STRING "where ctest runs the test images: qemu, embsim or hil (a physical board)")
    set_property(CACHE EMB_TEST_EMULATOR PROPERTY STRINGS qemu embsim hil)
    set(EMB_HIL_PORT "" CACHE STRING "EMB_TEST_EMULATOR=hil: the board console's serial device")
    set(EMB_HIL_ARCHIVE "${CMAKE_BINARY_DIR}/hil-archive" CACHE PATH
        "EMB_TEST_EMULATOR=hil: where each run's console log and record go (tools/hil/archive.py)")
    if(EMB_TEST_EMULATOR STREQUAL "hil")
      # a board described in YAML: its generated hw.json names the probe and the console
      if(EMB_HIL_PORT AND EXISTS "${EMB_BOARD_HW_JSON}")
        enable_testing()
      else()
        message(STATUS "hil: set EMB_HIL_PORT to the board's console (a board with a generated hw.json): test images are built without ctest entries")
      endif()
    elseif(EMB_TEST_EMULATOR STREQUAL "embsim")
      find_program(EMB_EMBSIM embsim)
      if(EMB_EMBSIM AND EMB_BOARD_EMBSIM_TARGET)
        enable_testing()
      else()
        message(STATUS "no EmbSIM board for ${EMB_BOARD} (or no embsim): test images are built without ctest entries")
      endif()
    else()
      if(EMB_QEMU AND EMB_BOARD_QEMU_ARGS) # found by emb_kernel()
        enable_testing()
      else()
        message(STATUS "no emulator for ${EMB_BOARD}: test images are built without ctest entries")
      endif()
    endif()
  endif()
  add_subdirectory(tests)
endif()

