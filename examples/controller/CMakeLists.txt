# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# The M3a reference controller (docs/adoption.md) on the NUCLEO-F446RE: an application outside
# the RTOS's tree, built through find_package as examples/hello_app is.
#
#   cmake -S examples/controller -B build/controller -G Ninja -DEMB_BOARD=nucleo_f446re
#   cmake --build build/controller
#   python3 tools/hil/controller_session.py --port /dev/cu.usbmodem* \
#           --elf build/controller/controller.elf --out <record directory>
#
# docs/engineering/controller-nucleo-2026-11-07.md describes it and its run on the board.
cmake_minimum_required(VERSION 3.20)

set(EmbLinkRTOS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../cmake" CACHE PATH "The RTOS's cmake/ directory")
set(EMB_BOARD "nucleo_f446re" CACHE STRING "The controller's board: an STM32F446 board")
# a fault reboots the part with its crash record kept (controller.conf)
set(EMB_CONF "${CMAKE_CURRENT_LIST_DIR}/controller.conf" CACHE STRING "Configuration fragments")
# its threads and objects (system.yaml, generated into generated/<board>/), and the roots the
# stack check after the link needs (A125)
set(EMB_SYSTEM "${CMAKE_CURRENT_LIST_DIR}/system.yaml" CACHE FILEPATH "The controller's system description")
set(EMB_STACK_ROOTS "${CMAKE_CURRENT_LIST_DIR}/stack-roots.json" CACHE FILEPATH "Its stack check's roots")
find_package(EmbLinkRTOS REQUIRED) # before project(): picks the board's toolchain

project(controller LANGUAGES C)

emb_kernel()
if(NOT EMB_BOARD MATCHES "^nucleo_f446")
  message(FATAL_ERROR "the controller drives an STM32F446's TIM2, ADC1, USART2 and IWDG: EMB_BOARD "
                      "nucleo_f446re or nucleo_f446ze, not ${EMB_BOARD}")
endif()
emb_add_executable(controller main.c)
