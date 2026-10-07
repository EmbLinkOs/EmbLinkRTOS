/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Native port header (SPEC-013; contract SPEC-011 §3, §6): the critical-section
 * primitives, the handler declaration macros, and the port's type and size constants.
 * Included by <emb/irq.h> and <emb/arch.h>.
 */
#ifndef EMB_NATIVE_EMB_ARCH_H
#define EMB_NATIVE_EMB_ARCH_H

#include <emb/compiler.h>
#include <emb/config.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int emb_irq_key_t;    /* the gate's previous mask state (SPEC-002 §4.1) */
typedef uint64_t emb_arch_timer_raw_t; /* virtual counter: one unit per tick */

#define EMB_ARCH_TCB_EXTENSION       128 /* host thread handle, condition variable, launch data */
#define EMB_ARCH_TCB_EXTENSION_ALIGN 16
#define EMB_ARCH_CLOCK_SEQLOCK       1 /* SPEC-003 §3.1 table */
#define EMB_ARCH_IRQ_COUNT           CONFIG_EMB_NATIVE_MAX_IRQS
#define EMB_NATIVE_IRQ_TIMER         ((emb_irq_t)0u) /* the virtual timer's interrupt number */

emb_irq_key_t embn_irq_lock(void);
void embn_irq_unlock(emb_irq_key_t key);
bool embn_irq_locked(void);

static EMB_ALWAYS_INLINE emb_irq_key_t emb_arch_irq_lock(void)
{
    return embn_irq_lock();
}

static EMB_ALWAYS_INLINE void emb_arch_irq_unlock(emb_irq_key_t key)
{
    embn_irq_unlock(key);
}

static EMB_ALWAYS_INLINE bool emb_arch_irq_locked(void)
{
    return embn_irq_locked();
}

/* Kernel-aware handlers are plain functions bound to a number with emb_irq_connect()
 * (CONFIG_EMB_IRQ_DYNAMIC is the native default). Both forms take the connect argument. */
#define EMB_ISR(name)     void name(void *emb_isr_arg_)
#define EMB_ISR_RAW(name) void name(void *emb_isr_arg_)

#ifdef __cplusplus
}
#endif

#endif /* EMB_NATIVE_EMB_ARCH_H */
