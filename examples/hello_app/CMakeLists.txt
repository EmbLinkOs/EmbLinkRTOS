# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# An application kept outside the RTOS tree (docs/user/application.md). Your own project
# starts from these five lines and sets EmbLinkRTOS_DIR to the RTOS's cmake/ directory, for
# example with -DEmbLinkRTOS_DIR=$HOME/EmbLinkRTOS/cmake.
cmake_minimum_required(VERSION 3.20)

# this example sits in the RTOS's tree, so it finds the package next door
set(EmbLinkRTOS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../cmake" CACHE PATH "The RTOS's cmake/ directory")
find_package(EmbLinkRTOS REQUIRED) # before project(): picks the board's toolchain

project(hello_app LANGUAGES C)

emb_kernel()                       # the kernel, the port and the board, for EMB_BOARD
emb_add_executable(hello main.c)   # also run_hello where the board has an emulator
