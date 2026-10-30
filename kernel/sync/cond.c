/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Condition variables (SPEC-005 §6). A wait enqueues the caller and releases its mutex in
 * one section, so no signal sent after the release can be missed; there is no stored
 * signal and no spurious wakeup. Every completion relocks the mutex before returning, with
 * cancellation deferred so that the caller always owns it again (A41).
 */
#include <emb/cond.h>
#include <emb/storage.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/sync.h>
#include <embk/thread.h>
#include <embk/wait.h>

#if CONFIG_EMB_COND

EMB_STATIC_ASSERT(sizeof(embk_cond_t) <= EMB_COND_STORAGE_SIZE, "generated storage too small");

#if CONFIG_EMB_OBJ_LIST
embk_list_t embk_cond_all = EMBK_LIST_INITIALIZER(embk_cond_all); /* usable before kernel init */
#endif

static EMB_INLINE embk_cond_t *cond_from_handle(emb_cond_t h)
{
    return (embk_cond_t *)embk_obj_from_raw(h.raw, EMBK_OBJ_COND, offsetof(embk_cond_t, obj));
}

void emb_cond_attr_default(emb_cond_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->flags = 0u;
    out_attr->reserved_[0] = 0u;
    out_attr->reserved_[1] = 0u;
    out_attr->reserved_[2] = 0u;
}

