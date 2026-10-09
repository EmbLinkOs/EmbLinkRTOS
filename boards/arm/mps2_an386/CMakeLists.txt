# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
add_library(emb_board STATIC ../mps2_common/board.c)
target_include_directories(emb_board PUBLIC include ../mps2_common/include)
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_arch) # the semihosting exit
set(EMB_BOARD_LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../mps2_common/mps2.ld" CACHE INTERNAL "board linker script")
target_link_options(emb_board INTERFACE "-T${EMB_BOARD_LINKER_SCRIPT}")
