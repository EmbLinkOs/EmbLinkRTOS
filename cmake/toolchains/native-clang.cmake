# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Host build with Clang (SPEC-013 §11).
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(EMB_TOOLCHAIN_C_FLAGS -pthread -g -O1)
set(EMB_TOOLCHAIN_LINK_FLAGS -pthread)
set(EMB_TOOLCHAIN_DEFINES _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
