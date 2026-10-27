/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Message queues (SPEC-007 §2): fixed-size items copied by value through a caller's ring. */
#ifndef EMB_MSGQ_H
#define EMB_MSGQ_H

#include <emb/compiler.h>
#include <emb/notify.h>
#include <emb/status.h>
#include <emb/thread.h>
#include <emb/time.h>
#include <emb/types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_msgq_t);

/* Defined by the generated <emb/storage.h>. */
typedef union emb_msgq_storage emb_msgq_storage_t;

/* Ring alignment: the kernel may copy items in words. */
#define EMB_MSGQ_ALIGN sizeof(void *)

/* Bytes of a ring that holds @capacity items of @item_size bytes. */
#define EMB_MSGQ_RING_SIZE(item_size, capacity) ((size_t)(item_size) * (size_t)(capacity))

/* Declares a correctly sized and aligned ring: static EMB_MSGQ_RING(ring, sizeof(T), 8); */
#define EMB_MSGQ_RING(name, item_size, capacity) \
    unsigned char name[EMB_MSGQ_RING_SIZE(item_size, capacity)] EMB_ALIGNED(EMB_MSGQ_ALIGN)

typedef struct emb_msgq_attr {
    const char *name;
    void *ring;           /* caller-owned, aligned to EMB_MSGQ_ALIGN; see EMB_MSGQ_RING() */
    size_t ring_size;     /* bytes at @ring: at least capacity * item_size */
    uint16_t item_size;   /* 1 .. CONFIG_EMB_MSGQ_MAX_ITEM */
    uint16_t capacity;    /* items, at least 1; at most 255 with CONFIG_EMB_IPC_SMALL */
    uint8_t flags;        /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
    uint8_t reserved_[3]; /* explicit padding */
} emb_msgq_attr_t;

/**
 * emb_msgq_attr_default() - Fill message queue attributes with defaults.
 * @out_attr: Receives name and ring NULL and every number 0, which init refuses until the
 *            ring, its size, the item size and the capacity are set; NULL is a no-op.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      API-020 KRN-IPC-010
 * @since    0.3
 * @stable   no
 */
void emb_msgq_attr_default(emb_msgq_attr_t *out_attr);

/**
 * emb_msgq_init() - Construct a message queue in caller-provided storage.
 * @storage: Aligned caller-owned storage, never initialized or destroyed; retain it until
 *           successful destruction.
 * @attr: Required attributes, copied; the ring stays borrowed. Keep name storage alive.
 * @out_msgq: Required output handle; cleared before argument validation.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1); O(active message queues) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     borrows:storage borrows:ring
 * @config   CONFIG_EMB_MSGQ
 * @req      API-021 KRN-IPC-002 KRN-IPC-010 KRN-IPC-011 KRN-IPC-019 KRN-OBJ-003 KRN-OBJ-007
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for bad storage or output, a missing, misaligned or
 *         undersized ring, an item size of 0 or above CONFIG_EMB_MSGQ_MAX_ITEM, or a
 *         capacity of 0 (or above 255 with CONFIG_EMB_IPC_SMALL); EMB_EBUSY when @storage
 *         still holds a message queue that was not destroyed (misuse; exact with
 *         CONFIG_EMB_OBJ_LIST). Every failure is misuse.
 */
emb_status_t emb_msgq_init(emb_msgq_storage_t *storage, const emb_msgq_attr_t *attr,
                           emb_msgq_t *out_msgq);

/**
 * emb_msgq_destroy() - End the message queue (KRN-OBJ-001); stored items are dropped.
 * @msgq: Initialized message queue; no waiters unless ABORT_WAITERS is set.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(waiters) with EMB_OBJ_ABORT_WAITERS
 * @owns     gives:storage gives:ring
 * @config   CONFIG_EMB_MSGQ
 * @req      KRN-OBJ-001 KRN-NOTIF-014
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EBUSY with waiters and no ABORT_WAITERS (misuse). Receivers and
 *         senders both leave with EMB_EDESTROYED; a binding is removed and its target
 *         released.
 */
emb_status_t emb_msgq_destroy(emb_msgq_t msgq);

/**
 * emb_msgq_send() - Copy an item to a waiting receiver, else into the tail, else wait.
 * @msgq:    Initialized message queue.
 * @item:    item_size bytes to copy; read until the call returns.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration); EMB_NO_WAIT only in
 *           an ISR.
 *
 * @ctx      thread isr
 * @blocks   timeout
 * @time     O(1) plus an item copy; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      KRN-IPC-010 KRN-IPC-011 KRN-IPC-003 KRN-IPC-005 KRN-IPC-018 KRN-WAIT-012
 * @since    0.3
 * @stable   no
 *
 * A waiting receiver gets the item directly and the ring is untouched (no binding
 * signal). A sender that waits for a slot is completed by the receive that frees it: its
 * item is moved into the ring before it wakes, and it does not re-check.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT when the queue stayed full (never a silent drop);
 *         EMB_ECANCELED; EMB_EDESTROYED; EMB_EINVAL for a NULL @item or a bad handle;
 *         EMB_EPERM (misuse) for a wait from an ISR or before the kernel starts.
 */
emb_status_t emb_msgq_send(emb_msgq_t msgq, const void *item, emb_timeout_t timeout);

