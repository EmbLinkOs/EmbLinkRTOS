/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Message queues (SPEC-007 §2): fixed-size items copied by value through a caller's ring,
 * with a wait queue for receivers (while empty) and one for senders (while full). A send
 * copies straight into a waiting receiver's out_item, whose pointer waits in wake_data; a
 * receive that frees a slot moves the first waiting sender's item, whose pointer waits in
 * wake_data likewise, into the ring. A woken thread never re-checks. Each section copies
 * at most two items of at most CONFIG_EMB_MSGQ_MAX_ITEM bytes (KRN-IPC-011).
 */
#include <emb/msgq.h>
#include <emb/storage.h>

#include <embk/binding.h>
#include <embk/ipc.h>
#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/thread.h>
#include <embk/wait.h>

#if CONFIG_EMB_MSGQ

EMB_STATIC_ASSERT(sizeof(embk_msgq_t) <= EMB_MSGQ_STORAGE_SIZE, "generated storage too small");
EMB_STATIC_ASSERT(!CONFIG_EMB_IPC_SMALL || CONFIG_EMB_MSGQ_MAX_ITEM <= 255,
                  "an 8-bit count cannot hold the item size");

#if CONFIG_EMB_OBJ_LIST
embk_list_t embk_msgq_all = EMBK_LIST_INITIALIZER(embk_msgq_all); /* usable before kernel init */
#endif

static EMB_INLINE embk_msgq_t *msgq_from_handle(emb_msgq_t h)
{
    return (embk_msgq_t *)embk_obj_from_raw(h.raw, EMBK_OBJ_MSGQ, offsetof(embk_msgq_t, obj));
}

/* The kernel does not depend on the C library; @n is at most CONFIG_EMB_MSGQ_MAX_ITEM. */
static void copy_item(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n != 0u) {
        *d++ = *s++;
        n--;
    }
}

/* The slot @offset items after the head; @offset is below the capacity. */
static unsigned char *slot(const embk_msgq_t *q, unsigned offset)
{
    unsigned to_end = (unsigned)q->capacity - q->head;
    unsigned i = (offset < to_end) ? q->head + offset : offset - to_end;
    return q->ring + ((size_t)i * q->item_size);
}

/* Store @item at the tail, or at the head when @front; lock held, a slot free. */
static void store_locked(embk_msgq_t *q, const void *item, bool front)
{
    if (front) {
        q->head = (embk_ipc_count_t)((q->head == 0u) ? q->capacity - 1u : q->head - 1u);
        copy_item(slot(q, 0u), item, q->item_size);
    } else {
        copy_item(slot(q, q->count), item, q->item_size);
    }
    q->count++;
#if CONFIG_EMB_NOTIFY
    if (q->count == 1u) {
        embk_binding_signal(&q->binding, q); /* empty to nonempty (KRN-IPC-018) */
    }
#endif
}

/* The part of a send that never waits; lock held. False when the ring is full. */
static bool put_locked(embk_msgq_t *q, const void *item, bool front)
{
    embk_thread_t *w = embk_wait_first(&q->receivers);
    if (w != NULL) {
        copy_item((void *)w->wake_data, item, q->item_size); /* into its out_item */
        (void)embk_wait_wake(w, EMBK_WAKE_SATISFIED, 0u);
        return true;
    }
    if (q->count == q->capacity) {
        return false;
    }
    store_locked(q, item, front);
    return true;
}

/* Copy out the head and refill the freed slot from the first waiting sender; lock held,
 * the ring not empty. The ring is never seen empty here, so the binding is not signaled. */
static void take_locked(embk_msgq_t *q, void *out_item)
{
    embk_thread_t *w;
    copy_item(out_item, slot(q, 0u), q->item_size);
    q->head = (embk_ipc_count_t)((q->head + 1u == q->capacity) ? 0u : q->head + 1u);
    q->count--;
    w = embk_wait_first(&q->senders);
    if (w != NULL) {
        copy_item(slot(q, q->count), (const void *)w->wake_data, q->item_size);
        q->count++;
        (void)embk_wait_wake(w, EMBK_WAKE_SATISFIED, 0u);
    }
}

void emb_msgq_attr_default(emb_msgq_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->ring = NULL;
    out_attr->ring_size = 0u;
    out_attr->item_size = 0u;
    out_attr->capacity = 0u;
    out_attr->flags = 0u;
    out_attr->reserved_[0] = 0u;
    out_attr->reserved_[1] = 0u;
    out_attr->reserved_[2] = 0u;
}

