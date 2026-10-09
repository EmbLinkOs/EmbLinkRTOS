# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Compiler flags common to every target (CODING-STANDARD CS-13.1). Toolchain files add
# the target-specific flags (-mmcu, -Os) and EMB_TOOLCHAIN_EXTRA_WARNINGS.

set(EMB_C_STANDARD_FLAGS -std=c11 -pedantic -ffreestanding -fno-common
    -ffunction-sections -fdata-sections)
# -fstack-usage feeds the stack-sizing methodology (KRN-MEM-009); avr-gcc cannot
# compute it for naked functions (the context switch and the EMB_ISR vectors) and turns
# that into an error, so the AVR port's stack figures come from the call graph instead.
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "avr")
  list(APPEND EMB_C_STANDARD_FLAGS -fstack-usage)
endif()

set(EMB_WARNING_FLAGS
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wundef
    -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wvla
    -Wcast-align -Wcast-qual -Wdouble-promotion -Wformat=2 -Wswitch-default
    -Wimplicit-fallthrough -Wnull-dereference -Wredundant-decls -Wnested-externs)

if(EMB_WERROR)
  list(APPEND EMB_WARNING_FLAGS -Werror)
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
  # -Wnull-dereference is GCC-only in this form; Clang has it under the same name but
  # as part of -Wextra. Keep the list identical and drop what Clang refuses.
  list(APPEND EMB_WARNING_FLAGS -Wno-unknown-warning-option)
endif()

# A target gets the flags through this interface library.
add_library(emb_flags INTERFACE)
target_compile_options(emb_flags INTERFACE
  $<$<COMPILE_LANGUAGE:C>:${EMB_C_STANDARD_FLAGS}>
  $<$<COMPILE_LANGUAGE:C>:${EMB_WARNING_FLAGS}>
  ${EMB_TOOLCHAIN_C_FLAGS}
  ${EMB_BOARD_C_FLAGS})   # the board's CPU, FPU and ABI flags (board.cmake)
target_compile_definitions(emb_flags INTERFACE ${EMB_TOOLCHAIN_DEFINES})
target_include_directories(emb_flags INTERFACE
  "${EMB_GENERATED_DIR}/include")
target_link_options(emb_flags INTERFACE ${EMB_TOOLCHAIN_LINK_FLAGS} ${EMB_BOARD_C_FLAGS})
