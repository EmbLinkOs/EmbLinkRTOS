/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Event flags (SPEC-005 §5): a word of bits that threads wait on with ANY or ALL of a
 * mask. A waiter keeps its mask in wake_data and its mode in the thread's bit-wait mode
 * (A38), so the control block does not grow. Set walks the waiters in queue order in one
 * section, hands each satisfied one its bits and clears them first for a CLEAR waiter.
 */
#include <emb/event.h>
#include <emb/storage.h>

#include <embk/binding.h>
#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/sync.h>
#include <embk/thread.h>
#include <embk/wait.h>

#if CONFIG_EMB_EVENT

EMB_STATIC_ASSERT(sizeof(embk_event_t) <= EMB_EVENT_STORAGE_SIZE, "generated storage too small");

#if CONFIG_EMB_OBJ_LIST
embk_list_t embk_event_all = EMBK_LIST_INITIALIZER(embk_event_all); /* usable before kernel init */
#endif

static EMB_INLINE embk_event_t *event_from_handle(emb_event_t h)
{
    return (embk_event_t *)embk_obj_from_raw(h.raw, EMBK_OBJ_EVENT, offsetof(embk_event_t, obj));
}

/* The bits a waiter with @mask and @mode is satisfied by, or 0. */
static emb_event_bits_t satisfying(emb_event_bits_t bits, emb_event_bits_t mask, uint8_t mode)
{
    emb_event_bits_t got = bits & mask;
    if ((mode & EMB_EVENT_ALL) != 0u) {
        return (got == mask) ? got : 0u;
    }
    return got;
}

void emb_event_attr_default(emb_event_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->initial = 0u;
    out_attr->flags = 0u;
    out_attr->reserved_[0] = 0u;
    out_attr->reserved_[1] = 0u;
    out_attr->reserved_[2] = 0u;
}

emb_status_t emb_event_init(emb_event_storage_t *storage, const emb_event_attr_t *attr,
                            emb_event_t *out_event)
{
    embk_event_t *e;
    emb_event_attr_t def;
#if CONFIG_EMB_OBJ_LIST
    emb_irq_key_t key;
#endif
    if (out_event != NULL) {
        out_event->raw = 0u;
    }
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_EVENT_INIT);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_INIT, storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_INIT, out_event != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     3u);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_INIT,
                     ((uintptr_t)storage % (uintptr_t)EMB_EVENT_STORAGE_ALIGN) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (attr == NULL) {
        emb_event_attr_default(&def);
        attr = &def;
    }
    e = (embk_event_t *)(void *)storage;
