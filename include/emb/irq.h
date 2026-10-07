/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Critical sections and interrupt management (SPEC-002 §4, §8). The critical-section
 * primitives are key based: emb_irq_unlock() restores exactly the state
 * emb_irq_lock() returned, so nesting costs nothing and unlocks are LIFO. In a
 * checked build they are real functions that track the nesting depth; in a release
 * build they are the port's always-inline primitives with a "memory" clobber.
 */
#ifndef EMB_IRQ_H
#define EMB_IRQ_H

#include <emb/status.h>
#include <emb/types.h>

#include <emb_arch.h> /* the port: emb_irq_key_t, emb_arch_irq_*, EMB_ISR, EMB_ISR_RAW */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*emb_isr_fn_t)(void *arg);

#define EMB_IRQ_LEVEL_KERNEL_MAX CONFIG_EMB_IRQ_KERNEL_LEVEL

/**
 * emb_irq_lock() - Mask kernel-aware interrupts on this CPU.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-001 KRN-IRQ-002 KRN-IRQ-033
 * @since    0.2
 * @stable   yes
 *
 * Return: the previous mask state, to be passed to emb_irq_unlock().
 */
#if CONFIG_EMB_CHECKED
emb_irq_key_t emb_irq_lock(void);
void emb_irq_unlock(emb_irq_key_t key);
#else
static EMB_ALWAYS_INLINE emb_irq_key_t emb_irq_lock(void)
{
    return emb_arch_irq_lock();
}

/**
 * emb_irq_unlock() - Restore the mask state returned by emb_irq_lock().
 * @key: The value emb_irq_lock() returned; unlocks must be in reverse order.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-001 KRN-IRQ-002
 * @since    0.2
 * @stable   yes
 */
static EMB_ALWAYS_INLINE void emb_irq_unlock(emb_irq_key_t key)
{
    emb_arch_irq_unlock(key);
}
#endif

/**
 * emb_irq_is_locked() - Are kernel-aware interrupts masked on this CPU?
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-002
 * @since    0.2
 * @stable   yes
 */
static EMB_ALWAYS_INLINE bool emb_irq_is_locked(void)
{
    return emb_arch_irq_locked();
}

/**
 * emb_irq_nesting_depth() - Number of nested kernel-aware interrupts active.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-012
 * @since    0.2
 * @stable   yes
 */
unsigned emb_irq_nesting_depth(void);

/**
 * emb_irq_enable() - Unmask one interrupt at the controller.
 * @irq: Interrupt number from the SoC table.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-020
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for an unknown number.
 */
emb_status_t emb_irq_enable(emb_irq_t irq);
emb_status_t emb_irq_disable(emb_irq_t irq);
bool emb_irq_is_enabled(emb_irq_t irq);
emb_status_t emb_irq_set_level(emb_irq_t irq, uint8_t level);
emb_status_t emb_irq_pend(emb_irq_t irq);
emb_status_t emb_irq_clear_pending(emb_irq_t irq);

#if CONFIG_EMB_IRQ_DYNAMIC
/**
 * emb_irq_connect() - Bind a handler to an interrupt at run time.
 * @irq: Interrupt number.
 * @fn:  Handler, run in interrupt context as a kernel-aware handler.
 * @arg: Passed to @fn.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_IRQ_DYNAMIC
 * @req      KRN-IRQ-021
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for an unknown number or a NULL handler.
 */
emb_status_t emb_irq_connect(emb_irq_t irq, emb_isr_fn_t fn, void *arg);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_IRQ_H */
