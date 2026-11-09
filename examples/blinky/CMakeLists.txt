# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Firat Hajiyev
cmake_minimum_required(VERSION 3.20)

set(EmbLinkRTOS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../cmake" CACHE PATH "The RTOS's cmake/ directory")
find_package(EmbLinkRTOS REQUIRED)

project(blinky LANGUAGES C)

emb_kernel()
emb_add_executable(blinky main.c)
