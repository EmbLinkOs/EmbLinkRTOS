# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# Host build with EmbCC (09 §2). Storage generation now passes, but current Linux
# native compilation needs a compatible POSIX header/runtime/link strategy.
# For the explicit experimental EmbCC-kernel/GCC-host build, use the native-embcc
# presets (embcc-native-mixed.cmake); this file retains the all-EmbCC experiment.
# No full compiler CI leg is configured. EmbCC's C dialect is described in 09 §3.
set(CMAKE_C_COMPILER embcc)
set(EMB_TOOLCHAIN_C_FLAGS -pthread -g -O1)
set(EMB_TOOLCHAIN_LINK_FLAGS -pthread)
set(EMB_TOOLCHAIN_DEFINES _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)

if(APPLE)
  list(APPEND EMB_TOOLCHAIN_LINK_FLAGS -Wl,-no_warn_duplicate_libraries)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/embcc-depfile.cmake") # header dependencies (see there)
