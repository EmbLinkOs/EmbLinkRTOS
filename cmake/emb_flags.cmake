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

# A Clang toolchain that links with LLD against a GNU toolchain's C library and libgcc
# (cmake/toolchains/*-clang.cmake): the GNU driver names the multilib directory for the board's
# CPU flags, which only now are known (board.cmake). EMB_TOOLCHAIN_GCC_LIBS is that driver,
# EMB_TOOLCHAIN_GCC_LIBS_FLAGS its own flags, EMB_TOOLCHAIN_GCC_LIBS_NAMES the libraries.
if(EMB_TOOLCHAIN_GCC_LIBS)
  set(_emb_libdirs)
  foreach(_emb_lib libgcc.a ${EMB_TOOLCHAIN_GCC_LIBS_NAMES})
    execute_process(COMMAND "${EMB_TOOLCHAIN_GCC_LIBS}" ${EMB_TOOLCHAIN_GCC_LIBS_FLAGS} ${EMB_BOARD_C_FLAGS}
                            -print-file-name=${_emb_lib}
                    OUTPUT_VARIABLE _emb_libpath OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT IS_ABSOLUTE "${_emb_libpath}")
      message(FATAL_ERROR "${EMB_TOOLCHAIN_GCC_LIBS} has no ${_emb_lib} for: ${EMB_BOARD_C_FLAGS}")
    endif()
    get_filename_component(_emb_libdir "${_emb_libpath}" DIRECTORY)
    list(APPEND _emb_libdirs "-L${_emb_libdir}")
  endforeach()
  list(REMOVE_DUPLICATES _emb_libdirs)
  set(_emb_libflags)
  foreach(_emb_lib ${EMB_TOOLCHAIN_GCC_LIBS_NAMES} libgcc.a)
    string(REGEX REPLACE "^lib(.*)\\.a$" "-l\\1" _emb_flag "${_emb_lib}")
    list(APPEND _emb_libflags "${_emb_flag}")
  endforeach()
  target_link_options(emb_flags INTERFACE ${_emb_libdirs} ${_emb_libflags})
endif()
