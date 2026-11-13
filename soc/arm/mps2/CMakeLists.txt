# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# emb_board for a MPS2 board described in YAML (SPEC-014): the board support and its console
# UART's driver, compiled against the board's generated hw_config.h, with its generated linker
# script, and the board's devices with the drivers they use (SPEC-016 §8). EMB_BOARD_DIR is
# the board's generated/ directory.
add_library(emb_board STATIC board.c "${EMB_BASE}/drivers/arm/cmsdk_uart.c" ${EMB_BOARD_DRIVER_SOURCES})
target_include_directories(emb_board PUBLIC ${EMB_BOARD_INCLUDE_DIRS} "${EMB_BASE}/drivers/arm/include"
                                             ${EMB_BOARD_DRIVER_INCLUDE_DIRS})
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_arch) # the semihosting exit
set(EMB_BOARD_LINKER_SCRIPT "${EMB_BOARD_DIR}/linker/regions.ld" CACHE INTERNAL "board linker script")
target_link_options(emb_board INTERFACE "-T${EMB_BOARD_LINKER_SCRIPT}")
