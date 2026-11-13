/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    esp32.ld
 * @brief   Linker script for the classic ESP32 booted by its ROM from an image at flash offset
 *          0x1000 (tools/esp/esp_image.py --chip esp32).
 *
 * @details
 * The ROM copies each segment to its run address and calls the entry, so everything runs from
 * internal SRAM and .data needs no copy (ADR-052).
 *
 *   - code in SRAM0's instruction side from 0x40080400, the port's vector table first, at the
 *     1 KB boundary VECBASE needs; the instruction RAM takes only 32-bit accesses, so nothing
 *     a byte load reads may go there;
 *   - .rodata, .data, .bss and the stack in SRAM2's data side, 0x3FFB0000-0x3FFE0000, clear
 *     of the 8 KB below it and of SRAM1 from 0x3FFE0000, which the ROM keeps for its own data
 *     and stack while it loads the image.
 *
 * The kernel's registration tables are bracketed by name (SPEC-001 §6.4, HW-006).
 *
 * @author  Junior Deogracias
 * @date    2026-11-05 (created)
 */
ENTRY(call_start_cpu0)

MEMORY
{
  IRAM (rx) : ORIGIN = 0x40080400, LENGTH = 0x1FC00
  DRAM (rw) : ORIGIN = 0x3FFB0000, LENGTH = 0x30000
}

SECTIONS
{
  .text : {
    KEEP(*(.text.embn_xt_vectors))
    *(.literal .literal.* .text .text.*)
  } > IRAM

  .rodata : ALIGN(16) {
    *(.rodata .rodata.*)
    . = ALIGN(16); /* the table entries' own alignment (sizeof(void *) * 4) */
    __start_emb_init_table = .;
    KEEP(*(emb_init_table))
    __stop_emb_init_table = .;
    . = ALIGN(16);
    __start_emb_test_table = .;
    KEEP(*(emb_test_table))
    __stop_emb_test_table = .;
  } > DRAM

  .data : ALIGN(16) {
    *(.data .data.*)
  } > DRAM

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > DRAM

  end = .;
  /* the crash record's memory (SPEC-015 §4, A73): above the stack and outside every loaded
     segment, so neither startup nor the ROM's load clears it */
  __emb_retained_end = ORIGIN(DRAM) + LENGTH(DRAM);
  __emb_retained_start = __emb_retained_end - 192;
  /* below it, 32 bytes the test framework keeps across the resets of its fault checks (A89) */
  __emb_test_retained_end = __emb_retained_start;
  __emb_test_retained_start = __emb_test_retained_end - 32;
  __stack_top = __emb_test_retained_start;
}