emb_status_t emb_msgq_init(emb_msgq_storage_t *storage, const emb_msgq_attr_t *attr,
                           emb_msgq_t *out_msgq)
{
    embk_msgq_t *q;
#if CONFIG_EMB_OBJ_LIST
    emb_irq_key_t key;
#endif
    if (out_msgq != NULL) {
        out_msgq->raw = 0u;
    }
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_MSGQ_INIT);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_INIT, storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_INIT, attr != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_INIT, out_msgq != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     3u);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_INIT,
                     ((uintptr_t)storage % (uintptr_t)EMB_MSGQ_STORAGE_ALIGN) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    /* SPEC-007 §2.2, §7: the ring must hold capacity items; division avoids overflow */
    EMBK_API_REQUIRE(
        EMB_API_ID_MSGQ_INIT,
        attr->ring != NULL && ((uintptr_t)attr->ring % (uintptr_t)EMB_MSGQ_ALIGN) == 0u &&
            attr->item_size != 0u && attr->item_size <= CONFIG_EMB_MSGQ_MAX_ITEM &&
            attr->capacity != 0u && (!CONFIG_EMB_IPC_SMALL || attr->capacity <= 255u) &&
            attr->ring_size / attr->item_size >= attr->capacity,
        EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    q = (embk_msgq_t *)(void *)storage;
#if CONFIG_EMB_OBJ_LIST
    /* storage is reused only through destroy (SPEC-009 §6, KRN-OBJ-007) */
    key = embk_lock_sched();
    if (embk_list_contains(&embk_msgq_all, &q->all_node)) {
        embk_unlock_sched(key);
        EMBK_API_MISUSE(EMB_API_ID_MSGQ_INIT, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
#endif
    embk_obj_init(&q->obj, EMBK_OBJ_MSGQ, attr->flags & (EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO),
                  attr->name);
    embk_wait_queue_init(&q->receivers, ((attr->flags & EMB_OBJ_FIFO) != 0u) ? EMBK_WAIT_FIFO : 0u);
    embk_wait_queue_init(&q->senders, ((attr->flags & EMB_OBJ_FIFO) != 0u) ? EMBK_WAIT_FIFO : 0u);
    q->ring = (unsigned char *)attr->ring;
    q->item_size = (embk_ipc_count_t)attr->item_size;
    q->capacity = (embk_ipc_count_t)attr->capacity;
    q->head = 0u;
    q->count = 0u;
#if CONFIG_EMB_NOTIFY
    embk_binding_init(&q->binding);
#endif
#if CONFIG_EMB_OBJ_LIST
    embk_list_append(&embk_msgq_all, &q->all_node);
    embk_unlock_sched(key);
#endif
    out_msgq->raw = embk_obj_to_raw(q);
    return EMB_OK;
}

emb_status_t emb_msgq_destroy(emb_msgq_t msgq)
{
    embk_msgq_t *q = msgq_from_handle(msgq);
    emb_irq_key_t key;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_MSGQ_DESTROY);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_DESTROY, q != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(q);
    if (!embk_wait_queue_is_empty(&q->receivers) || !embk_wait_queue_is_empty(&q->senders)) {
        if ((q->obj.flags & EMB_OBJ_ABORT_WAITERS) == 0u) {
            embk_unlock_object(q, key);
            EMBK_API_MISUSE(EMB_API_ID_MSGQ_DESTROY, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
        }
        (void)embk_wait_flush(&q->receivers, EMBK_WAKE_DESTROYED);
        (void)embk_wait_flush(&q->senders, EMBK_WAKE_DESTROYED);
    }
#if CONFIG_EMB_NOTIFY
    (void)embk_binding_unbind(&q->binding); /* releases the target */
#endif
#if CONFIG_EMB_OBJ_LIST
    embk_list_remove(&q->all_node);
#endif
    q->count = 0u; /* stored items are dropped (SPEC-007 §2.2) */
    q->obj.state = EMBK_OBJ_UNINIT;
    embk_sched_reschedule_if_needed();
    embk_unlock_object(q, key);
    return EMB_OK;
}

/* Send and receive run in a thread, or in an ISR with EMB_NO_WAIT (SPEC-007 §1). */
static EMB_INLINE bool msgq_context_ok(void)
{
    return embk_in_thread() || embk_in_isr();
}

static emb_status_t send_common(uint16_t api_id, embk_msgq_t *q, const void *item,
                                emb_tick_t deadline, bool nowait)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    EMBK_API_REQUIRE(api_id, msgq_context_ok(), EMB_FAULT_API_CONTEXT, EMB_EPERM, 0u);
    self = embk_cpu.current;
    if (q == NULL) {
        EMBK_TRACE(EMB_TRACE_MSGQ_SEND, embk_thread_index(self), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(api_id, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    EMBK_API_REQUIRE(api_id, item != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE_CAN_BLOCK(api_id, nowait ? 0u : 1u);
    key = embk_lock_object(q);
    if (!embk_in_isr() && embk_thread_take_cancel_locked(self)) {
        EMBK_TRACE(EMB_TRACE_MSGQ_SEND, embk_thread_index(self), q, EMB_ECANCELED);
        embk_unlock_object(q, key);
        return EMB_ECANCELED;
    }
    if (put_locked(q, item, false)) {
        EMBK_TRACE(EMB_TRACE_MSGQ_SEND, embk_thread_index(self), q, EMB_OK);
        embk_sched_reschedule_if_needed();
        embk_unlock_object(q, key);
        return EMB_OK;
    }
    if (nowait) {
        EMBK_TRACE(EMB_TRACE_MSGQ_SEND, embk_thread_index(self), q, EMB_ETIMEDOUT);
        embk_unlock_object(q, key);
        return EMB_ETIMEDOUT;
    }
    self->wake_data = (uintptr_t)item; /* until the receive that frees a slot copies it */
    r = embk_block_on(&q->senders, EMB_WAIT_REASON_OBJECT, deadline, key, &data);
    (void)data;
    EMBK_TRACE(EMB_TRACE_MSGQ_SEND, embk_thread_index(self), q, embk_wait_result_status(r));
    return embk_wait_result_status(r);
}

emb_status_t emb_msgq_send(emb_msgq_t msgq, const void *item, emb_timeout_t timeout)
{
    return send_common(EMB_API_ID_MSGQ_SEND, msgq_from_handle(msgq), item,
                       embk_deadline_from_timeout(timeout), EMB_TIMEOUT_IS_NO_WAIT(timeout));
}

emb_status_t emb_msgq_send_until(emb_msgq_t msgq, const void *item, emb_instant_t deadline)
{
    bool nowait = deadline.ticks != EMB_TICK_MAX && embk_deadline_passed(deadline.ticks);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_MSGQ_SEND_UNTIL);
    return send_common(EMB_API_ID_MSGQ_SEND_UNTIL, msgq_from_handle(msgq), item, deadline.ticks,
                       nowait);
}

emb_status_t emb_msgq_send_front(emb_msgq_t msgq, const void *item)
{
    embk_msgq_t *q = msgq_from_handle(msgq);
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_SEND_FRONT, msgq_context_ok(), EMB_FAULT_API_CONTEXT,
                     EMB_EPERM, 0u);
    if (q == NULL) {
        EMBK_TRACE(EMB_TRACE_MSGQ_SEND, embk_thread_index(embk_cpu.current), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(EMB_API_ID_MSGQ_SEND_FRONT, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_SEND_FRONT, item != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     2u);
    key = embk_lock_object(q);
    st = put_locked(q, item, true) ? EMB_OK : EMB_ETIMEDOUT;
    EMBK_TRACE(EMB_TRACE_MSGQ_SEND, embk_thread_index(embk_cpu.current), q, st);
    embk_sched_reschedule_if_needed();
    embk_unlock_object(q, key);
    return st;
}

static emb_status_t receive_common(uint16_t api_id, embk_msgq_t *q, void *out_item,
                                   emb_tick_t deadline, bool nowait)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    EMBK_API_REQUIRE(api_id, msgq_context_ok(), EMB_FAULT_API_CONTEXT, EMB_EPERM, 0u);
    self = embk_cpu.current;
    if (q == NULL) {
        EMBK_TRACE(EMB_TRACE_MSGQ_RECEIVE, embk_thread_index(self), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(api_id, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    EMBK_API_REQUIRE(api_id, out_item != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE_CAN_BLOCK(api_id, nowait ? 0u : 1u);
    key = embk_lock_object(q);
    if (!embk_in_isr() && embk_thread_take_cancel_locked(self)) {
        EMBK_TRACE(EMB_TRACE_MSGQ_RECEIVE, embk_thread_index(self), q, EMB_ECANCELED);
        embk_unlock_object(q, key);
        return EMB_ECANCELED;
    }
    if (q->count != 0u) {
        take_locked(q, out_item);
        EMBK_TRACE(EMB_TRACE_MSGQ_RECEIVE, embk_thread_index(self), q, EMB_OK);
        embk_sched_reschedule_if_needed();
        embk_unlock_object(q, key);
        return EMB_OK;
    }
    if (nowait) {
        EMBK_TRACE(EMB_TRACE_MSGQ_RECEIVE, embk_thread_index(self), q, EMB_ETIMEDOUT);
        embk_unlock_object(q, key);
        return EMB_ETIMEDOUT;
    }
    self->wake_data = (uintptr_t)out_item; /* the sender copies into it before the wake */
    r = embk_block_on(&q->receivers, EMB_WAIT_REASON_OBJECT, deadline, key, &data);
    (void)data;
    EMBK_TRACE(EMB_TRACE_MSGQ_RECEIVE, embk_thread_index(self), q, embk_wait_result_status(r));
    return embk_wait_result_status(r);
}

emb_status_t emb_msgq_receive(emb_msgq_t msgq, void *out_item, emb_timeout_t timeout)
{
    return receive_common(EMB_API_ID_MSGQ_RECEIVE, msgq_from_handle(msgq), out_item,
                          embk_deadline_from_timeout(timeout), EMB_TIMEOUT_IS_NO_WAIT(timeout));
}

emb_status_t emb_msgq_receive_until(emb_msgq_t msgq, void *out_item, emb_instant_t deadline)
{
    bool nowait = deadline.ticks != EMB_TICK_MAX && embk_deadline_passed(deadline.ticks);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_MSGQ_RECEIVE_UNTIL);
    return receive_common(EMB_API_ID_MSGQ_RECEIVE_UNTIL, msgq_from_handle(msgq), out_item,
                          deadline.ticks, nowait);
}

emb_status_t emb_msgq_peek(emb_msgq_t msgq, void *out_item)
{
    embk_msgq_t *q = msgq_from_handle(msgq);
    emb_irq_key_t key;
    emb_status_t st = EMB_ETIMEDOUT;
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_PEEK, msgq_context_ok(), EMB_FAULT_API_CONTEXT, EMB_EPERM, 0u);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_PEEK, q != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_PEEK, out_item != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     2u);
    key = embk_lock_object(q);
    if (q->count != 0u) {
        copy_item(out_item, slot(q, 0u), q->item_size);
        st = EMB_OK;
    }
    embk_unlock_object(q, key);
    return st;
}

uint16_t emb_msgq_count(emb_msgq_t msgq)
{
    const embk_msgq_t *q = msgq_from_handle(msgq);
    return (q == NULL) ? 0u : q->count;
}

uint16_t emb_msgq_free(emb_msgq_t msgq)
{
    const embk_msgq_t *q = msgq_from_handle(msgq);
    return (q == NULL) ? 0u : (uint16_t)(q->capacity - q->count);
}

#if CONFIG_EMB_NOTIFY
emb_status_t emb_msgq_bind_notify(emb_msgq_t msgq, emb_thread_t thread, emb_notify_bits_t bit)
{
    embk_msgq_t *q = msgq_from_handle(msgq);
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_MSGQ_BIND_NOTIFY);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_BIND_NOTIFY, q != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_BIND_NOTIFY, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_BIND_NOTIFY,
                     bit != 0u && (bit & (emb_notify_bits_t)(bit - 1u)) == 0u &&
                         (bit & (emb_notify_bits_t)~EMB_NOTIFY_APP_MASK) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    key = embk_lock_object(q);
    st = embk_binding_bind(&q->binding, t, bit);
    embk_unlock_object(q, key);
    return st;
}

emb_status_t emb_msgq_unbind_notify(emb_msgq_t msgq)
{
    embk_msgq_t *q = msgq_from_handle(msgq);
    emb_irq_key_t key;
    bool bound;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_MSGQ_UNBIND_NOTIFY);
    EMBK_API_REQUIRE(EMB_API_ID_MSGQ_UNBIND_NOTIFY, q != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL,
                     1u);
    key = embk_lock_object(q);
    bound = embk_binding_unbind(&q->binding);
    embk_unlock_object(q, key);
    return bound ? EMB_OK : EMB_ENOENT;
}
#endif

#endif /* CONFIG_EMB_MSGQ */
