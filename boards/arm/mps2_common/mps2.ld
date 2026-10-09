/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Linker script for the MPS2 FPGA images AN385 (Cortex-M3) and AN386 (Cortex-M4) as QEMU
 * models them: code in ZBT SSRAM1 at 0x00000000, data in ZBT SSRAM2/3 at 0x20000000. The
 * kernel's registration tables are bracketed by name (SPEC-001 §6.4, HW-006).
 */
ENTRY(embn_cm_reset)

MEMORY
{
  CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > CODE

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
  } > CODE

  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > CODE

  __data_load = LOADADDR(.data);
  .data : ALIGN(8) {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(8);
    __data_end = .;
  } > RAM AT > CODE

  .bss (NOLOAD) : ALIGN(8) {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } > RAM

  end = .;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
