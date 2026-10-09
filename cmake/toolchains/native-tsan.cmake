# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Host build with ThreadSanitizer (SPEC-013 §7).
# GCC by default: its runtimes ship with the compiler; pass -DEMB_SANITIZER_CC=clang
# where libclang-rt is installed.
set(EMB_SANITIZER_CC "gcc" CACHE STRING "compiler for the sanitizer legs")
set(CMAKE_C_COMPILER ${EMB_SANITIZER_CC})
set(EMB_TOOLCHAIN_C_FLAGS -pthread -g -O1 -fsanitize=thread -fno-omit-frame-pointer)
set(EMB_TOOLCHAIN_LINK_FLAGS -pthread -fsanitize=thread)
set(EMB_TOOLCHAIN_DEFINES _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