emb_status_t emb_cond_init(emb_cond_storage_t *storage, const emb_cond_attr_t *attr,
                           emb_cond_t *out_cond)
{
    embk_cond_t *c;
    emb_cond_attr_t def;
#if CONFIG_EMB_OBJ_LIST
    emb_irq_key_t key;
#endif
    if (out_cond != NULL) {
        out_cond->raw = 0u;
    }
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_COND_INIT);
    EMBK_API_REQUIRE(EMB_API_ID_COND_INIT, storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_COND_INIT, out_cond != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     3u);
    EMBK_API_REQUIRE(EMB_API_ID_COND_INIT,
                     ((uintptr_t)storage % (uintptr_t)EMB_COND_STORAGE_ALIGN) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (attr == NULL) {
        emb_cond_attr_default(&def);
        attr = &def;
    }
    c = (embk_cond_t *)(void *)storage;
#if CONFIG_EMB_OBJ_LIST
    /* storage is reused only through destroy (SPEC-009 §6, KRN-OBJ-007) */
    key = embk_lock_sched();
    if (embk_list_contains(&embk_cond_all, &c->all_node)) {
        embk_unlock_sched(key);
        EMBK_API_MISUSE(EMB_API_ID_COND_INIT, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
#endif
    embk_obj_init(&c->obj, EMBK_OBJ_COND, attr->flags & (EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO),
                  attr->name);
    embk_wait_queue_init(&c->waiters, ((attr->flags & EMB_OBJ_FIFO) != 0u) ? EMBK_WAIT_FIFO : 0u);
#if CONFIG_EMB_OBJ_LIST
    embk_list_append(&embk_cond_all, &c->all_node);
    embk_unlock_sched(key);
#endif
    out_cond->raw = embk_obj_to_raw(c);
    return EMB_OK;
}

emb_status_t emb_cond_destroy(emb_cond_t cond)
{
    embk_cond_t *c = cond_from_handle(cond);
    emb_irq_key_t key;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_COND_DESTROY);
    EMBK_API_REQUIRE(EMB_API_ID_COND_DESTROY, c != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(c);
    if (!embk_wait_queue_is_empty(&c->waiters)) {
        if ((c->obj.flags & EMB_OBJ_ABORT_WAITERS) == 0u) {
            embk_unlock_object(c, key);
            EMBK_API_MISUSE(EMB_API_ID_COND_DESTROY, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
        }
        (void)embk_wait_flush(&c->waiters, EMBK_WAKE_DESTROYED);
    }
#if CONFIG_EMB_OBJ_LIST
    embk_list_remove(&c->all_node);
#endif
    c->obj.state = EMBK_OBJ_UNINIT;
    embk_sched_reschedule_if_needed();
    embk_unlock_object(c, key);
    return EMB_OK;
}

static emb_status_t wait_common(uint16_t api_id, embk_cond_t *c, embk_mutex_t *m,
                                emb_tick_t deadline, bool nowait)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    emb_status_t st;
    bool deferred;
    EMBK_API_REQUIRE_THREAD(api_id);
    EMBK_API_REQUIRE(api_id, c != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(api_id, m != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE_CAN_BLOCK(api_id, nowait ? 0u : 1u);
    self = embk_cpu.current;
    key = embk_lock_object(c);
    if (m->owner != self) {
        embk_unlock_object(c, key);
        EMBK_API_MISUSE(api_id, EMB_FAULT_API_OWNER, EMB_EPERM, 2u);
    }
    if (m->count != 1u) {
        EMBK_TRACE(EMB_TRACE_COND_WAIT, embk_thread_index(self), c, EMB_ESTATE);
        embk_unlock_object(c, key);
        return EMB_ESTATE; /* a recursive hold cannot be released and restored (§6) */
    }
    if (embk_thread_take_cancel_locked(self)) {
        EMBK_TRACE(EMB_TRACE_COND_WAIT, embk_thread_index(self), c, EMB_ECANCELED);
        embk_unlock_object(c, key);
        return EMB_ECANCELED; /* the mutex was never released */
    }
    if (nowait) {
        EMBK_TRACE(EMB_TRACE_COND_WAIT, embk_thread_index(self), c, EMB_ETIMEDOUT);
        embk_unlock_object(c, key);
        return EMB_ETIMEDOUT; /* no stored signal to find */
    }
    embk_wait_prepare(&c->waiters, EMB_WAIT_REASON_OBJECT);
    embk_mutex_release_locked(m, self); /* enqueue, then release: atomic for signal */
    embk_unlock_object(c, key);
    r = embk_wait_commit(deadline, &data);
    /* the mandatory relock is not cancelable: the caller must own the mutex on return */
    deferred = self->cancel_disable < UINT8_MAX;
    if (deferred) {
        self->cancel_disable++; /* own field, thread context */
    }
    st = embk_mutex_lock_common(api_id, m, EMBK_DEADLINE_FOREVER, false);
    if (deferred) {
        self->cancel_disable--;
    }
    if (st == EMB_OK) {
        st = embk_wait_result_status(r); /* the relock's EOWNERDEAD or failure wins */
    }
    EMBK_TRACE(EMB_TRACE_COND_WAIT, embk_thread_index(self), c, st);
    return st;
}

emb_status_t emb_cond_wait(emb_cond_t cond, emb_mutex_t mutex, emb_timeout_t timeout)
{
    return wait_common(EMB_API_ID_COND_WAIT, cond_from_handle(cond), embk_mutex_from_handle(mutex),
                       embk_deadline_from_timeout(timeout), EMB_TIMEOUT_IS_NO_WAIT(timeout));
}

emb_status_t emb_cond_wait_until(emb_cond_t cond, emb_mutex_t mutex, emb_instant_t deadline)
{
    bool nowait = deadline.ticks != EMB_TICK_MAX && embk_deadline_passed(deadline.ticks);
    return wait_common(EMB_API_ID_COND_WAIT_UNTIL, cond_from_handle(cond),
                       embk_mutex_from_handle(mutex), deadline.ticks, nowait);
}

static emb_status_t wake(uint16_t api_id, emb_cond_t cond, bool all)
{
    embk_cond_t *c = cond_from_handle(cond);
    emb_irq_key_t key;
    unsigned woken = 0u;
    embk_thread_t *w;
    EMBK_API_REQUIRE_THREAD(api_id);
    EMBK_API_REQUIRE(api_id, c != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(c);
    if (all) {
        woken = embk_wait_wake_all(&c->waiters, EMBK_WAKE_SATISFIED, 0u);
    } else {
        w = embk_wait_first(&c->waiters);
        if (w != NULL) {
            (void)embk_wait_wake(w, EMBK_WAKE_SATISFIED, 0u);
            woken = 1u;
        }
    }
    EMBK_TRACE(EMB_TRACE_COND_SIGNAL, woken, c, all);
    (void)woken; /* only the trace reports it */
    embk_sched_reschedule_if_needed();
    embk_unlock_object(c, key);
    return EMB_OK;
}

emb_status_t emb_cond_signal(emb_cond_t cond)
{
    return wake(EMB_API_ID_COND_SIGNAL, cond, false);
}

emb_status_t emb_cond_broadcast(emb_cond_t cond)
{
    return wake(EMB_API_ID_COND_BROADCAST, cond, true);
}

#endif /* CONFIG_EMB_COND */
