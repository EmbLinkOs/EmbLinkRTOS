/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Event flags (SPEC-005 §5): a word of bits, ANY or ALL waits, ISR-safe set and clear. */
#ifndef EMB_EVENT_H
#define EMB_EVENT_H

#include <emb/notify.h>
#include <emb/status.h>
#include <emb/thread.h>
#include <emb/time.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_event_t);

/* Defined by the generated <emb/storage.h>. */
typedef union emb_event_storage emb_event_storage_t;

#if CONFIG_EMB_EVENT_BITS == 8
typedef uint8_t emb_event_bits_t;
#elif CONFIG_EMB_EVENT_BITS == 16
typedef uint16_t emb_event_bits_t;
#else
typedef uint32_t emb_event_bits_t;
#endif

/* Wait modes: ANY or ALL of the mask, optionally clearing the satisfying bits. */
#define EMB_EVENT_ANY   ((uint8_t)0x00u)
#define EMB_EVENT_ALL   ((uint8_t)0x01u)
#define EMB_EVENT_CLEAR ((uint8_t)0x02u)

typedef struct emb_event_attr {
    const char *name;
    emb_event_bits_t initial; /* default 0 */
    uint8_t flags;            /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
    uint8_t reserved_[3];     /* explicit padding */
} emb_event_attr_t;

/**
 * emb_event_attr_default() - Fill event flag attributes with defaults.
 * @out_attr: Receives name NULL, initial 0 and flags 0; NULL is a no-op.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_EVENT
 * @req      API-020 KRN-SYNC-029
 * @since    0.3
 * @stable   no
 */
void emb_event_attr_default(emb_event_attr_t *out_attr);

/**
 * emb_event_init() - Construct event flags in caller-provided storage.
 * @storage: Aligned caller-owned storage, never initialized or destroyed; retain it until
 *           successful destruction.
 * @attr: Copied attributes; NULL selects defaults. Keep name storage alive.
 * @out_event: Required output handle; cleared before argument validation.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1); O(active event flags) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     borrows:storage
 * @config   CONFIG_EMB_EVENT
 * @req      API-021 KRN-SYNC-029 KRN-OBJ-003 KRN-OBJ-007
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for bad storage or output; EMB_EBUSY when @storage still
 *         holds event flags that were not destroyed (misuse; exact with CONFIG_EMB_OBJ_LIST).
 */
emb_status_t emb_event_init(emb_event_storage_t *storage, const emb_event_attr_t *attr,
                            emb_event_t *out_event);

/**
 * emb_event_destroy() - End the event flags (KRN-OBJ-001).
 * @event: Initialized event flags; no waiters unless ABORT_WAITERS is set.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(waiters) with EMB_OBJ_ABORT_WAITERS
 * @owns     gives:storage
 * @config   CONFIG_EMB_EVENT
 * @req      KRN-OBJ-001 KRN-NOTIF-014
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EBUSY with waiters and no ABORT_WAITERS (misuse). A binding is
 *         removed and its target released; the target's bits are not touched.
 */
emb_status_t emb_event_destroy(emb_event_t event);

/**
 * emb_event_set() - OR @bits in, then wake every waiter the new value satisfies.
 * @event: Initialized event flags.
 * @bits:  Bits to set; 0 changes nothing.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(waiters) in one section, one reschedule
 * @owns     none
 * @config   CONFIG_EMB_EVENT
 * @req      KRN-SYNC-029 KRN-SYNC-005 KRN-SYNC-027 KRN-WAIT-013
 * @since    0.3
 * @stable   no
 *
 * Waiters are examined in queue order (priority, or arrival with EMB_OBJ_FIFO). Each
 * satisfied waiter is handed the satisfying bits; a CLEAR waiter clears them before the
 * next waiter is examined, so overlapping clearing waiters are served deterministically.
 * A bound thread is notified when bits remain set and differ from before the call.
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_event_set(emb_event_t event, emb_event_bits_t bits);

/**
 * emb_event_clear() - Clear @bits; never wakes anyone.
 * @event: Initialized event flags.
 * @bits:  Bits to clear; 0 changes nothing.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_EVENT
 * @req      KRN-SYNC-029 KRN-SYNC-027
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_event_clear(emb_event_t event, emb_event_bits_t bits);

/**
 * emb_event_get() - Snapshot of the bits; 0 for a bad handle.
 * @event: Initialized event flags.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_EVENT
 * @req      KRN-SYNC-029
 * @since    0.3
 * @stable   no
 */
emb_event_bits_t emb_event_get(emb_event_t event);

/**
 * emb_event_wait() - Wait until ANY or ALL of @mask is set.
 * @event:   Initialized event flags.
 * @mask:    Nonzero bits of interest.
 * @mode:    EMB_EVENT_ANY or EMB_EVENT_ALL, optionally | EMB_EVENT_CLEAR.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 * @out_satisfied: Receives the satisfying bits on EMB_OK; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) check; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_EVENT
 * @req      KRN-SYNC-029 KRN-WAIT-012
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EDESTROYED; EMB_EINVAL for a zero
 *         mask, an unknown mode bit or a bad handle; EMB_EPERM outside thread context.
 *         A woken waiter receives the bits that satisfied it; the word is not re-read.
 */
emb_status_t emb_event_wait(emb_event_t event, emb_event_bits_t mask, uint8_t mode,
                            emb_timeout_t timeout, emb_event_bits_t *out_satisfied);

/**
 * emb_event_wait_until() - Wait for ANY or ALL of @mask with an absolute deadline.
 * @event:    Initialized event flags.
 * @mask:     Nonzero bits of interest.
 * @mode:     EMB_EVENT_ANY or EMB_EVENT_ALL, optionally | EMB_EVENT_CLEAR.
 * @deadline: Kernel-clock deadline; a past deadline checks without waiting.
 * @out_satisfied: Receives the satisfying bits on EMB_OK; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) check; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_EVENT
 * @req      API-024 KRN-TIM-011 KRN-SYNC-029
 * @since    0.3
 * @stable   no
 *
 * Return: the statuses of emb_event_wait().
 */
emb_status_t emb_event_wait_until(emb_event_t event, emb_event_bits_t mask, uint8_t mode,
                                  emb_instant_t deadline, emb_event_bits_t *out_satisfied);

#if CONFIG_EMB_NOTIFY
/**
 * emb_event_bind_notify() - Set @bit in @thread when a set leaves bits changed and nonzero.
 * @event:  Initialized event flags with no binding.
 * @thread: Initialized, not terminated, application thread; it cannot be destroyed while
 *          bound (ADR-046).
 * @bit:    One application-bit mask, e.g. EMB_NOTIFY_BIT(0).
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_EVENT CONFIG_EMB_NOTIFY
 * @req      KRN-SYNC-035 KRN-NOTIF-004 KRN-NOTIF-005 KRN-NOTIF-014
 * @since    0.3
 * @stable   no
 *
 * Return: the statuses of emb_sem_bind_notify(). Direct waiters are served first; the
 *         binding fires only for bits still set after them.
 */
emb_status_t emb_event_bind_notify(emb_event_t event, emb_thread_t thread, emb_notify_bits_t bit);

/**
 * emb_event_unbind_notify() - Remove the event flags' notification binding.
 * @event: Initialized event flags.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_EVENT CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-013
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOENT when not bound; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_event_unbind_notify(emb_event_t event);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_EVENT_H */
