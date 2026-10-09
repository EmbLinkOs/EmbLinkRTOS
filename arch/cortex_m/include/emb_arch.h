/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Cortex-M port header (contract SPEC-011 §3, §6; docs/ports/cortex_m.md): the
 * critical-section primitives, the handler declaration macros, and the port's types.
 *
 * The kernel's critical section raises BASEPRI to CONFIG_EMB_CM_KERNEL_BASEPRI, so an
 * interrupt with a higher priority (a smaller priority byte) is never masked by the
 * kernel: a zero-latency interrupt that may not call it (SPEC-002 §3). The key is the
 * previous BASEPRI. Armv6-M has no BASEPRI and will use PRIMASK (M3-B).
 */
#ifndef EMB_CORTEX_M_EMB_ARCH_H
#define EMB_CORTEX_M_EMB_ARCH_H

#include <emb/compiler.h>
#include <emb/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int emb_irq_key_t;    /* the previous BASEPRI */
typedef uint32_t emb_arch_timer_raw_t; /* SysTick counts */

#define EMB_ARCH_TCB_EXTENSION 0
#define EMB_ARCH_CLOCK_SEQLOCK 0 /* the 64-bit clock is read inside the critical section */
#define EMB_ARCH_IRQ_COUNT     CONFIG_EMB_CM_IRQ_COUNT

#if !defined(__ARM_ARCH_7M__) && !defined(__ARM_ARCH_7EM__)
#error "this port builds for Armv7-M and Armv7E-M; Armv6-M and Armv8-M are the M3-B variants"
#endif

static EMB_ALWAYS_INLINE emb_irq_key_t emb_arch_irq_lock(void)
{
    emb_irq_key_t key;
    __asm__ __volatile__("mrs %0, basepri" : "=r"(key));
    __asm__ __volatile__("msr basepri_max, %0" : : "r"(CONFIG_EMB_CM_KERNEL_BASEPRI) : "memory");
    return key;
}

/* The `isb` makes a pending interrupt that the old mask held back taken here, one
 * instruction later at most, on the hardware and under QEMU alike. */
static EMB_ALWAYS_INLINE void emb_arch_irq_unlock(emb_irq_key_t key)
{
    __asm__ __volatile__("msr basepri, %0\n\t"
                         "isb"
                         :
                         : "r"(key)
                         : "memory");
}

static EMB_ALWAYS_INLINE bool emb_arch_irq_locked(void)
{
    emb_irq_key_t b;
    emb_irq_key_t p;
    __asm__ __volatile__("mrs %0, basepri" : "=r"(b));
    __asm__ __volatile__("mrs %0, primask" : "=r"(p));
    return b != 0u || (p & 1u) != 0u;
}

/* Kernel-aware handlers are plain functions bound with emb_irq_connect(); every
 * external vector enters the port's common entry, which brackets the handler with the
 * kernel's interrupt entry and exit (CONFIG_EMB_IRQ_DYNAMIC is required on this port). */
#define EMB_ISR(name)     static void name(void *emb_isr_arg_)
#define EMB_ISR_RAW(name) static void name(void *emb_isr_arg_)

#ifdef __cplusplus
}
#endif

#endif /* EMB_CORTEX_M_EMB_ARCH_H */
