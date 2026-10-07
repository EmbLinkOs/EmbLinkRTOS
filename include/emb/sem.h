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

void emb_sem_attr_default(emb_sem_attr_t *out_attr);

/**
 * emb_sem_attr_binary() - Attributes of a binary semaphore: max 1, EMB_SEM_SATURATE.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-020
 * @since    0.2
 * @stable   yes
 */
void emb_sem_attr_binary(emb_sem_attr_t *out_attr, bool initially_available);

/**
 * emb_sem_init() - Construct a semaphore in caller-provided storage.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     borrows:storage
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-020 KRN-OBJ-003
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EINVAL for NULL storage or initial > max.
 */
emb_status_t emb_sem_init(emb_sem_storage_t *storage, const emb_sem_attr_t *attr,
                          emb_sem_t *out_sem);

/**
 * emb_sem_destroy() - End the semaphore (KRN-OBJ-001).
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
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) fast path; O(waiters) enqueue
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-020 KRN-SYNC-021 KRN-WAIT-012
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EDESTROYED; EMB_EINVAL; EMB_EPERM.
 */
emb_status_t emb_sem_take(emb_sem_t sem, emb_timeout_t timeout);

emb_status_t emb_sem_take_until(emb_sem_t sem, emb_instant_t deadline);

/**
 * emb_sem_give() - Give one unit: hand it to the first waiter, else count it.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-020 KRN-SYNC-022 KRN-IRQ-033
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EOVERFLOW at max without EMB_SEM_SATURATE; EMB_EINVAL.
 */
emb_status_t emb_sem_give(emb_sem_t sem);

/**
 * emb_sem_count() - Snapshot of the count; 0 for a bad handle.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM
 * @req      KRN-SYNC-020
 * @since    0.2
 * @stable   yes
 */
emb_sem_count_t emb_sem_count(emb_sem_t sem);

#if CONFIG_EMB_NOTIFY
/**
 * emb_sem_bind_notify() - Set @bit in @thread when the count goes from 0 to 1 (ADR-027).
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_SEM CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-004 KRN-NOTIF-005
 * @since    0.2
 * @stable   yes
 *
 * Return: EMB_OK; EMB_EEXIST when already bound; EMB_EINVAL for a kernel bit.
 */
emb_status_t emb_sem_bind_notify(emb_sem_t sem, emb_thread_t thread, emb_notify_bits_t bit);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_SEM_H */
