/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    exec.c
 * @brief   Kernel execution core.
 *
 * @details
 * per-CPU state (SPEC-002 §2), initialization and start (SPEC-008 §4, SPEC-011 §4), context queries
 * (SPEC-001 §5.1), the checked-build critical-section wrappers (SPEC-002 §4.1, §12), the scheduler
 * lock (SPEC-002 §5), the interrupt entry and exit hooks (SPEC-002 §6.2), and the initialization
 * table (SPEC-001 §6.4).
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#include <emb/arch.h>
#include <emb/irq.h>
#include <emb/kernel.h>
#include <emb/sched.h>
#include <emb/version.h>
#if CONFIG_EMB_DEBUG_DESCRIPTOR
#include <emb/debug.h>
#endif

#include <embk/crash.h>
#if CONFIG_EMB_DEVICE
#include <embk/device.h>
#endif
#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/thread.h>
#include <embk/time.h>

embk_cpu_t embk_cpu; /* lock: the critical section */

/* The initialization table is the bracketed section emb_init_table (09 §7); the bounds
 * are linker-provided and may be absent when no EMB_*_DEFINE object exists. */
EMB_TABLE_BOUNDS(const emb_init_entry_t, emb_init_table);

static void run_init_table(void)
{
    const emb_init_entry_t *e = EMB_TABLE_START(emb_init_table);
    const emb_init_entry_t *stop = EMB_TABLE_STOP(emb_init_table);
    /* MISRA Dev CS-12: a false positive of the analyzer. The bounds are weak: with no entry
     * they are undefined and their address is 0, which C's array declaration cannot say. */
    /* cppcheck-suppress knownConditionTrueFalse */
    if (e == NULL || stop == NULL) {
        return;
    }
    /* bounded by the number of entries the linker placed; the table may live in flash.
     * MISRA Dev CS-12: 18.3, the bounds of one linker table */
    /* cppcheck-suppress comparePointers */
    for (; e < stop; e++) {
        emb_init_fn_t fn = (emb_init_fn_t)EMB_FLASH_READ_FNPTR(&e->fn);
        if (fn != NULL) {
            fn();
        }
    }
}

/* ---- the interrupt stack (A90, PORT-016) ------------------------------------------- */

#if CONFIG_EMB_STACK_CHECK || CONFIG_EMB_STACK_STATS
#define ISR_STACK_FILLED 1
#define ISR_STACK_LIVE   64u /* kept clear of the fill: the frames above the caller's */
#else
#define ISR_STACK_FILLED 0
#endif

/* Fill the port's interrupt stack below the frames now live on it. Before the kernel
 * starts, interrupts are masked, so nothing else uses it. */
static void isr_stack_fill(void)
{
#if ISR_STACK_FILLED
    size_t size = 0u;
    uint8_t *base = emb_arch_isr_stack(&size);
    uint8_t here = 0u;
    uintptr_t at = (uintptr_t)&here; /* this frame: a bound for the live part */
    size_t n = size;
    size_t i;
    if (base == NULL) {
        return;
    }
    if (at >= (uintptr_t)base && at < (uintptr_t)base + size) {
        n = (size_t)(at - (uintptr_t)base); /* running on it: fill below this frame only */
        n = (n > ISR_STACK_LIVE) ? n - ISR_STACK_LIVE : 0u;
    }
    /* bounded by the stack's size */
    for (i = 0u; i < n; i++) {
        base[i] = EMB_STACK_FILL;
    }
#endif
}

#if CONFIG_EMB_STACK_CHECK
/* The outermost exit (A90): the interrupt stack's guard bytes, and the interrupted thread's,
 * whose stack takes at least the exception frame on every port. */
static void isr_stack_check(void)
{
    size_t size = 0u;
    const uint8_t *base = emb_arch_isr_stack(&size);
    if (base != NULL && size >= EMBK_STACK_GUARD_BYTES) {
        unsigned i;
        /* bounded: EMBK_STACK_GUARD_BYTES */
        for (i = 0u; i < EMBK_STACK_GUARD_BYTES; i++) {
            if (base[i] != EMB_STACK_FILL) {
                embk_fault_raise(EMB_FAULT_STACK_OVERFLOW, 0xFFu, 2u, "interrupt stack guard");
            }
        }
    }
    if (embk_cpu.kernel_state == EMBK_KERNEL_RUNNING) {
        embk_stack_guard_check(embk_cpu.current);
    }
}
#endif

