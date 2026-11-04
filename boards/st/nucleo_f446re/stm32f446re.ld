/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Linker script for the STM32F446RE: 512 KiB of flash at 0x08000000 (aliased at 0 with BOOT0
 * low, where the core reads its vector table), 128 KiB of SRAM at 0x20000000 (SRAM1 112 KiB
 * and SRAM2 16 KiB, contiguous). The kernel's registration tables are bracketed by name
 * (SPEC-001 §6.4, HW-006).
 */
ENTRY(embn_cm_reset)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > FLASH

  .text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(16); /* the table entries' own alignment (sizeof(void *) * 4) */
    __start_emb_init_table = .;
    KEEP(*(emb_init_table))
    __stop_emb_init_table = .;
    . = ALIGN(16);
    __start_emb_test_table = .;
    KEEP(*(emb_test_table))
    __stop_emb_test_table = .;
    . = ALIGN(8);
  } > FLASH

  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH

  __data_load = LOADADDR(.data);
  .data : ALIGN(8) {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(8);
    __data_end = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) : ALIGN(8) {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } > RAM

  end = .;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
