# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# The M3a reference controller (docs/adoption.md) on the NUCLEO-F446RE: an application outside
# the RTOS's tree, built through find_package as examples/hello_app is.
#
#   cmake -S examples/controller -B build/controller -G Ninja -DEMB_BOARD=nucleo_f446re
#   cmake --build build/controller
#   python3 tools/hil/controller_session.py build/controller/controller.elf --port /dev/cu.usbmodem*
cmake_minimum_required(VERSION 3.20)

set(EmbLinkRTOS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../cmake" CACHE PATH "The RTOS's cmake/ directory")
set(EMB_BOARD "nucleo_f446re" CACHE STRING "The controller's board: an STM32F446 board")
# a fault reboots the part with its crash record kept (controller.conf)
set(EMB_CONF "${CMAKE_CURRENT_LIST_DIR}/controller.conf" CACHE STRING "Configuration fragments")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../../cmake/toolchains/arm-none-eabi-gcc.cmake")
endif()
find_package(EmbLinkRTOS REQUIRED) # before project(): picks the board's toolchain

project(controller LANGUAGES C)

emb_kernel()
if(NOT EMB_BOARD MATCHES "^nucleo_f446")
  message(FATAL_ERROR "the controller drives an STM32F446's TIM2, ADC1, USART2 and IWDG: EMB_BOARD "
                      "nucleo_f446re or nucleo_f446ze, not ${EMB_BOARD}")
endif()
emb_add_executable(controller main.c)
