# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# ATmega328P with Clang (ADR-053): Clang compiles for --target=avr against avr-libc's headers;
# avr-gcc's driver links, with GNU ld, avr-libc's startup and C library and avr-gcc's libgcc,
# as with avr-gcc. LLD links no AVR image here: avr-libc's and libgcc's objects carry the
# R_AVR_DIFF relocations GNU as writes for relaxation, which LLD does not implement. The device
# comes from -DEMB_AVR_MCU (default atmega328p). EMB_CLANG names the clang (default: clang on
# PATH); avr-gcc, with avr-libc, must be on PATH.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)
if(NOT CMAKE_C_COMPILER)
  if(DEFINED ENV{EMB_CLANG})
    set(CMAKE_C_COMPILER "$ENV{EMB_CLANG}")
  else()
    set(CMAKE_C_COMPILER clang)
  endif()
endif()
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(EMB_AVR_MCU "atmega328p" CACHE STRING "AVR device for -mmcu")
find_program(EMB_AVR_GCC avr-gcc REQUIRED)
find_program(CMAKE_OBJCOPY avr-objcopy REQUIRED)
find_program(CMAKE_SIZE avr-size REQUIRED)
find_program(CMAKE_NM avr-nm REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/gnu-libc-headers.cmake")
emb_gnu_libc_include("${EMB_AVR_GCC}" _emb_avr_inc -mmcu=${EMB_AVR_MCU}) # avr-libc's headers
set(EMB_TOOLCHAIN_C_FLAGS --target=avr -mmcu=${EMB_AVR_MCU} -Os -g -fno-short-enums -nostdlibinc
                          -isystem "${_emb_avr_inc}")
# the link: avr-gcc's driver, with the flags avr-gcc.cmake links with
set(CMAKE_C_LINK_EXECUTABLE
    "\"${EMB_AVR_GCC}\" <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
# -z noexecstack: avr-libc's and libgcc's assembly objects carry no .note.GNU-stack, which GNU ld
# warns about once Clang's objects are linked with them; the AVR has no executable-stack notion
set(EMB_TOOLCHAIN_LINK_FLAGS -mmcu=${EMB_AVR_MCU} -mrelax -Wl,--gc-sections -Wl,-z,noexecstack)
set(EMB_TOOLCHAIN_DEFINES)
set(EMB_TOOLCHAIN_NO_LTO "avr-clang links with avr-gcc's GNU ld, which cannot read Clang's LTO objects")
set(CMAKE_EXECUTABLE_SUFFIX ".elf")
