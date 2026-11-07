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
# -print-file-name searches the driver's own directories, not the -L directories a specs file
# adds to the link (Ubuntu's picolibc.specs), so those come from the link command (-###).
if(EMB_TOOLCHAIN_GCC_LIBS)
  execute_process(COMMAND "${EMB_TOOLCHAIN_GCC_LIBS}" ${EMB_TOOLCHAIN_GCC_LIBS_FLAGS} ${EMB_BOARD_C_FLAGS}
                          "-###" -nostartfiles -o emb-probe.elf emb-probe.o
                  ERROR_VARIABLE _emb_linkcmd OUTPUT_QUIET)
  string(REGEX MATCHALL "[ \"]-L[^ \"\n]+" _emb_specdirs "${_emb_linkcmd}")
  list(TRANSFORM _emb_specdirs REPLACE "^[ \"]-L" "")
  set(_emb_libdirs)
  foreach(_emb_lib libgcc.a ${EMB_TOOLCHAIN_GCC_LIBS_NAMES})
    execute_process(COMMAND "${EMB_TOOLCHAIN_GCC_LIBS}" ${EMB_TOOLCHAIN_GCC_LIBS_FLAGS} ${EMB_BOARD_C_FLAGS}
                            -print-file-name=${_emb_lib}
                    OUTPUT_VARIABLE _emb_libpath OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT IS_ABSOLUTE "${_emb_libpath}")
      foreach(_emb_dir ${_emb_specdirs})
        if(EXISTS "${_emb_dir}/${_emb_lib}")
          set(_emb_libpath "${_emb_dir}/${_emb_lib}")
          break()
        endif()
      endforeach()
    endif()
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

# Link-time optimization (ADR-056): the kernel, the port, the board and the application
# optimized as one program, so the kernel's small functions inline across its files and into
# the caller. GCC and Clang only; a toolchain file whose link cannot read the compiler's LTO
# objects sets EMB_TOOLCHAIN_NO_LTO (avr-clang: GNU ld links Clang's objects). CMake's own IPO
# support picks the flags and the LTO-aware archiver (gcc-ar, llvm-ar). Every target made after
# this point inherits it; the layout probe opts out (include/CMakeLists.txt).
option(EMB_LTO "Link-time optimization of the whole image (ADR-056; GCC and Clang)" OFF)
if(EMB_LTO)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR "EMB_LTO: the ${CMAKE_C_COMPILER_ID} compiler has no link-time optimization "
                        "this build supports (GCC and Clang do)")
  endif()
  if(EMB_TOOLCHAIN_NO_LTO)
    message(FATAL_ERROR "EMB_LTO: ${EMB_TOOLCHAIN_NO_LTO}")
  endif()
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND (NOT CMAKE_C_COMPILER_AR OR NOT CMAKE_C_COMPILER_RANLIB))
    message(FATAL_ERROR "EMB_LTO: no gcc-ar and gcc-ranlib beside ${CMAKE_C_COMPILER}")
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  # the kernel's sources ask for it where a layout trick does not survive LTO (EMBK_IDLE_SLIM)
  target_compile_definitions(emb_flags INTERFACE EMB_LTO=1)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # A kernel call during which another thread or an interrupt handler may run must stay a
    # barrier for every variable they share. Without LTO it is one: an opaque call may reach any
    # function whose address was taken. With LTO GCC sees the whole call graph, but not the
    # switch (PendSV, an interrupt), and its ipa-reference pass concludes that a static only
    # those functions write survives the call unchanged; inline asm "memory" clobbers do not
    # cover a static whose address is never taken. test_smoke, test_timer and test_irq failed
    # that way. -fno-ipa-reference keeps the call a barrier.
    target_compile_options(emb_flags INTERFACE -fno-ipa-reference)
  endif()
endif()

