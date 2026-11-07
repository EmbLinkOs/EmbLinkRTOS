# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
# emb_board for an STM32F4 board described in YAML (SPEC-014): the family's board support and
# drivers, compiled against the board's generated hw_config.h, with its generated pin table and
# linker script. EMB_BOARD_DIR is the board's generated/ directory.
add_library(emb_board STATIC
  board.c
  "${EMB_BASE}/drivers/st/stm32f4_rcc.c"
  "${EMB_BASE}/drivers/st/stm32f4_gpio.c"
  "${EMB_BASE}/drivers/st/stm32_usart.c"
  "${EMB_BASE}/drivers/st/stm32_tim.c"
  "${EMB_BASE}/drivers/st/stm32f4_adc.c"
  "${EMB_BASE}/drivers/st/stm32f4_iwdg.c"
  "${EMB_BOARD_DIR}/board_pins.c")
# the drivers' header is public: an application on the board drives its peripherals with them
target_include_directories(emb_board PUBLIC ${EMB_BOARD_INCLUDE_DIRS} "${EMB_BASE}/drivers/st/include")
target_link_libraries(emb_board PUBLIC emb_api PRIVATE emb_flags INTERFACE emb_arch) # emb_cm_halt
# The generated linker script brackets the kernel's flash tables (SPEC-012 §8).
set(EMB_BOARD_LINKER_SCRIPT "${EMB_BOARD_DIR}/regions.ld" CACHE INTERNAL "board linker script")
target_link_options(emb_board INTERFACE "-T${EMB_BOARD_LINKER_SCRIPT}")
