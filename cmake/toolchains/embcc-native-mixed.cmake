# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Experimental Linux x86-64 build: EmbCC kernel/probe, GCC POSIX host and linker.
# Set EMB_KERNEL_COMPILER to the installed EmbCC executable (ADR-041).
include("${CMAKE_CURRENT_LIST_DIR}/native-gcc.cmake")
set(EMB_NATIVE_MIXED_EMBCC ON)
list(APPEND EMB_TOOLCHAIN_LINK_FLAGS -no-pie)