/**
 * emb_msgq_send_until() - emb_msgq_send() with an absolute deadline.
 * @msgq:     Initialized message queue.
 * @item:     item_size bytes to copy.
 * @deadline: Kernel-clock deadline; a past deadline sends only if it need not wait.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     as emb_msgq_send()
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      API-024 KRN-TIM-011 KRN-IPC-010
 * @since    0.3
 * @stable   no
 *
 * Return: the statuses of emb_msgq_send().
 */
emb_status_t emb_msgq_send_until(emb_msgq_t msgq, const void *item, emb_instant_t deadline);

/**
 * emb_msgq_send_front() - Copy an urgent item to a waiting receiver, else to the head.
 * @msgq: Initialized message queue.
 * @item: item_size bytes to copy.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus an item copy
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      KRN-IPC-012 KRN-IPC-003
 * @since    0.3
 * @stable   no
 *
 * The item is the next one received. The call never waits and is not a cancellation
 * point.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT when the queue is full; EMB_EINVAL for a NULL @item or a
 *         bad handle; EMB_EPERM (misuse) before the kernel starts.
 */
emb_status_t emb_msgq_send_front(emb_msgq_t msgq, const void *item);

/**
 * emb_msgq_receive() - Copy out the head, refilled from a waiting sender, else wait.
 * @msgq:     Initialized message queue.
 * @out_item: Receives item_size bytes on EMB_OK; written by the sender while waiting.
 * @timeout:  EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration); EMB_NO_WAIT only in
 *            an ISR.
 *
 * @ctx      thread isr
 * @blocks   timeout
 * @time     O(1) plus up to two item copies; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      KRN-IPC-010 KRN-IPC-011 KRN-IPC-003 KRN-IPC-005 KRN-WAIT-012
 * @since    0.3
 * @stable   no
 *
 * When a sender waits, its item fills the freed slot in the same section, so the queue is
 * never seen empty and the binding is not signaled.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EDESTROYED; EMB_EINVAL for a NULL
 *         @out_item or a bad handle; EMB_EPERM (misuse) for a wait from an ISR or before
 *         the kernel starts. @out_item is written only on EMB_OK.
 */
emb_status_t emb_msgq_receive(emb_msgq_t msgq, void *out_item, emb_timeout_t timeout);

/**
 * emb_msgq_receive_until() - emb_msgq_receive() with an absolute deadline.
 * @msgq:     Initialized message queue.
 * @out_item: Receives item_size bytes on EMB_OK.
 * @deadline: Kernel-clock deadline; a past deadline receives only if it need not wait.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     as emb_msgq_receive()
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      API-024 KRN-TIM-011 KRN-IPC-010
 * @since    0.3
 * @stable   no
 *
 * Return: the statuses of emb_msgq_receive().
 */
emb_status_t emb_msgq_receive_until(emb_msgq_t msgq, void *out_item, emb_instant_t deadline);

/**
 * emb_msgq_peek() - Copy the head without removing it.
 * @msgq:     Initialized message queue.
 * @out_item: Receives item_size bytes on EMB_OK.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus an item copy
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      KRN-IPC-010
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ETIMEDOUT when the queue is empty; EMB_EINVAL for a NULL @out_item
 *         or a bad handle; EMB_EPERM (misuse) before the kernel starts.
 */
emb_status_t emb_msgq_peek(emb_msgq_t msgq, void *out_item);

/**
 * emb_msgq_count() - Snapshot of the items stored; 0 for a bad handle.
 * @msgq: Initialized message queue.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      KRN-IPC-010
 * @since    0.3
 * @stable   no
 */
uint16_t emb_msgq_count(emb_msgq_t msgq);

/**
 * emb_msgq_free() - Snapshot of the free slots; 0 for a bad handle.
 * @msgq: Initialized message queue.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MSGQ
 * @req      KRN-IPC-010
 * @since    0.3
 * @stable   no
 */
uint16_t emb_msgq_free(emb_msgq_t msgq);

#if CONFIG_EMB_NOTIFY
/**
 * emb_msgq_bind_notify() - Set @bit in @thread when the queue goes from empty to nonempty.
 * @msgq:   Initialized message queue with no binding.
 * @thread: Initialized, not terminated, application thread; it cannot be destroyed while
 *          bound (ADR-046).
 * @bit:    One application-bit mask, e.g. EMB_NOTIFY_BIT(0).
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MSGQ CONFIG_EMB_NOTIFY
 * @req      KRN-IPC-018 KRN-NOTIF-004 KRN-NOTIF-005 KRN-NOTIF-014
 * @since    0.3
 * @stable   no
 *
 * Return: the statuses of emb_sem_bind_notify(). An item handed directly to a waiting
 *         receiver does not signal the binding.
 */
emb_status_t emb_msgq_bind_notify(emb_msgq_t msgq, emb_thread_t thread, emb_notify_bits_t bit);

/**
 * emb_msgq_unbind_notify() - Remove the message queue's notification binding.
 * @msgq: Initialized message queue.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_MSGQ CONFIG_EMB_NOTIFY
 * @req      KRN-NOTIF-013
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOENT when not bound; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_msgq_unbind_notify(emb_msgq_t msgq);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_MSGQ_H */
