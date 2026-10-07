# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# EmbLinkRTOS reference build (05 §2.2, BLD-001). Configure with a preset:
#   cmake --preset native-gcc && cmake --build --preset native-gcc && ctest --preset native-gcc
#   cmake --preset avr-uno    && cmake --build --preset avr-uno
# or by hand with -DEMB_BOARD=<board> -DEMB_PROFILE=<profile> and a toolchain file.
cmake_minimum_required(VERSION 3.20)

project(EmbLinkRTOS VERSION 0.2.0 LANGUAGES C)

set(EMB_BOARD "native" CACHE STRING "Board name: a directory boards/<vendor>/<board>/")
set(EMB_PROFILE "" CACHE STRING "Reference profile: tiny or base (default from the board)")
set(EMB_CONF "" CACHE STRING "Extra configuration fragments, semicolon separated, applied after the profile")
set(EMB_SET "" CACHE STRING "Symbol overrides SYMBOL=value, semicolon separated, applied last")
option(EMB_BUILD_TESTS "Build the test suites (native only)" ON)
option(EMB_BUILD_SAMPLES "Build the samples" ON)
option(EMB_WERROR "Treat warnings as errors (CODING-STANDARD CS-13.1)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(emb_config)      # board lookup, embconfig run, CONFIG_EMB_* variables
include(emb_flags)       # warning set and per-toolchain flags
include(emb_target)      # emb_add_executable(), footprint targets

add_subdirectory(include)
add_subdirectory(arch/${CONFIG_EMB_ARCH})
add_subdirectory(kernel)
add_subdirectory(${EMB_BOARD_DIR} boards/${EMB_BOARD})
if(EMB_BUILD_SAMPLES)
  add_subdirectory(samples)
endif()
if(EMB_BUILD_TESTS AND CONFIG_EMB_ARCH STREQUAL "native")
  enable_testing()
  add_subdirectory(tests)
endif()

emb_print_summary()
