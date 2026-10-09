/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Hardware configuration of mps2_an386 (Cortex-M4F, QEMU machine mps2-an386). Hand-written from
 * QEMU's model and the AN386 application note; emb-hwgen generates it from M3 (SPEC-014). */
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#define EMB_HW_BOARD      "mps2_an386"
#define EMB_HW_SOC        "Cortex-M4F"
#define EMB_HW_CPU_HZ     25000000u
#define EMB_HW_UART0_BASE 0x40004000u
#define EMB_HW_TEST_IRQ   30u /* a line no device drives: the test framework's software interrupt */

#endif /* HW_CONFIG_H */
