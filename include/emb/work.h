/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Work queues (SPEC-006 §4): a kernel thread runs submitted items one at a time, in order. */
#ifndef EMB_WORK_H
#define EMB_WORK_H

#include <emb/status.h>
#include <emb/storage.h>
#include <emb/time.h>
#include <emb/types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_workq_t);

/* Work item states, as emb_work_state() and emb_work_cancel() report them. */
typedef uint8_t emb_work_state_t;
#define EMB_WORK_IDLE    ((emb_work_state_t)0u) /* not queued, not running */
#define EMB_WORK_PENDING ((emb_work_state_t)1u) /* queued, its handler not started */
#define EMB_WORK_RUNNING ((emb_work_state_t)2u) /* its handler runs */

/* A work item: caller-owned storage, embedded in the application's own structure and
 * reached from the handler with EMB_CONTAINER_OF(). Initialize it with emb_work_init(). */
typedef struct emb_work {
    emb_work_storage_t opaque_;
} emb_work_t;

typedef void (*emb_work_fn_t)(emb_work_t *work);

typedef struct emb_workq_attr {
    const char *name;
    void *stack; /* required, caller-provided (KRN-THR-007); see EMB_THREAD_STACK() */
    size_t stack_size;
    uint8_t priority;     /* of the queue's thread */
    uint8_t reserved_[3]; /* explicit padding */
} emb_workq_attr_t;

/**
 * emb_workq_attr_default() - Fill work queue attributes with defaults.
 * @out_attr: Receives name and stack NULL, stack size 0 and the lowest application
 *            priority, which init refuses until a stack is set; NULL is a no-op.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WORKQ
 * @req      API-020 KRN-WQ-001
 * @since    0.3
 * @stable   no
 */
void emb_workq_attr_default(emb_workq_attr_t *out_attr);

/**
 * emb_workq_init() - Construct a work queue and start its thread.
 * @storage: Aligned caller-owned storage, never initialized or destroyed; retain it until
 *           successful destruction.
 * @attr: Required attributes, copied; the stack stays borrowed. Keep name storage alive.
 * @out_workq: Required output handle; cleared before argument validation.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     as emb_thread_init(); O(active work queues) list lookup with
 *           CONFIG_EMB_OBJ_LIST
 * @owns     borrows:storage borrows:stack
 * @config   CONFIG_EMB_WORKQ
 * @req      API-021 KRN-WQ-009 KRN-THR-007 KRN-OBJ-003 KRN-OBJ-007
 * @since    0.3
 * @stable   no
 *
 * The queue's thread is a kernel thread: thread calls on its handle return EMB_EPERM and
 * notifications to it are refused, so only the queue's own operations reach it. Handlers
 * run on it at @attr's priority.
 *
 * Return: EMB_OK; EMB_EINVAL for bad storage, attributes or output, or for a priority or
 *         stack emb_thread_init() refuses; EMB_EBUSY when @storage still holds a work
 *         queue that was not destroyed (misuse); the runtime statuses of
 *         emb_thread_init().
 */
emb_status_t emb_workq_init(emb_workq_storage_t *storage, const emb_workq_attr_t *attr,
                            emb_workq_t *out_workq);

/**
 * emb_workq_destroy() - Stop an idle work queue and wait for its thread to exit.
 * @workq: Initialized work queue with no item queued or running and no waiter.
 *
 * @ctx      thread
 * @blocks   until the queue's thread exits
 * @time     O(1) plus the queue thread's exit
 * @owns     gives:storage gives:stack
 * @config   CONFIG_EMB_WORKQ
 * @req      KRN-OBJ-001 KRN-WQ-010
 * @since    0.3
 * @stable   no
 *
 * The wait is not a cancellation point: a cancel sent meanwhile is delivered at the next
 * one. A submit or flush racing with the destroy returns EMB_ESTATE.
 *
 * Return: EMB_OK; EMB_EBUSY (misuse) while an item is queued or running, a thread waits
 *         on the queue, or another destroy is in progress, and when called from the
 *         queue's own thread; EMB_EINVAL for a bad handle; EMB_EPERM outside thread context.
 */
emb_status_t emb_workq_destroy(emb_workq_t workq);

