/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Hardware configuration of riscv32_virt (QEMU virt, RV32IMAC). Hand-written from QEMU's
 * model; emb-hwgen generates it from M3 (SPEC-014). */
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#define EMB_HW_BOARD       "riscv32_virt"
#define EMB_HW_SOC         "qemu-virt-rv32"
#define EMB_HW_UART0_BASE  0x10000000u /* ns16550a */
#define EMB_HW_TEST_DEVICE 0x00100000u /* sifive_test: run exit */
#define EMB_HW_TEST_IRQ    0u          /* the machine software interrupt (CLINT msip) */

#endif /* HW_CONFIG_H */
