# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
add_library(emb_arch STATIC native_port.c)
target_include_directories(emb_arch PUBLIC include)
target_link_libraries(emb_arch PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_kernel)
find_package(Threads REQUIRED)
target_link_libraries(emb_arch PUBLIC Threads::Threads)
add_dependencies(emb_arch emb_storage_h)
