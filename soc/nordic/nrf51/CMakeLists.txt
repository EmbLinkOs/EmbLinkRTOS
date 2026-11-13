# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# emb_board for an nRF51 board described in YAML (A86): the family's board support, compiled
# against the board's generated hw_config.h, with its generated linker script.
# EMB_BOARD_DIR is the board's generated/ directory (A126: src/, linker/).
add_library(emb_board STATIC board.c ${EMB_BOARD_DRIVER_SOURCES}) # the board's devices (SPEC-016 §8)
target_include_directories(emb_board PUBLIC ${EMB_BOARD_INCLUDE_DIRS} ${EMB_BOARD_DRIVER_INCLUDE_DIRS})
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_arch) # semihosting
set(EMB_BOARD_LINKER_SCRIPT "${EMB_BOARD_DIR}/linker/regions.ld" CACHE INTERNAL "board linker script")
target_link_options(emb_board INTERFACE "-T${EMB_BOARD_LINKER_SCRIPT}")
