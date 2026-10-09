/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * RISC-V port header, RV32 machine mode (contract SPEC-011 §3, §6; docs/ports/riscv.md):
 * the critical-section primitives, the handler declaration macros, and the port's types.
 * The critical section clears mstatus.MIE; the key is the previous MIE bit.
 */
#ifndef EMB_RISCV_EMB_ARCH_H
#define EMB_RISCV_EMB_ARCH_H

#include <emb/compiler.h>
#include <emb/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int emb_irq_key_t;    /* mstatus & MIE before the lock */
typedef uint32_t emb_arch_timer_raw_t; /* mtime, low word */

#define EMB_ARCH_TCB_EXTENSION 0
#define EMB_ARCH_CLOCK_SEQLOCK 0 /* the 64-bit clock is read inside the critical section */
#define EMB_ARCH_IRQ_COUNT     CONFIG_EMB_RV_IRQ_COUNT
#define EMB_RV_IRQ_SOFT        ((emb_irq_t)0u) /* the machine software interrupt (CLINT msip) */

#define EMBN_RV_MSTATUS_MIE 0x8u

static EMB_ALWAYS_INLINE emb_irq_key_t emb_arch_irq_lock(void)
{
    emb_irq_key_t old;
    __asm__ __volatile__("csrrci %0, mstatus, 8" : "=r"(old) : : "memory");
    return old & EMBN_RV_MSTATUS_MIE;
}

static EMB_ALWAYS_INLINE void emb_arch_irq_unlock(emb_irq_key_t key)
{
    __asm__ __volatile__("csrs mstatus, %0" : : "r"(key) : "memory");
}

static EMB_ALWAYS_INLINE bool emb_arch_irq_locked(void)
{
    emb_irq_key_t s;
    __asm__ __volatile__("csrr %0, mstatus" : "=r"(s));
    return (s & EMBN_RV_MSTATUS_MIE) == 0u;
}

/* Kernel-aware handlers are plain functions bound with emb_irq_connect(); the trap entry
 * brackets them with the kernel's interrupt entry and exit (CONFIG_EMB_IRQ_DYNAMIC). */
#define EMB_ISR(name)     static void name(void *emb_isr_arg_)
#define EMB_ISR_RAW(name) static void name(void *emb_isr_arg_)

#ifdef __cplusplus
}
#endif

#endif /* EMB_RISCV_EMB_ARCH_H */
