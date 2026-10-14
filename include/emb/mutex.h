/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Mutexes (SPEC-005 §3): one type, protocol INHERIT (default), CEILING, or NONE,
 * optionally recursive; hand-off to the first waiter on unlock (no barging);
 * deadlock detection along the inheritance chain; owner-death policy from
 * CONFIG_EMB_MUTEX_OWNER_DEATH. Mutating operations are thread-only; is_owner returns false in an
 * ISR.
 */
#ifndef EMB_MUTEX_H
#define EMB_MUTEX_H

#include <emb/status.h>
#include <emb/time.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_mutex_t);

/* Defined by the generated <emb/storage.h>; an incomplete type is enough for the
 * prototypes, and EMB_*_DEFINE users include <emb/emb.h>. */
typedef union emb_mutex_storage emb_mutex_storage_t;

#define EMB_MUTEX_INHERIT ((uint8_t)0u)
#define EMB_MUTEX_CEILING ((uint8_t)1u)
#define EMB_MUTEX_NONE    ((uint8_t)2u)

#define EMB_MUTEX_RECURSIVE ((uint8_t)0x01u)

typedef struct emb_mutex_attr {
    const char *name;
    uint8_t protocol; /* EMB_MUTEX_INHERIT | EMB_MUTEX_CEILING | EMB_MUTEX_NONE */
    uint8_t ceiling;  /* EMB_MUTEX_CEILING only */
    uint8_t flags;    /* EMB_MUTEX_RECURSIVE | EMB_OBJ_ABORT_WAITERS */
    uint8_t reserved_;
} emb_mutex_attr_t;

/**
 * emb_mutex_attr_default() - Fill mutex attributes with defaults.
 * @out_attr: Receives name NULL, INHERIT protocol, ceiling 0 and flags 0; NULL is a no-op.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MUTEX
 * @req      API-020
 * @since    0.2
 * @stable   yes
 *
 * When CONFIG_EMB_MUTEX_INHERIT is off, select an available protocol before
 * initialization; the default INHERIT initialization returns EMB_ENOTSUP.
 */
void emb_mutex_attr_default(emb_mutex_attr_t *out_attr);

/**
 * emb_mutex_init() - Construct a mutex in caller-provided storage.
 * @storage: Aligned caller-owned storage; retain it until successful destruction.
 * @attr: Attributes copied during initialization; NULL selects defaults. Keep name storage alive.
 * @out_mutex: Required output handle; cleared before argument validation.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     borrows:storage
 * @config   CONFIG_EMB_MUTEX
 * @req      API-020 API-021 KRN-OBJ-003 KRN-SYNC-003 KRN-SYNC-012
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for bad storage/output, protocol or ceiling;
 *         EMB_ENOTSUP for INHERIT when CONFIG_EMB_MUTEX_INHERIT is off.
 */
emb_status_t emb_mutex_init(emb_mutex_storage_t *storage, const emb_mutex_attr_t *attr,
                            emb_mutex_t *out_mutex);

/**
 * emb_mutex_destroy() - End the mutex (KRN-OBJ-001, SPEC-005 §3.7).
 * @mutex: Initialized mutex; no other owner and no live waiters unless ABORT_WAITERS is set.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(waiters + owned + PI work), plus profile-specific timeout removals
 * @owns     gives:storage
 * @config   CONFIG_EMB_MUTEX
 * @req      KRN-OBJ-001 KRN-OBJ-010
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EBUSY when owned by another thread or with waiters and no
 *         ABORT_WAITERS (misuse).
 */
emb_status_t emb_mutex_destroy(emb_mutex_t mutex);

/**
 * emb_mutex_lock() - Acquire the mutex, waiting up to @timeout.
 * @mutex: Initialized mutex to acquire.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(owned) acquisition; O(waiters + PI work + timeouts) finite contended wait
 * @owns     none
 * @config   CONFIG_EMB_MUTEX
 * @req      KRN-SYNC-003 KRN-SYNC-009 KRN-SYNC-020 KRN-SYNC-023 KRN-SYNC-024 KRN-SYNC-025
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EDESTROYED; EMB_EOWNERDEAD
 *         (the caller owns the mutex); EMB_EDEADLK (it does not); EMB_EOVERFLOW
 *         (recursion count at 255); EMB_EPERM; EMB_EINVAL.
 */
emb_status_t emb_mutex_lock(emb_mutex_t mutex, emb_timeout_t timeout);

/**
 * emb_mutex_lock_until() - Acquire with an absolute deadline.
 * @mutex: Initialized mutex to acquire.
 * @deadline: Kernel-clock deadline; a past deadline attempts acquisition without waiting.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(owned) acquisition; enqueue/PI/timeout costs as emb_mutex_lock()
 * @owns     none
 * @config   CONFIG_EMB_MUTEX
 * @req      API-024 KRN-TIM-011 KRN-SYNC-020
 * @since    0.2
 * @stable   yes
 *
 * Return: the same statuses and ownership rules as emb_mutex_lock().
 * EMB_TICK_MAX is the current internal forever sentinel; use a finite deadline
 * for bounded waits. In 32-bit configurations, keep deadlines within the
 * signed half-range window (SPEC-003 §5.2).
 */
emb_status_t emb_mutex_lock_until(emb_mutex_t mutex, emb_instant_t deadline);

/**
 * emb_mutex_unlock() - Release the mutex; hand it to the first waiter if any.
 * @mutex: Initialized mutex owned by the calling thread.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(owned + PI work), plus waiter hand-off and profile-specific timeout removal
 * @owns     none
 * @config   CONFIG_EMB_MUTEX
 * @req      KRN-SYNC-008 KRN-SYNC-010 KRN-SYNC-021 KRN-WAIT-012
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EPERM when the caller is not the owner (misuse).
 */
emb_status_t emb_mutex_unlock(emb_mutex_t mutex);

/**
 * emb_mutex_is_owner() - Does the calling thread own @mutex?
 * @mutex: Initialized mutex to query; false for an invalid handle or ISR caller.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MUTEX
 * @req      KRN-SYNC-002
 * @since    0.2
 * @stable   yes
 */
bool emb_mutex_is_owner(emb_mutex_t mutex);

/**
 * emb_mutex_mark_consistent() - Clear the inconsistent mark after EMB_EOWNERDEAD (SPEC-005 §3.6).
 * @mutex: Mutex owned by the caller after repairing state following EMB_EOWNERDEAD.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MUTEX
 * @req      KRN-SYNC-025
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EPERM when the caller is not the owner.
 */
emb_status_t emb_mutex_mark_consistent(emb_mutex_t mutex);

#ifdef __cplusplus
}
#endif

#endif /* EMB_MUTEX_H */