/**
 * emb_workq_flush() - Wait until every item queued before the call has run.
 * @workq:   Initialized work queue.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) enqueue of a marker; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_WORKQ
 * @req      KRN-WQ-006 KRN-WQ-010 KRN-WAIT-012
 * @since    0.3
 * @stable   no
 *
 * An idle queue returns at once. Otherwise a marker queued behind the existing items
 * wakes the caller when the queue's thread reaches it; a timeout or cancel removes it.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_ESTATE while the queue is being
 *         destroyed; EMB_EPERM (misuse) from the queue's own thread (it would wait for
 *         itself) or outside thread context; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_workq_flush(emb_workq_t workq, emb_timeout_t timeout);

#if CONFIG_EMB_SYSTEM_WORKQ
/**
 * emb_workq_system() - The system work queue.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SYSTEM_WORKQ
 * @req      KRN-WQ-008
 * @since    0.3
 * @stable   no
 *
 * The queue, its thread and its stack are linked into an image only when this function
 * is referenced (ADR-048); kernel initialization then constructs it at
 * CONFIG_EMB_SYSTEM_WORKQ_PRIO with a CONFIG_EMB_SYSTEM_WORKQ_STACK-byte stack.
 *
 * Return: the queue's handle; a null handle before emb_kernel_init().
 */
emb_workq_t emb_workq_system(void);
#endif

/**
 * emb_work_init() - Initialize a work item that runs @fn.
 * @work: Caller-owned item, not queued or running; NULL is a no-op.
 * @fn:   Handler, run on the queue's thread with @work.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WORKQ
 * @req      KRN-WQ-001
 * @since    0.3
 * @stable   no
 *
 * Reinitializing an item that is queued or running corrupts its queue: cancel it with
 * emb_work_cancel_sync() first.
 */
void emb_work_init(emb_work_t *work, emb_work_fn_t fn);

/**
 * emb_work_submit() - Queue an idle item at the tail of @workq.
 * @workq: Initialized work queue.
 * @work:  Initialized item.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1), plus the queue thread's wake
 * @owns     none
 * @config   CONFIG_EMB_WORKQ
 * @req      KRN-WQ-001 KRN-WQ-002 KRN-WQ-005
 * @since    0.3
 * @stable   no
 *
 * An item is never queued twice and never runs concurrently with itself. A submit while
 * its handler runs on @workq is kept: the item is queued again when the handler returns.
 *
 * Return: EMB_OK; EMB_EEXIST when already queued (nothing changes); EMB_EBUSY while its
 *         handler runs on another queue; EMB_ESTATE while @workq is being destroyed;
 *         EMB_EINVAL for a bad handle or an item that was not initialized.
 */
emb_status_t emb_work_submit(emb_workq_t workq, emb_work_t *work);

/**
 * emb_work_cancel() - Remove a queued item; never waits.
 * @work:      Initialized item.
 * @out_state: Receives the state it was in; may be NULL.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WORKQ
 * @req      KRN-WQ-004
 * @since    0.3
 * @stable   no
 *
 * EMB_WORK_PENDING: the item was unlinked and will not run. EMB_WORK_RUNNING: its handler
 * completes and a resubmission made while it runs is dropped. EMB_WORK_IDLE: nothing to do.
 *
 * Return: EMB_OK; EMB_EINVAL for an item that was not initialized.
 */
emb_status_t emb_work_cancel(emb_work_t *work, emb_work_state_t *out_state);

/**
 * emb_work_cancel_sync() - Cancel, then wait until a running handler has returned.
 * @work:    Initialized item.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1); O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_WORKQ
 * @req      KRN-WQ-006 KRN-WQ-010 KRN-WAIT-012
 * @since    0.3
 * @stable   no
 *
 * On EMB_OK the item is idle unless it was submitted again after the call began, and its
 * storage may be reused once no one submits it.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT with the handler still running; EMB_ECANCELED; EMB_EPERM
 *         (misuse) from the queue thread the item is queued or running on (it would wait
 *         for itself) or outside thread context; EMB_EINVAL for an item that was not
 *         initialized.
 */
emb_status_t emb_work_cancel_sync(emb_work_t *work, emb_timeout_t timeout);

/**
 * emb_work_state() - Snapshot of an item's state; EMB_WORK_IDLE for one not initialized.
 * @work: Initialized item.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WORKQ
 * @req      KRN-WQ-004
 * @since    0.3
 * @stable   no
 */
emb_work_state_t emb_work_state(const emb_work_t *work);

#ifdef __cplusplus
}
#endif

#endif /* EMB_WORK_H */
