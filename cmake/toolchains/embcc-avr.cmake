# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# ATmega328P with EmbCC and embld (09 §7). Untested until EmbCC is available in CI.
# Current embld supports AVR linker scripts. This file retains the compatibility
# option-set path of SPEC-012 §8; generated inputs and RTOS qualification remain
# planned (HW-006). Compiler capability alone does not wire storage/flash macros.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)
set(CMAKE_C_COMPILER embcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_AVR_MCU "atmega328p" CACHE STRING "AVR device")
set(EMB_TOOLCHAIN_C_FLAGS --target=avr -mmcu=${EMB_AVR_MCU} -Os -g)
set(EMB_TOOLCHAIN_LINK_FLAGS --target=avr -mmcu=${EMB_AVR_MCU} -Wl,-Ttext=0x0 -Wl,-Tdata=0x800100 -Wl,--rom-limit=32256 -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