emb_status_t emb_isr_stack_info(size_t *out_size, size_t *out_high_water)
{
    size_t size = 0u;
    const uint8_t *base = emb_arch_isr_stack(&size);
    if (base == NULL) {
        return EMB_ENOTSUP;
    }
    if (out_size != NULL) {
        *out_size = size;
    }
    if (out_high_water != NULL) {
        size_t used = 0u;
#if ISR_STACK_FILLED
        size_t i = 0u;
        /* bounded by the stack's size: the first byte that is not the fill */
        while (i < size && base[i] == EMB_STACK_FILL) {
            i++;
        }
        used = size - i;
#endif
        *out_high_water = used;
    }
    return EMB_OK;
}

void emb_kernel_init(void)
{
    if (embk_cpu.kernel_state != EMBK_KERNEL_UNINIT) {
        embk_fault_raise_api(EMB_API_ID_KERNEL_INIT, EMB_FAULT_API_LIFECYCLE, 0u, 0u, EMBK_WHERE);
    }
    emb_arch_init();
#if CONFIG_EMB_DEBUG_DESCRIPTOR
    /* the one reference to the descriptor: it keeps it in the image (A107) */
    (void)*(const volatile uint8_t *)&emb_debug_descriptor.magic[0];
#endif
    isr_stack_fill();
#if CONFIG_EMB_CRASH_RECORD
    embk_crash_record_boot(); /* what the previous run left, before anything can fault */
#endif
    embk_sched_init();
    embk_time_init();
    embk_thread_init_subsystem();
    embk_cpu.kernel_state = EMBK_KERNEL_INIT;
#if CONFIG_EMB_DEVICE
    embk_device_init_level(EMB_DEVICE_EARLY); /* polled, before any kernel object (SPEC-016 §4) */
#endif
    run_init_table();
}

void emb_kernel_start(void)
{
    embk_thread_t *first;
    emb_irq_key_t key;

    if (embk_cpu.kernel_state != EMBK_KERNEL_INIT) {
        embk_fault_raise_api(EMB_API_ID_KERNEL_START, EMB_FAULT_API_LIFECYCLE, 1u, 0u, EMBK_WHERE);
    }
#if CONFIG_EMB_DEVICE
    embk_device_init_level(EMB_DEVICE_KERNEL); /* the kernel's objects exist (SPEC-016 §4) */
#endif
    key = embk_lock_sched();
    embk_time_start();
    first = embk_sched_peek(); /* it stays in the ready structure while it runs (ADR-055) */
    if (first == NULL) {
        first = EMBK_IDLE;
    }
    embk_cpu.current = first;
    embk_cpu.reschedule_pending = 0u;
    embk_cpu.kernel_state = EMBK_KERNEL_RUNNING;
#if CONFIG_EMB_TIME_SLICING
    embk_cpu.rotate = 0u;
    embk_time_slice_start(first);
#endif
#if CONFIG_EMB_CHECKED
    embk_cpu.irq_lock_depth =
        0u; /* the first thread is a fresh context; the idle record is 0 too */
#endif
    EMBK_TRACE(EMB_TRACE_SWITCH, 0xFFu, embk_thread_index(first), 0u);
    (void)key; /* the launch restores the first thread's own interrupt state (P3) */
    emb_arch_kernel_start(first, EMBK_IDLE);
}

void embk_idle_loop(void)
{
    for (;;) {
        EMBK_TRACE(EMB_TRACE_IDLE_ENTER, 0u, 0u, 0u);
        emb_arch_idle();
        EMBK_TRACE(EMB_TRACE_IDLE_EXIT, 0u, 0u, 0u);
    }
}

/* ---- context ------------------------------------------------------------------ */

emb_context_t emb_context(void)
{
    if (embk_cpu.irq_nesting_depth != 0u) {
        return EMB_CONTEXT_ISR;
    }
    if (embk_cpu.kernel_state != EMBK_KERNEL_RUNNING) {
        return EMB_CONTEXT_PREKERNEL;
    }
    return EMB_CONTEXT_THREAD;
}

bool emb_in_isr(void)
{
    return embk_cpu.irq_nesting_depth != 0u;
}

unsigned emb_irq_nesting_depth(void)
{
    return embk_cpu.irq_nesting_depth;
}

/* ---- critical sections, checked build (SPEC-002 §12) ---------------------------- */

#if CONFIG_EMB_CHECKED
emb_irq_key_t emb_irq_lock(void)
{
    return embk_irq_lock();
}

void emb_irq_unlock(emb_irq_key_t key)
{
    if (embk_cpu.irq_lock_depth == 0u) {
        embk_fault_raise_api(EMB_API_ID_IRQ_UNLOCK, EMB_FAULT_API_CONTEXT, 1u, 1u,
                             EMBK_WHERE); /* unlock without lock */
    }
    embk_irq_unlock(key);
}
#endif

