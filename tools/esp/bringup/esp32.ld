/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The ESP32 bring-up probe's layout (ADR-052, EmbCC D-024): code in the instruction RAM, and
 * everything a byte load reads (.rodata, .data) in the data RAM, since the IRAM takes only
 * 32-bit accesses. The boot ROM loads each segment of the flash image at its own address, so
 * nothing is copied at start. The regions are ESP-IDF's bootloader.ld iram_seg and dram_seg,
 * which the ROM loads into without overwriting itself.
 */
ENTRY(call_start_cpu0)
MEMORY
{
  IRAM (rx) : ORIGIN = 0x40080400, LENGTH = 0xFC00
  DRAM (rw) : ORIGIN = 0x3FFF0000, LENGTH = 0x6000
}
SECTIONS
{
  .text : { *(.literal .text .literal.* .text.*) } > IRAM
  .rodata : { *(.rodata .rodata.*) } > DRAM
  .data : { *(.data .data.*) } > DRAM
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) } > DRAM
}
