# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
enable_language(ASM)
add_library(emb_arch STATIC rv_port.c rv_switch.S)
target_include_directories(emb_arch PUBLIC include)
target_link_libraries(emb_arch PUBLIC emb_api emb_board PRIVATE emb_flags INTERFACE emb_kernel)
add_dependencies(emb_arch emb_storage_h)
