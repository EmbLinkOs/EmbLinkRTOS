# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# The framed console's soak (M4.3): logs and the trace's stream together, for tools/hil/log_soak.py.
cmake_minimum_required(VERSION 3.20)

set(EmbLinkRTOS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../cmake" CACHE PATH "The RTOS's cmake/ directory")
set(EMB_SET "EMB_CONSOLE_FRAMED=y;EMB_TRACE=y;EMB_TRACE_STREAM=y;EMB_TRACE_STREAM_MS=500" CACHE STRING
    "Symbol overrides")
find_package(EmbLinkRTOS REQUIRED)

project(log_soak LANGUAGES C)

emb_kernel()
emb_add_executable(log_soak main.c)
