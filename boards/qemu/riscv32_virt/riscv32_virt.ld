/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Linker script for QEMU's RISC-V virt machine started with -bios none: QEMU loads the
 * image into RAM at 0x80000000 and starts the hart there in machine mode. Everything is in
 * RAM, so .data needs no copy. The kernel's registration tables are bracketed by name
 * (SPEC-001 §6.4, HW-006).
 */
OUTPUT_ARCH(riscv)
ENTRY(embn_rv_reset)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16M
}

SECTIONS
{
  .text : {
    KEEP(*(.text.reset))
    *(.text .text.*)
  } > RAM

  .rodata : ALIGN(16) {
    *(.rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(16); /* the table entries' own alignment (sizeof(void *) * 4) */
    __start_emb_init_table = .;
    KEEP(*(emb_init_table))
    __stop_emb_init_table = .;
    . = ALIGN(16);
    __start_emb_test_table = .;
    KEEP(*(emb_test_table))
    __stop_emb_test_table = .;
  } > RAM

  .data : ALIGN(16) {
    *(.data .data.* .sdata .sdata.*)
  } > RAM

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > RAM

  end = .;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
