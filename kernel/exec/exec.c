/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Kernel execution core: per-CPU state (SPEC-002 §2), initialization and start
 * (SPEC-008 §4, SPEC-011 §4), context queries (SPEC-001 §5.1), the checked-build
 * critical-section wrappers (SPEC-002 §4.1, §12), the scheduler lock (SPEC-002 §5),
 * the interrupt entry and exit hooks (SPEC-002 §6.2), and the initialization table
 * (SPEC-001 §6.4).
 */
#include <emb/kernel.h>
#include <emb/sched.h>
#include <emb/version.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/thread.h>
#include <embk/time.h>

embk_cpu_t embk_cpu; /* lock: the critical section */

/* The initialization table is the bracketed section emb_init_table (09 §7); weak so
 * that an image without EMB_*_DEFINE objects links. */
extern const emb_init_entry_t __start_emb_init_table[]
    EMB_WEAK; /* NOLINT(bugprone-reserved-identifier): linker-provided */
extern const emb_init_entry_t __stop_emb_init_table[]
    EMB_WEAK; /* NOLINT(bugprone-reserved-identifier): linker-provided */

static void run_init_table(void)
{
    const emb_init_entry_t *e = __start_emb_init_table;
    const emb_init_entry_t *stop = __stop_emb_init_table;
    if (e == NULL || stop == NULL) {
        return;
    }
    /* bounded by the number of entries the linker placed */
    for (; e < stop; e++) {
        if (e->fn != NULL) {
            e->fn();
        }
    }
}

void emb_kernel_init(void)
{
    if (embk_cpu.kernel_state != EMBK_KERNEL_UNINIT) {
        embk_fault_raise(EMB_FAULT_API_LIFECYCLE, 0u, 0u, __func__);
    }
    emb_arch_init();
    embk_sched_init();
    embk_time_init();
    embk_thread_init_subsystem();
    embk_cpu.kernel_state = EMBK_KERNEL_INIT;
    run_init_table();
}

void emb_kernel_start(void)
{
    embk_thread_t *first;
    emb_irq_key_t key;

    if (embk_cpu.kernel_state != EMBK_KERNEL_INIT) {
        embk_fault_raise(EMB_FAULT_API_LIFECYCLE, 1u, 0u, __func__);
    }
    key = embk_lock_sched();
    embk_time_start();
    first = embk_sched_peek();
    if (first != NULL) {
        embk_sched_remove_ready(first);
    } else {
        first = &embk_idle_thread;
    }
    embk_cpu.current = first;
    embk_cpu.reschedule_pending = 0u;
    embk_cpu.kernel_state = EMBK_KERNEL_RUNNING;
    EMBK_TRACE(EMB_TRACE_SWITCH, 0xFFu, embk_thread_index(first), 0u);
    (void)key; /* the launch restores the first thread's own interrupt state (P3) */
    emb_arch_kernel_start(first, &embk_idle_thread);
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
        embk_fault_raise(EMB_FAULT_API_CONTEXT, 1u, 1u, __func__); /* unlock without lock */
    }
    embk_irq_unlock(key);
}
#endif

/* ---- scheduler lock (SPEC-002 §5) ----------------------------------------------- */

void emb_sched_lock(void)
{
    if (!embk_in_thread()) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise(EMB_FAULT_API_CONTEXT, 0u, 0u, __func__);
#else
        return;
#endif
    }
    if (embk_cpu.sched_lock_depth == UINT8_MAX) {
        embk_fault_raise(EMB_FAULT_API_ARGUMENT, 0u, 0u, __func__);
    }
    embk_cpu.sched_lock_depth++; /* single byte, thread context only: no critical section needed */
    EMBK_TRACE(EMB_TRACE_SCHED_LOCK, embk_cpu.sched_lock_depth, 0u, 0u);
}

void emb_sched_unlock(void)
{
    emb_irq_key_t key;
    if (!embk_in_thread()) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise(EMB_FAULT_API_CONTEXT, 0u, 0u, __func__);
#else
        return;
#endif
    }
    if (embk_cpu.sched_lock_depth == 0u) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise(EMB_FAULT_API_CONTEXT, 2u, 0u, __func__); /* unlock without lock */
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

void embk_isr_enter(void)
{
    embk_cpu.irq_nesting_depth++;
#if CONFIG_EMB_CHECKED
    if (embk_cpu.irq_nesting_depth > (uint8_t)CONFIG_EMB_IRQ_MAX_NESTING) {
        embk_fault_raise(EMB_FAULT_IRQ_NESTING, embk_cpu.irq_nesting_depth, 0u, __func__);
    }
#endif
}

embk_thread_t *embk_isr_exit(void)
{
    EMBK_ASSERT(embk_cpu.irq_nesting_depth != 0u);
    embk_cpu.irq_nesting_depth--;
    if (embk_cpu.irq_nesting_depth != 0u) {
        return NULL; /* nested exit never switches (KRN-SCH-021) */
    }
    if (embk_cpu.reschedule_pending != 0u && embk_cpu.sched_lock_depth == 0u &&
        embk_cpu.kernel_state == EMBK_KERNEL_RUNNING) {
        return embk_sched_select(false); /* P1 */
    }
    return NULL;
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