#if CONFIG_EMB_OBJ_LIST
    /* storage is reused only through destroy (SPEC-009 §6, KRN-OBJ-007) */
    key = embk_lock_sched();
    if (embk_list_contains(&embk_event_all, &e->all_node)) {
        embk_unlock_sched(key);
        EMBK_API_MISUSE(EMB_API_ID_EVENT_INIT, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
#endif
    embk_obj_init(&e->obj, EMBK_OBJ_EVENT, attr->flags & (EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO),
                  attr->name);
    embk_wait_queue_init(&e->waiters, ((attr->flags & EMB_OBJ_FIFO) != 0u) ? EMBK_WAIT_FIFO : 0u);
    e->bits = attr->initial;
#if CONFIG_EMB_NOTIFY
    embk_binding_init(&e->binding);
#endif
#if CONFIG_EMB_OBJ_LIST
    embk_list_append(&embk_event_all, &e->all_node);
    embk_unlock_sched(key);
#endif
    out_event->raw = embk_obj_to_raw(e);
    return EMB_OK;
}

emb_status_t emb_event_destroy(emb_event_t event)
{
    embk_event_t *e = event_from_handle(event);
    emb_irq_key_t key;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_EVENT_DESTROY);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_DESTROY, e != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(e);
    if (!embk_wait_queue_is_empty(&e->waiters)) {
        if ((e->obj.flags & EMB_OBJ_ABORT_WAITERS) == 0u) {
            embk_unlock_object(e, key);
            EMBK_API_MISUSE(EMB_API_ID_EVENT_DESTROY, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
        }
        (void)embk_wait_flush(&e->waiters, EMBK_WAKE_DESTROYED);
    }
#if CONFIG_EMB_NOTIFY
    (void)embk_binding_unbind(&e->binding); /* releases the target; its bits stay */
#endif
#if CONFIG_EMB_OBJ_LIST
    embk_list_remove(&e->all_node);
#endif
    e->obj.state = EMBK_OBJ_UNINIT;
    embk_sched_reschedule_if_needed();
    embk_unlock_object(e, key);
    return EMB_OK;
}

emb_status_t emb_event_set(emb_event_t event, emb_event_bits_t bits)
{
    embk_event_t *e = event_from_handle(event);
    embk_wait_iter_t it;
    embk_thread_t *w;
    emb_irq_key_t key;
    emb_event_bits_t before;
    unsigned woken = 0u;
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_SET, e != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(e);
    before = e->bits;
    e->bits |= bits;
    embk_wait_iter_begin(&e->waiters, &it);
    /* bounded by the waiters; one section, one reschedule (KRN-WAIT-013) */
    while ((w = embk_wait_iter_next(&e->waiters, &it)) != NULL) {
        uint8_t mode = embk_thread_notify_mode(w);
        emb_event_bits_t got = satisfying(e->bits, (emb_event_bits_t)w->wake_data, mode);
        if (got != 0u) {
            if ((mode & EMB_EVENT_CLEAR) != 0u) {
                e->bits &= (emb_event_bits_t)~got; /* before the next waiter is examined */
            }
            (void)embk_wait_wake(w, EMBK_WAKE_SATISFIED, (uintptr_t)got);
            woken++;
        }
    }
#if CONFIG_EMB_NOTIFY
    if (e->bits != 0u && e->bits != before) {
        embk_binding_signal(&e->binding, e); /* what direct waiters left (KRN-NOTIF-005) */
    }
#endif
    EMBK_TRACE(EMB_TRACE_EVENT_SET, woken, e, e->bits);
    (void)woken; /* only the trace reports it */
    embk_sched_reschedule_if_needed();
    embk_unlock_object(e, key);
    return EMB_OK;
}

emb_status_t emb_event_clear(emb_event_t event, emb_event_bits_t bits)
{
    embk_event_t *e = event_from_handle(event);
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_CLEAR, e != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(e);
    e->bits &= (emb_event_bits_t)~bits;
    embk_unlock_object(e, key);
    return EMB_OK;
}

emb_event_bits_t emb_event_get(emb_event_t event)
{
    const embk_event_t *e = event_from_handle(event);
    return (e == NULL) ? 0u : e->bits;
}

static emb_status_t wait_common(uint16_t api_id, embk_event_t *e, emb_event_bits_t mask,
                                uint8_t mode, emb_tick_t deadline, bool nowait,
                                emb_event_bits_t *out_satisfied)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    emb_event_bits_t got;
    EMBK_API_REQUIRE_THREAD(api_id);
    if (e == NULL) {
        EMBK_TRACE(EMB_TRACE_EVENT_WAIT, embk_thread_index(embk_cpu.current), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(api_id, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    EMBK_API_REQUIRE(api_id, mask != 0u, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE(api_id, (mode & (uint8_t) ~(EMB_EVENT_ALL | EMB_EVENT_CLEAR)) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_API_REQUIRE_CAN_BLOCK(api_id, nowait ? 0u : 1u);
    self = embk_cpu.current;
    key = embk_lock_object(e);
    if (embk_thread_take_cancel_locked(self)) {
        EMBK_TRACE(EMB_TRACE_EVENT_WAIT, embk_thread_index(self), e, EMB_ECANCELED);
        embk_unlock_object(e, key);
        return EMB_ECANCELED;
    }
    got = satisfying(e->bits, mask, mode);
    if (got != 0u) {
        if ((mode & EMB_EVENT_CLEAR) != 0u) {
            e->bits &= (emb_event_bits_t)~got;
        }
        EMBK_TRACE(EMB_TRACE_EVENT_WAIT, embk_thread_index(self), e, EMB_OK);
        embk_unlock_object(e, key);
        if (out_satisfied != NULL) {
            *out_satisfied = got;
        }
        return EMB_OK;
    }
    if (nowait) {
        EMBK_TRACE(EMB_TRACE_EVENT_WAIT, embk_thread_index(self), e, EMB_ETIMEDOUT);
        embk_unlock_object(e, key);
        return EMB_ETIMEDOUT;
    }
    self->wake_data = (uintptr_t)mask; /* the wait's arguments until a waker replaces them */
    embk_thread_set_notify_mode(self, mode);
    r = embk_block_on(&e->waiters, EMB_WAIT_REASON_OBJECT, deadline, key, &data);
    EMBK_TRACE(EMB_TRACE_EVENT_WAIT, embk_thread_index(self), e, embk_wait_result_status(r));
    if (r == EMBK_WAKE_SATISFIED && out_satisfied != NULL) {
        *out_satisfied = (emb_event_bits_t)data;
    }
    return embk_wait_result_status(r);
}

emb_status_t emb_event_wait(emb_event_t event, emb_event_bits_t mask, uint8_t mode,
                            emb_timeout_t timeout, emb_event_bits_t *out_satisfied)
{
    return wait_common(EMB_API_ID_EVENT_WAIT, event_from_handle(event), mask, mode,
                       embk_deadline_from_timeout(timeout), EMB_TIMEOUT_IS_NO_WAIT(timeout),
                       out_satisfied);
}

emb_status_t emb_event_wait_until(emb_event_t event, emb_event_bits_t mask, uint8_t mode,
                                  emb_instant_t deadline, emb_event_bits_t *out_satisfied)
{
    bool nowait = deadline.ticks != EMB_TICK_MAX && embk_deadline_passed(deadline.ticks);
    return wait_common(EMB_API_ID_EVENT_WAIT_UNTIL, event_from_handle(event), mask, mode,
                       deadline.ticks, nowait, out_satisfied);
}

#if CONFIG_EMB_NOTIFY
emb_status_t emb_event_bind_notify(emb_event_t event, emb_thread_t thread, emb_notify_bits_t bit)
{
    embk_event_t *e = event_from_handle(event);
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_EVENT_BIND_NOTIFY);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_BIND_NOTIFY, e != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_BIND_NOTIFY, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_BIND_NOTIFY,
                     bit != 0u && (bit & (emb_notify_bits_t)(bit - 1u)) == 0u &&
                         (bit & (emb_notify_bits_t)~EMB_NOTIFY_APP_MASK) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    key = embk_lock_object(e);
    st = embk_binding_bind(&e->binding, t, bit);
    embk_unlock_object(e, key);
    return st;
}

emb_status_t emb_event_unbind_notify(emb_event_t event)
{
    embk_event_t *e = event_from_handle(event);
    emb_irq_key_t key;
    bool bound;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_EVENT_UNBIND_NOTIFY);
    EMBK_API_REQUIRE(EMB_API_ID_EVENT_UNBIND_NOTIFY, e != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL,
                     1u);
    key = embk_lock_object(e);
    bound = embk_binding_unbind(&e->binding);
    embk_unlock_object(e, key);
    return bound ? EMB_OK : EMB_ENOENT;
}
#endif

#endif /* CONFIG_EMB_EVENT */
