/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Linker script for the ESP32-C3 booted by its ROM from an image at flash offset 0
 * (tools/esp/esp_image.py): the ROM copies each segment into internal SRAM and jumps to
 * the entry, so everything runs from RAM and .data needs no copy. SRAM1 is seen at
 * 0x40380000 by the instruction bus and at 0x3FC80000 by the data bus (TRM 3.3.2): code
 * takes the first 128 KB through the instruction bus (0x3FC80000-0x3FC9FFFF on the data
 * bus), data the next 192 KB through the data bus, up to 0x3FCD0000, below what the ROM keeps for itself at the top of SRAM
 * (from 0x3FCDC710). The kernel's registration tables are bracketed by name
 * (SPEC-001 §6.4, HW-006).
 */
OUTPUT_ARCH(riscv)
ENTRY(embn_rv_reset)

MEMORY
{
  IRAM (rx)  : ORIGIN = 0x40380000, LENGTH = 128K
  DRAM (rw)  : ORIGIN = 0x3FCA0000, LENGTH = 192K
}

SECTIONS
{
  .text : {
    KEEP(*(.text.embn_rv_vectors))
    KEEP(*(.text.embn_rv_reset))
    *(.text .text.*)
  } > IRAM

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
  } > DRAM

  .data : ALIGN(16) {
    *(.data .data.* .sdata .sdata.*)
  } > DRAM

  /* the C library's thread-local block (errno in picolibc), one for every thread: .tdata in
     place, .tbss reserved after it, which the reset clears; tp points at it (A88) */
  /* rv_switch.S puts a word in each, so both exist and the TLS segment starts at .tdata */
  .tdata : ALIGN(16) { KEEP(*(.tdata.emb_tls_anchor)) *(.tdata .tdata.* .gnu.linkonce.td.*) } > DRAM
  .tbss : { KEEP(*(.tbss.emb_tls_anchor)) *(.tbss .tbss.* .gnu.linkonce.tb.* .tcommon) } > DRAM
  __emb_tls_start = ADDR(.tdata);
  __emb_tbss_start = ADDR(.tdata) + SIZEOF(.tdata);
  __emb_tls_end = ADDR(.tbss) + SIZEOF(.tbss);

  /* placed past .tbss, which takes no room of its own */
  .bss ALIGN(__emb_tls_end, 16) (NOLOAD) : {
    __bss_start = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > DRAM

  end = .;
  /* the crash record's memory (SPEC-015 §4, A73): above the stack and outside every loaded
     segment, so neither startup nor a loader's reset clears it */
  __emb_retained_end = ORIGIN(DRAM) + LENGTH(DRAM);
  __emb_retained_start = __emb_retained_end - 192;
  __stack_top = __emb_retained_start;
}
