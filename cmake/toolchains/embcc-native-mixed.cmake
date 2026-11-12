# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# Experimental Linux x86-64 build: EmbCC kernel/probe, GCC POSIX host and linker.
# Set EMB_KERNEL_COMPILER to the installed EmbCC executable (ADR-041).
include("${CMAKE_CURRENT_LIST_DIR}/native-gcc.cmake")
set(EMB_NATIVE_MIXED_EMBCC ON)
list(APPEND EMB_TOOLCHAIN_LINK_FLAGS -no-pie)
