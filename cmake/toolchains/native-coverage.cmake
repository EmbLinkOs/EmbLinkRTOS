# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Host build with gcov instrumentation for statement and branch coverage (M2-10);
# tools/coverage/report.py turns the counters into the report.
set(CMAKE_C_COMPILER gcc)
set(EMB_TOOLCHAIN_C_FLAGS -pthread -g -O0 --coverage)
set(EMB_TOOLCHAIN_LINK_FLAGS -pthread --coverage)
set(EMB_TOOLCHAIN_DEFINES _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE EMB_NATIVE_COVERAGE=1)

if(APPLE)
  list(APPEND EMB_TOOLCHAIN_LINK_FLAGS -Wl,-no_warn_duplicate_libraries)
endif()
