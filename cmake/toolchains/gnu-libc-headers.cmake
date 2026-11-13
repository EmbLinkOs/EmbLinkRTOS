# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# The C library's headers of a GNU toolchain, for a Clang toolchain file that compiles against
# that library (arm-none-eabi-clang, riscv-clang): the first directory of the GCC driver's
# #include <...> search list that holds stdio.h. GCC's own directories (stddef.h, include-fixed)
# come before it and hold no stdio.h; Clang brings its own. It works wherever the toolchain is
# installed: the Arm GNU toolchain's arm-none-eabi/include, Ubuntu's /usr/lib/arm-none-eabi/include,
# picolibc's include directory named by picolibc.specs.
#   emb_gnu_libc_include(<gcc> <out-var> [driver flags...])
function(emb_gnu_libc_include gcc out)
  execute_process(COMMAND "${gcc}" ${ARGN} -E -v -x c /dev/null -o /dev/null
                  ERROR_VARIABLE _search OUTPUT_QUIET RESULT_VARIABLE _rc)
  string(REGEX MATCH "#include <\\.\\.\\.> search starts here:\n(.*)\nEnd of search list" _m "${_search}")
  string(REPLACE "\n" ";" _dirs "${CMAKE_MATCH_1}")
  foreach(_dir ${_dirs})
    string(STRIP "${_dir}" _dir)
    if(EXISTS "${_dir}/stdio.h")
      get_filename_component(_dir "${_dir}" REALPATH)
      set(${out} "${_dir}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "${gcc} ${ARGN}: no C library headers (stdio.h) in its search list")
endfunction()
