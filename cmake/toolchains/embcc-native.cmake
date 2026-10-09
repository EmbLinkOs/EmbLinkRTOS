# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# Host build with EmbCC (09 §2). Untested until EmbCC is available in CI; kept so the
# matrix leg exists (05 §2.2). EmbCC ignores -std= and compiles its single dialect (09 §3).
set(CMAKE_C_COMPILER embcc)
set(EMB_TOOLCHAIN_C_FLAGS -pthread -g -O1)
set(EMB_TOOLCHAIN_LINK_FLAGS -pthread)
set(EMB_TOOLCHAIN_DEFINES _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
