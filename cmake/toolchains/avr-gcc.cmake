# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# ATmega328P with avr-gcc (SPEC-012). The device comes from -DEMB_AVR_MCU (default atmega328p).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)
set(CMAKE_C_COMPILER avr-gcc)
set(CMAKE_CXX_COMPILER avr-g++)
set(CMAKE_OBJCOPY avr-objcopy)
set(CMAKE_SIZE avr-size)
set(CMAKE_NM avr-nm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_AVR_MCU "atmega328p" CACHE STRING "AVR device for -mmcu")
set(EMB_TOOLCHAIN_C_FLAGS -mmcu=${EMB_AVR_MCU} -Os -g -fno-short-enums)
set(EMB_TOOLCHAIN_LINK_FLAGS -mmcu=${EMB_AVR_MCU} -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
