# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# The one source both kernels build (tests/benchmarks/bench_compare.c).
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../tests/benchmarks/bench_compare.c"
                       REQUIRES freertos esp_hw_support esp_rom soc)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BENCH_FREERTOS)
