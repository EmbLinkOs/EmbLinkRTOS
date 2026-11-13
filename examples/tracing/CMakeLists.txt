# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

cmake_minimum_required(VERSION 3.20)

set(EmbLinkRTOS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../cmake" CACHE PATH "The RTOS's cmake/ directory")
# the kernel's trace points and the framed console, which carries the trace on channel 1 (A128)
set(EMB_SET "EMB_TRACE=y;EMB_CONSOLE_FRAMED=y" CACHE STRING "Symbol overrides")
find_package(EmbLinkRTOS REQUIRED)

project(tracing LANGUAGES C)

emb_kernel()
emb_add_executable(tracing main.c)
