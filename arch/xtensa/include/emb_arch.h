/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Xtensa port header, the classic ESP32's LX6 (contract SPEC-011 §3, §6; docs/ports/xtensa.md):
 * the critical-section primitives, the handler declaration macros, and the port's types. The
 * critical section raises PS.INTLEVEL to 1, which masks every kernel-aware interrupt (all are
 * level 1 in this milestone); the key is the whole previous PS.
 */
#ifndef EMB_XTENSA_EMB_ARCH_H
#define EMB_XTENSA_EMB_ARCH_H

#include <emb/compiler.h>
#include <emb/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int emb_irq_key_t;    /* PS before the lock */
typedef uint32_t emb_arch_timer_raw_t; /* CCOUNT */

#define EMB_ARCH_TCB_EXTENSION 0
#define EMB_ARCH_CLOCK_SEQLOCK 0 /* the 64-bit clock is read inside the critical section */
#define EMB_ARCH_IRQ_COUNT     CONFIG_EMB_XT_IRQ_COUNT
#define EMB_XT_IRQ_FROM_CPU0   ((emb_irq_t)24u) /* the matrix source software can raise */

#define EMBN_XT_PS_INTLEVEL 0xFu

static EMB_ALWAYS_INLINE emb_irq_key_t emb_arch_irq_lock(void)
{
    emb_irq_key_t old;
    __asm__ __volatile__("rsil %0, 1" : "=a"(old) : : "memory");
    return old;
}

static EMB_ALWAYS_INLINE void emb_arch_irq_unlock(emb_irq_key_t key)
{
    __asm__ __volatile__("wsr %0, ps\n\trsync" : : "a"(key) : "memory");
}

static EMB_ALWAYS_INLINE bool emb_arch_irq_locked(void)
{
    emb_irq_key_t ps;
    __asm__ __volatile__("rsr %0, ps" : "=a"(ps));
    return (ps & EMBN_XT_PS_INTLEVEL) != 0u;
}

/* Masks everything and waits for good: how a board with no host to exit to ends a run. */
EMB_NORETURN void emb_xt_halt(void);

/* Kernel-aware handlers are plain functions bound with emb_irq_connect(); the level-1 entry
 * brackets them with the kernel's interrupt entry and exit (CONFIG_EMB_IRQ_DYNAMIC). */
#define EMB_ISR(name)     static void name(void *emb_isr_arg_)
#define EMB_ISR_RAW(name) static void name(void *emb_isr_arg_)

#ifdef __cplusplus
}
#endif

#endif /* EMB_XTENSA_EMB_ARCH_H */
