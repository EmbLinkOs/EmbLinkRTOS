/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Counting semaphores (SPEC-005 §4). No owner, no inheritance, ISR-safe give. */
#ifndef EMB_SEM_H
#define EMB_SEM_H

#include <emb/notify.h>
#include <emb/status.h>
#include <emb/thread.h>
#include <emb/time.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_sem_t);

/* Defined by the generated <emb/storage.h>; an incomplete type is enough for the
 * prototypes, and EMB_*_DEFINE users include <emb/emb.h>. */
typedef union emb_sem_storage emb_sem_storage_t;

#if CONFIG_EMB_SEM_COUNT_BITS == 8
typedef uint8_t emb_sem_count_t;
#define EMB_SEM_COUNT_MAX UINT8_MAX
#elif CONFIG_EMB_SEM_COUNT_BITS == 16
typedef uint16_t emb_sem_count_t;
#define EMB_SEM_COUNT_MAX UINT16_MAX
#else
typedef uint32_t emb_sem_count_t;
#define EMB_SEM_COUNT_MAX UINT32_MAX
#endif

#define EMB_SEM_SATURATE ((uint8_t)0x01u) /* give at max returns EMB_OK and changes nothing */

typedef struct emb_sem_attr {
    const char *name;
    emb_sem_count_t initial; /* default 0 */
    emb_sem_count_t max;     /* default EMB_SEM_COUNT_MAX; 1 for a binary semaphore */
    uint8_t flags;           /* EMB_SEM_SATURATE | EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
    uint8_t reserved_[3];    /* explicit padding */
} emb_sem_attr_t;

/**
 * emb_sem_attr_default() - Fill counting semaphore attributes with defaults.
 * @out_attr: Receives name NULL, initial 0, max EMB_SEM_COUNT_MAX and flags 0; NULL is a no-op.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      API-020 KRN-SYNC-028
 * @since    0.2
 * @stable   yes
 */
void emb_sem_attr_default(emb_sem_attr_t *out_attr);

/**
 * emb_sem_attr_binary() - Attributes of a binary semaphore: max 1, EMB_SEM_SATURATE.
 * @out_attr: Receives max 1 and EMB_SEM_SATURATE; NULL is a no-op.
 * @initially_available: True starts at 1, false at 0.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      API-020 KRN-SYNC-028
 * @since    0.2
 * @stable   yes
 */
void emb_sem_attr_binary(emb_sem_attr_t *out_attr, bool initially_available);

/**
 * emb_sem_init() - Construct a semaphore in caller-provided storage.
 * @storage: Aligned caller-owned storage, never initialized or destroyed; retain it until
 *           successful destruction.
 * @attr: Copied attributes; NULL selects defaults. Keep name storage alive.
 * @out_sem: Required output handle; cleared before argument validation.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1); O(active semaphores) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     borrows:storage
 * @config   CONFIG_EMB_SEM
 * @req      API-021 KRN-SYNC-028 KRN-OBJ-003 KRN-OBJ-007 KRN-OBJ-011
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for bad storage/output, max == 0 or initial > max;
 *         EMB_EBUSY when @storage still holds a semaphore that was not destroyed
 *         (misuse: storage is reused only through emb_sem_destroy()). The refusal is
 *         exact with CONFIG_EMB_OBJ_LIST; without it such storage is undefined.
 */
emb_status_t emb_sem_init(emb_sem_storage_t *storage, const emb_sem_attr_t *attr,
                          emb_sem_t *out_sem);

/**
 * emb_sem_destroy() - End the semaphore (KRN-OBJ-001).
 * @sem: Initialized semaphore; no live waiters unless ABORT_WAITERS is set.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(waiters) with EMB_OBJ_ABORT_WAITERS
 * @owns     gives:storage
 * @config   CONFIG_EMB_SEM
 * @req      KRN-OBJ-001
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EBUSY with waiters and no ABORT_WAITERS (misuse).
 */
emb_status_t emb_sem_destroy(emb_sem_t sem);

/**
 * emb_sem_take() - Take one unit, waiting up to @timeout.
 * @sem: Initialized semaphore to consume.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration); this is thread-only.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) fast path; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-028 KRN-WAIT-012
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EDESTROYED; EMB_EINVAL; EMB_EPERM.
 */
emb_status_t emb_sem_take(emb_sem_t sem, emb_timeout_t timeout);

/**
 * emb_sem_take_until() - Take one unit with an absolute deadline.
 * @sem: Initialized semaphore to consume.
 * @deadline: Kernel-clock deadline; a past deadline attempts a take without waiting.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) fast path; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      API-024 KRN-TIM-011 KRN-SYNC-028
 * @since    0.2
 * @stable   yes
 *
 * Return: the same statuses as emb_sem_take(). EMB_TICK_MAX is the current
 * internal forever sentinel. Keep 32-bit deadlines within the signed
 * half-range window (SPEC-003 §5.2).
 */
emb_status_t emb_sem_take_until(emb_sem_t sem, emb_instant_t deadline);

/**
 * emb_sem_give() - Give one unit: hand it to the first waiter, else count it.
 * @sem: Initialized semaphore to signal; a waiting thread receives the unit directly.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) count/selection, plus profile-specific timeout removal and scheduling
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-005 KRN-SYNC-028 KRN-IRQ-033
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EOVERFLOW at max without EMB_SEM_SATURATE; EMB_EINVAL.
 */
emb_status_t emb_sem_give(emb_sem_t sem);

/**
 * emb_sem_count() - Snapshot of the count; 0 for a bad handle.
 * @sem: Initialized semaphore to inspect; the returned count is a snapshot, not a reservation.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-028
 * @since    0.2
 * @stable   yes
 */
emb_sem_count_t emb_sem_count(emb_sem_t sem);

#if CONFIG_EMB_NOTIFY
/**
 * emb_sem_bind_notify() - Set @bit in @thread when the count goes from 0 to 1 (ADR-027).
 * @sem: Initialized semaphore with no existing binding.
 * @thread: Initialized target retained for the entire binding lifetime; no unbind API exists yet.
 * @bit: One application-bit mask, e.g. EMB_NOTIFY_BIT(0), not a bit index.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM CONFIG_EMB_NOTIFY
 * @req      KRN-SYNC-035 KRN-NOTIF-005 KRN-NOTIF-009
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EEXIST when already bound; EMB_EINVAL for bad handles,
 *         a zero/multi-bit mask or a kernel bit. Binding does not signal an
 *         already-positive count: inspect/drain initial readiness before waiting.
 *         Keep the target thread alive until the semaphore is destroyed.
 */
emb_status_t emb_sem_bind_notify(emb_sem_t sem, emb_thread_t thread, emb_notify_bits_t bit);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_SEM_H */
