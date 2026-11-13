# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# bench_compare as an ESP-IDF application: FreeRTOS on the ESP32 as ESP32 projects run it
# (tests/benchmarks/bench_compare.c, -DBENCH_FREERTOS). Built by build.sh beside this file.
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bench_compare_freertos)
