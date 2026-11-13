# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

# emb_board for an RP2350 board described in YAML (A79): the family's board support over the
# RP2350 drivers and the PL011's, compiled against the board's generated hw_config.h, with its
# generated pin table and linker script, and the board's devices with the drivers they use
# (SPEC-016 §8). EMB_BOARD_DIR is the board's generated/ directory (A126: src/, linker/).
add_library(emb_board STATIC board.c
  "${EMB_BASE}/drivers/rpi/rp2350_clocks.c"
  "${EMB_BASE}/drivers/rpi/rp2350_gpio.c"
  "${EMB_BASE}/drivers/arm/pl011.c"
  "${EMB_BOARD_DIR}/src/board_pins.c"
  "${EMB_BOARD_DIR}/src/boot_block.c"
  ${EMB_BOARD_DRIVER_SOURCES})
target_include_directories(emb_board PUBLIC ${EMB_BOARD_INCLUDE_DIRS} "${EMB_BASE}/drivers/rpi/include"
                                             "${EMB_BASE}/drivers/arm/include" ${EMB_BOARD_DRIVER_INCLUDE_DIRS})
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_arch) # emb_cm_halt
set(EMB_BOARD_LINKER_SCRIPT "${EMB_BOARD_DIR}/linker/regions.ld" CACHE INTERNAL "board linker script")
target_link_options(emb_board INTERFACE "-T${EMB_BOARD_LINKER_SCRIPT}")
# a test image on the board reboots into BOOTSEL when it ends, for picotool's next load
if(EMB_TEST_EMULATOR STREQUAL "hil")
  target_compile_definitions(emb_board PRIVATE EMB_RP2350_EXIT_TO_BOOTSEL=1)
endif()
