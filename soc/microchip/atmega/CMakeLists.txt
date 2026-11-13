# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# emb_board for an ATmega board described in YAML (SPEC-014): the family's board support and
# drivers, compiled against the board's generated hw_config.h, with its generated linker script.
# EMB_BOARD_DIR is the board's generated/ directory (A126: src/, linker/; an application's
# src/config/<board>/, A130: EMB_BOARD_SRC_DIR, EMB_BOARD_LINKER_DIR).
add_library(emb_board STATIC board.c ${EMB_BOARD_DRIVER_SOURCES}) # its drivers are inline (drivers/microchip/include)
target_include_directories(emb_board PUBLIC ${EMB_BOARD_INCLUDE_DIRS} ${EMB_BOARD_DRIVER_INCLUDE_DIRS}
  PRIVATE "${EMB_BASE}/drivers/microchip/include")
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags)
# The generated linker script brackets the kernel's flash tables (SPEC-012 §8).
set(EMB_BOARD_LINKER_SCRIPT "${EMB_BOARD_LINKER_DIR}/regions.ld" CACHE INTERNAL "board linker script")
target_link_options(emb_board INTERFACE "-T${EMB_BOARD_LINKER_SCRIPT}")
