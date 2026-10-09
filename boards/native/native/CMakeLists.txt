# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
add_library(emb_board STATIC board.c)
target_include_directories(emb_board PUBLIC include)
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags)
