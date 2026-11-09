# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# emb_board for an RP2350 board described in YAML (A79): the family's board support, compiled
# against the board's generated hw_config.h, with its generated pin table and linker script.
# EMB_BOARD_DIR is the board's generated/ directory.
add_library(emb_board STATIC board.c "${EMB_BOARD_DIR}/board_pins.c" "${EMB_BOARD_DIR}/boot_block.c"
                             ${EMB_BOARD_DRIVER_SOURCES}) # the board's devices (SPEC-016 §8)
target_include_directories(emb_board PUBLIC ${EMB_BOARD_INCLUDE_DIRS} ${EMB_BOARD_DRIVER_INCLUDE_DIRS})
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_arch) # emb_cm_halt
set(EMB_BOARD_LINKER_SCRIPT "${EMB_BOARD_DIR}/regions.ld" CACHE INTERNAL "board linker script")
target_link_options(emb_board INTERFACE "-T${EMB_BOARD_LINKER_SCRIPT}")
# a test image on the board reboots into BOOTSEL when it ends, for picotool's next load
if(EMB_TEST_EMULATOR STREQUAL "hil")
  target_compile_definitions(emb_board PRIVATE EMB_RP2350_EXIT_TO_BOOTSEL=1)
endif()
