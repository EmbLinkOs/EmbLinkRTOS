# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

add_library(emb_arch STATIC native_port.c)
target_include_directories(emb_arch PUBLIC include)
target_link_libraries(emb_arch PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_kernel)
find_package(Threads REQUIRED)
target_link_libraries(emb_arch PUBLIC Threads::Threads)
add_dependencies(emb_arch emb_storage_h)
