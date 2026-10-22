/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * What the RISC-V port's common part (rv_port.c) asks of the SoC's interrupt controller
 * and timer: the CLINT (rv_clint.c) or the ESP32-C3's interrupt matrix and SYSTIMER
 * (rv_esp32c3.c). The SoC file also defines the arch API's interrupt, timer and cycle
 * functions (emb_arch_irq_*, emb_arch_timer_*, emb_arch_cycles*).
 */
#ifndef EMB_RISCV_RV_SOC_H
#define EMB_RISCV_RV_SOC_H

#include <stdint.h>

#define EMBN_RV_REG32(a) (*(volatile uint32_t *)(uintptr_t)(a)) /* CODING-STANDARD CS-5.1 */

/* the trap entry (rv_switch.S) */
void embn_rv_trap_entry(void);

/* mtvec and what must happen before C runs on (interrupts are off) */
void embn_rv_soc_early_init(void);
/* the interrupt controller's state at the kernel's initialisation */
void embn_rv_soc_init(void);
/* an interrupt with this cause (mcause without its interrupt bit), between the kernel's
 * interrupt entry and exit */
void embn_rv_soc_irq(uint32_t cause);

#endif /* EMB_RISCV_RV_SOC_H */
