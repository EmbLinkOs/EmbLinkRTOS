/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Condition variables (SPEC-005 §6): wait with a mutex released atomically, no stored signal. */
#ifndef EMB_COND_H
#define EMB_COND_H

#include <emb/kernel.h>
#include <emb/mutex.h>
#include <emb/status.h>
#include <emb/time.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_cond_t);

/* Defined by the generated <emb/storage.h>. */
typedef union emb_cond_storage emb_cond_storage_t;

typedef struct emb_cond_attr {
    const char *name;
    uint8_t flags;        /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
    uint8_t reserved_[3]; /* explicit padding */
} emb_cond_attr_t;

/**
 * emb_cond_attr_default() - Fill condition variable attributes with defaults.
 * @out_attr: Receives name NULL and flags 0; NULL is a no-op.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_COND
 * @req      API-020 KRN-SYNC-030
 * @since    0.3
 * @stable   no
 */
void emb_cond_attr_default(emb_cond_attr_t *out_attr);

/**
 * emb_cond_init() - Construct a condition variable in caller-provided storage.
 * @storage: Aligned caller-owned storage, never initialized or destroyed; retain it until
 *           successful destruction.
 * @attr: Copied attributes; NULL selects defaults. Keep name storage alive.
 * @out_cond: Required output handle; cleared before argument validation.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1); O(active condition variables) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     borrows:storage
 * @config   CONFIG_EMB_COND
 * @req      API-021 KRN-SYNC-030 KRN-OBJ-003 KRN-OBJ-007
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for bad storage or output; EMB_EBUSY when @storage still
 *         holds a condition variable that was not destroyed (misuse; exact with
 *         CONFIG_EMB_OBJ_LIST).
 */
emb_status_t emb_cond_init(emb_cond_storage_t *storage, const emb_cond_attr_t *attr,
                           emb_cond_t *out_cond);

/**
 * emb_cond_destroy() - End the condition variable (KRN-OBJ-001).
 * @cond: Initialized condition variable; no waiters unless ABORT_WAITERS is set.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(waiters) with EMB_OBJ_ABORT_WAITERS
 * @owns     gives:storage
 * @config   CONFIG_EMB_COND
 * @req      KRN-OBJ-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EBUSY with waiters and no ABORT_WAITERS (misuse). Aborted waiters
 *         still relock their mutex before emb_cond_wait() returns EMB_EDESTROYED.
 */
emb_status_t emb_cond_destroy(emb_cond_t cond);

/**
 * emb_cond_wait() - Release @mutex, wait for a signal, and relock @mutex.
 * @cond:    Initialized condition variable.
 * @mutex:   A mutex the caller owns exactly once (recursion count 1).
 * @timeout: EMB_NO_WAIT (returns EMB_ETIMEDOUT at once, mutex kept), EMB_WAIT_FOREVER or
 *           EMB_TIMEOUT(duration); the relock may block beyond it.
 *
 * @ctx      thread
 * @blocks   timeout, then the relock
 * @time     O(waiters + timeouts) enqueue, a full mutex release, then the cost of
 *           emb_mutex_lock()
 * @owns     none
 * @config   CONFIG_EMB_COND
 * @req      KRN-SYNC-030 KRN-SYNC-020 KRN-WAIT-012
 * @since    0.3
 * @stable   no
 *
 * The enqueue and the release are one section, so a signal sent after the release always
 * finds the waiter. There are no spurious wakeups and no stored signals. On every result,
 * including timeout, cancellation and destruction, the caller owns @mutex again on return;
 * the relock is not cancelable (a pending cancel is delivered at the next point).
 *
 * Return: EMB_OK when signaled; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EDESTROYED;
 *         EMB_EOWNERDEAD when the relock inherited an inconsistent mutex, whatever the
 *         wait's result (the caller owns it and repairs its state); the relock's own
 *         failure, EMB_EDEADLK or EMB_EDESTROYED, after which the caller does not own
 *         @mutex (checked builds fault on the deadlock); EMB_ESTATE when the
 *         recursion count is above 1; EMB_EPERM (misuse) when the caller does not own
 *         @mutex or is not a thread; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_cond_wait(emb_cond_t cond, emb_mutex_t mutex, emb_timeout_t timeout);

/**
 * emb_cond_wait_until() - emb_cond_wait() with an absolute deadline.
 * @cond:     Initialized condition variable.
 * @mutex:    A mutex the caller owns exactly once.
 * @deadline: Kernel-clock deadline for the wait; a past deadline returns EMB_ETIMEDOUT
 *            with the mutex kept. The relock may block beyond it.
 *
 * @ctx      thread
 * @blocks   timeout, then the relock
 * @time     as emb_cond_wait()
 * @owns     none
 * @config   CONFIG_EMB_COND
 * @req      API-024 KRN-TIM-011 KRN-SYNC-030
 * @since    0.3
 * @stable   no
 *
 * Return: the statuses of emb_cond_wait().
 */
emb_status_t emb_cond_wait_until(emb_cond_t cond, emb_mutex_t mutex, emb_instant_t deadline);

/**
 * emb_cond_signal() - Wake the first waiter, if any; a signal with no waiter is lost.
 * @cond: Initialized condition variable. Holding the waiters' mutex while signaling is
 *        recommended, not required.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1) selection, plus profile-specific timeout removal and scheduling
 * @owns     none
 * @config   CONFIG_EMB_COND
 * @req      KRN-SYNC-030
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle; EMB_EPERM (misuse) outside thread context.
 */
emb_status_t emb_cond_signal(emb_cond_t cond);

/**
 * emb_cond_broadcast() - Wake every waiter, in queue order, with one reschedule.
 * @cond: Initialized condition variable.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(waiters) in one section
 * @owns     none
 * @config   CONFIG_EMB_COND
 * @req      KRN-SYNC-030 KRN-WAIT-013
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle; EMB_EPERM (misuse) outside thread context.
 */
emb_status_t emb_cond_broadcast(emb_cond_t cond);

/* EMB_COND_DEFINE(sym, flags): storage, the handle variable `sym`, and an init-table
 * entry that constructs the condition variable in emb_kernel_init() (SPEC-001 §6.4, A64);
 * a failure faults. MISRA Dev CS-12: rule 20.7, `sym` is a token. */
/* NOLINTBEGIN(bugprone-macro-parentheses): MISRA Dev CS-12 rule 20.7, as above */
#define EMB_COND_DEFINE(sym, flags_)                                         \
    static emb_cond_storage_t sym##_storage_;                                \
    emb_cond_t sym;                                                          \
    static void sym##_init_(void)                                            \
    {                                                                        \
        emb_cond_attr_t attr_;                                               \
        emb_cond_attr_default(&attr_);                                       \
        attr_.name = EMB_DEFINE_NAME_(#sym);                                 \
        attr_.flags = (uint8_t)(flags_);                                     \
        (void)emb_check_status(emb_cond_init(&sym##_storage_, &attr_, &sym), \
                               "EMB_COND_DEFINE(" #sym ")");                 \
    }                                                                        \
    EMB_INIT_TABLE_ENTRY(sym##_init_)
/* NOLINTEND(bugprone-macro-parentheses) */

#ifdef __cplusplus
}
#endif

#endif /* EMB_COND_H */
