# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# ATmega328P with EmbCC and embld (09 §7). Untested until EmbCC is available in CI.
# embld takes no linker script on AVR: the region model is passed as the option set of
# SPEC-012 §8 (-Ttext/-Tdata/--rom-limit), which the generator will emit in M3 (HW-006).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)
set(CMAKE_C_COMPILER embcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_AVR_MCU "atmega328p" CACHE STRING "AVR device")
set(EMB_TOOLCHAIN_C_FLAGS --target=avr -mmcu=${EMB_AVR_MCU} -Os -g)
set(EMB_TOOLCHAIN_LINK_FLAGS --target=avr -mmcu=${EMB_AVR_MCU} -Wl,-Ttext=0x0 -Wl,-Tdata=0x800100 -Wl,--rom-limit=32256 -Wl,--gc-sections)
set(EMB_TOOLCHAIN_DEFINES)
