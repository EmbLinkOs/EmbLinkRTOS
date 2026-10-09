# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Host build with AddressSanitizer and UndefinedBehaviorSanitizer (SPEC-013 §7).
# ThreadSanitizer is a separate leg (native-tsan) because it cannot combine with ASan.
# GCC by default: its runtimes ship with the compiler; pass -DEMB_SANITIZER_CC=clang
# where libclang-rt is installed.
set(EMB_SANITIZER_CC "gcc" CACHE STRING "compiler for the sanitizer legs")
set(CMAKE_C_COMPILER ${EMB_SANITIZER_CC})
set(EMB_TOOLCHAIN_C_FLAGS -pthread -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
set(EMB_TOOLCHAIN_LINK_FLAGS -pthread -fsanitize=address,undefined)
set(EMB_TOOLCHAIN_DEFINES _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
