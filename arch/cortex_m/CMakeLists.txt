# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
add_library(emb_arch STATIC cm_port.c cm_vectors.c cm_services.c)
target_include_directories(emb_arch PUBLIC include)
target_link_libraries(emb_arch PUBLIC emb_api emb_board PRIVATE emb_flags INTERFACE emb_kernel)
add_dependencies(emb_arch emb_storage_h)