/* ---- scheduler lock (SPEC-002 §5) ----------------------------------------------- */

void emb_sched_lock(void)
{
    if (!embk_in_thread()) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise_api(EMB_API_ID_SCHED_LOCK, EMB_FAULT_API_CONTEXT, 0u, 0u, EMBK_WHERE);
#else
        return;
#endif
    }
    if (embk_cpu.sched_lock_depth == UINT8_MAX) {
        embk_fault_raise_api(EMB_API_ID_SCHED_LOCK, EMB_FAULT_API_ARGUMENT, 0u, 0u, EMBK_WHERE);
    }
    embk_cpu.sched_lock_depth++; /* single byte, thread context only: no critical section needed */
    EMBK_TRACE(EMB_TRACE_SCHED_LOCK, embk_cpu.sched_lock_depth, 0u, 0u);
}

void emb_sched_unlock(void)
{
    emb_irq_key_t key;
    if (!embk_in_thread()) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise_api(EMB_API_ID_SCHED_UNLOCK, EMB_FAULT_API_CONTEXT, 0u, 0u, EMBK_WHERE);
#else
        return;
#endif
    }
    if (embk_cpu.sched_lock_depth == 0u) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise_api(EMB_API_ID_SCHED_UNLOCK, EMB_FAULT_API_CONTEXT, 2u, 0u,
                             EMBK_WHERE); /* unlock without lock */
#else
        return;
#endif
    }
    key = embk_lock_sched();
    embk_cpu.sched_lock_depth--;
    EMBK_TRACE(EMB_TRACE_SCHED_UNLOCK, embk_cpu.sched_lock_depth, 0u, 0u);
    if (embk_cpu.sched_lock_depth == 0u) {
        embk_sched_reschedule_if_needed(); /* KRN-SCH-018 */
    }
    embk_unlock_sched(key);
}

unsigned emb_sched_lock_depth(void)
{
    return embk_cpu.sched_lock_depth;
}

/* ---- interrupt entry and exit (SPEC-002 §6.2, §7.1) ----------------------------- */

/* Both are called from the ports' interrupt entry assembly (the AVR's EMB_ISR vectors among
 * them), which LTO cannot see: EMB_USED keeps them (ADR-056). */

EMB_USED void embk_isr_enter(void)
{
    embk_cpu.irq_nesting_depth++;
#if CONFIG_EMB_CHECKED
    if (embk_cpu.irq_nesting_depth > (uint8_t)CONFIG_EMB_IRQ_MAX_NESTING) {
        embk_fault_raise(EMB_FAULT_IRQ_NESTING, embk_cpu.irq_nesting_depth, 0u, EMBK_WHERE);
    }
#endif
}

EMB_USED embk_thread_t *embk_isr_exit(void)
{
    embk_thread_t *next = NULL;
    EMBK_ASSERT(embk_cpu.irq_nesting_depth != 0u);
    embk_cpu.irq_nesting_depth--;
#if CONFIG_EMB_STACK_CHECK
    if (embk_cpu.irq_nesting_depth == 0u) {
        isr_stack_check(); /* SPEC-002 §7.2: the fill check at the outermost exit */
    }
#endif
    if (embk_cpu.irq_nesting_depth == 0u && embk_cpu.reschedule_pending != 0u &&
        embk_cpu.sched_lock_depth == 0u && embk_cpu.kernel_state == EMBK_KERNEL_RUNNING) {
        next = embk_sched_select(false); /* P1; a nested exit never switches (KRN-SCH-021) */
    }
    return (next != NULL) ? next : embk_cpu.current; /* the context to resume, never NULL */
}

/* ---- version and status names ---------------------------------------------------- */

const char *emb_version_string(void)
{
    return "0.2.0";
}

const char *emb_status_name(emb_status_t status)
{
#if CONFIG_EMB_STATUS_NAMES
    static const char *const names[] = {
        "EMB_OK",        "EMB_EPERM",      "EMB_EINVAL",     "EMB_EBUSY",  "EMB_ETIMEDOUT",
        "EMB_ECANCELED", "EMB_EDESTROYED", "EMB_EOWNERDEAD", "EMB_ENOMEM", "EMB_ENOTSUP",
        "EMB_EIO",       "EMB_EFAULT",     "EMB_ENOENT",     "EMB_EEXIST", "EMB_ESTATE",
        "EMB_EOVERFLOW", "EMB_ESTALE",     "EMB_EINTR",
    };
    if (status <= 0 && status >= -17) {
        return names[-status];
    }
    if (status == EMB_EDEADLK) {
        return "EMB_EDEADLK";
    }
    return "EMB_E?";
#else
    (void)status;
    return "";
#endif
}
