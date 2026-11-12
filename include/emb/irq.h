/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    irq.h
 * @brief   Critical sections and interrupt management (SPEC-002 §4, §8).
 *
 * @details
 * The critical-section primitives are key based: emb_irq_unlock() restores exactly the state
 * emb_irq_lock() returned, so nesting costs nothing and unlocks are LIFO. In a checked build they
 * are real functions that track the nesting depth; in a release build they are the port's
 * always-inline primitives with a "memory" clobber.
 *
 * @see     kernel/irq/irq.c
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
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
#define EMB_IRQ_LEVEL_MAX        CONFIG_EMB_IRQ_LEVEL_MAX

/**
 * emb_irq_lock() - Mask kernel-aware interrupts on this CPU.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-018 KRN-IRQ-033
 * @since    0.2
 * @stable   yes
 *
 * Return: the previous mask state, to be passed to emb_irq_unlock().
 */
#if CONFIG_EMB_CHECKED
emb_irq_key_t emb_irq_lock(void);
/**
 * emb_irq_unlock() - Restore the previous mask state in a checked build.
 * @key: The value from the matching emb_irq_lock(); unlock in reverse order.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_CHECKED
 * @req      KRN-IRQ-018 KRN-IRQ-033
 * @since    0.2
 * @stable   yes
 */
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
 * @req      KRN-IRQ-018 KRN-IRQ-033
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
 * @req      KRN-IRQ-018
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
 * emb_isr_stack_info() - Size and high-water mark of the interrupt stack.
 * @out_size:       Its size in bytes: CONFIG_EMB_ISR_STACK_SIZE where the port has an
 *                  interrupt stack of its own; on AVR, the pre-kernel stack the idle
 *                  context and its interrupts use. May be NULL.
 * @out_high_water: The bytes ever used, from the fill below the live frames at kernel
 *                  init; 0 without CONFIG_EMB_STACK_CHECK or CONFIG_EMB_STACK_STATS.
 *                  May be NULL.
 *
 * The deepest point any interrupt, nested ones included, has reached since the kernel
 * initialized: the measurement for sizing CONFIG_EMB_ISR_STACK_SIZE (A90).
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(stack) scan
 * @owns     none
 * @config   CONFIG_EMB_STACK_CHECK CONFIG_EMB_STACK_STATS
 * @req      PORT-016 KRN-MEM-009
 * @since    0.2
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOTSUP when the port has no such stack (native).
 */
emb_status_t emb_isr_stack_info(size_t *out_size, size_t *out_high_water);

/**
 * emb_irq_enable() - Unmask one interrupt at the controller.
 * @irq: Interrupt number from the SoC table.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-029
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for an unknown number.
 */
emb_status_t emb_irq_enable(emb_irq_t irq);
/**
 * emb_irq_disable() - Mask one interrupt at the controller.
 * @irq: Interrupt number from the port/SoC table.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-029
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for an unknown number; port-specific unsupported
 * operations may return EMB_ENOTSUP. This does not clear a pending interrupt.
 */
emb_status_t emb_irq_disable(emb_irq_t irq);
/**
 * emb_irq_is_enabled() - Read one controller enable state.
 * @irq: Interrupt number from the port/SoC table.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-029
 * @since    0.2
 * @stable   yes
 *
 * Return: true when enabled, false when disabled or the number is unknown.
 */
bool emb_irq_is_enabled(emb_irq_t irq);
/**
 * emb_irq_set_level() - Set a generic interrupt priority.
 * @irq: Interrupt number from the port/SoC table.
 * @level: Generic priority in 0..EMB_IRQ_LEVEL_MAX; larger means more urgent.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-028 KRN-IRQ-029
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; release builds return EMB_EPERM in ISR context and EMB_EINVAL
 * for an unknown number or out-of-range level. Checked builds raise the respective
 * context/argument fault. Context is checked first, then irq (argument 1), then
 * level (argument 2). Invalid requests do not write the controller.
 * Current ports expose only kernel-aware levels: EMB_IRQ_LEVEL_MAX equals
 * EMB_IRQ_LEVEL_KERNEL_MAX. Native, AVR and current RV32 expose only level 0;
 * it preserves their fixed delivery order, not equal hardware urgency for all
 * sources. Cortex-M maps generic levels using its declared priority width.
 * Callers must query the advertised limits; a level is not portable to every port.
 */
emb_status_t emb_irq_set_level(emb_irq_t irq, uint8_t level);
/**
 * emb_irq_pend() - Request delivery of a kernel-aware interrupt.
 * @irq: Interrupt number supported by the port.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-029
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for an unknown number; EMB_ENOTSUP for a
 * controller/vector that cannot be software-pended. Delivery follows the
 * current mask and enable state. A kernel-independent ISR must use the
 * port primitive emb_arch_irq_pend_soft(), not this public kernel API.
 */
emb_status_t emb_irq_pend(emb_irq_t irq);
/**
 * emb_irq_clear_pending() - Clear one controller pending state.
 * @irq: Interrupt number supported by the port.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-IRQ-029
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for an unknown number; EMB_ENOTSUP where
 * clearing is unsupported. A level source may pend again until acknowledged.
 */
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
 * @req      KRN-IRQ-029
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
