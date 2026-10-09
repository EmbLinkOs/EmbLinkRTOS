/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Counting semaphores (SPEC-005 §4): hand-off to the first waiter, no owner, ISR-safe give. */
#include <emb/sem.h>
#include <emb/storage.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/sync.h>
#include <embk/thread.h>
#include <embk/wait.h>

#if CONFIG_EMB_SEM

EMB_STATIC_ASSERT(sizeof(embk_sem_t) <= EMB_SEM_STORAGE_SIZE, "generated storage too small");

static EMB_INLINE embk_sem_t *sem_from_handle(emb_sem_t h)
{
    return (embk_sem_t *)embk_obj_from_raw(h.raw, EMBK_OBJ_SEM, offsetof(embk_sem_t, obj));
}

void emb_sem_attr_default(emb_sem_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->initial = 0u;
    out_attr->max = EMB_SEM_COUNT_MAX;
    out_attr->flags = 0u;
    out_attr->reserved_[0] = 0u;
    out_attr->reserved_[1] = 0u;
    out_attr->reserved_[2] = 0u;
}

void emb_sem_attr_binary(emb_sem_attr_t *out_attr, bool initially_available)
{
    emb_sem_attr_default(out_attr);
    if (out_attr == NULL) {
        return;
    }
    out_attr->max = 1u;
    out_attr->initial = initially_available ? 1u : 0u;
    out_attr->flags = EMB_SEM_SATURATE;
}

emb_status_t emb_sem_init(emb_sem_storage_t *storage, const emb_sem_attr_t *attr,
                          emb_sem_t *out_sem)
{
    embk_sem_t *s;
    emb_sem_attr_t def;
    if (out_sem != NULL) {
        out_sem->raw = 0u;
    }
    EMBK_REQUIRE_NOT_ISR();
    EMBK_REQUIRE(storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_REQUIRE(out_sem != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_REQUIRE(((uintptr_t)storage % (uintptr_t)EMB_SEM_STORAGE_ALIGN) == 0u,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (attr == NULL) {
        emb_sem_attr_default(&def);
        attr = &def;
    }
    EMBK_REQUIRE(attr->max >= 1u && attr->initial <= attr->max, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                 2u);
    s = (embk_sem_t *)(void *)storage;
    embk_obj_init(&s->obj, EMBK_OBJ_SEM,
                  attr->flags & (EMB_SEM_SATURATE | EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO),
                  attr->name);
    embk_wait_queue_init(&s->waiters, ((attr->flags & EMB_OBJ_FIFO) != 0u) ? EMBK_WAIT_FIFO : 0u);
    s->count = attr->initial;
    s->max = attr->max;
#if CONFIG_EMB_NOTIFY
    s->bind_thread = NULL;
    s->bind_bit = 0u;
#endif
    out_sem->raw = embk_obj_to_raw(s);
    return EMB_OK;
}

emb_status_t emb_sem_destroy(emb_sem_t sem)
{
    embk_sem_t *s = sem_from_handle(sem);
    emb_irq_key_t key;
    EMBK_REQUIRE_NOT_ISR();
    EMBK_REQUIRE(s != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(s);
    if (!embk_wait_queue_is_empty(&s->waiters)) {
        if ((s->obj.flags & EMB_OBJ_ABORT_WAITERS) == 0u) {
            embk_unlock_object(s, key);
            EMBK_MISUSE(EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
        }
        (void)embk_wait_flush(&s->waiters, EMBK_WAKE_DESTROYED);
    }
    s->obj.state = EMBK_OBJ_UNINIT;
    embk_sched_reschedule_if_needed();
    embk_unlock_object(s, key);
    return EMB_OK;
}

static emb_status_t take_common(embk_sem_t *s, emb_tick_t deadline, bool nowait)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    EMBK_REQUIRE_THREAD();
    if (s == NULL) {
        EMBK_TRACE(EMB_TRACE_SEM_TAKE, embk_thread_index(embk_cpu.current), 0u, EMB_EINVAL);
        EMBK_MISUSE(EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    EMBK_REQUIRE_CAN_BLOCK(nowait ? 0u : 1u);
    self = embk_cpu.current;
    key = embk_lock_object(s);
    if (embk_thread_take_cancel_locked(self)) {
        EMBK_TRACE(EMB_TRACE_SEM_TAKE, embk_thread_index(self), s, EMB_ECANCELED);
        embk_unlock_object(s, key);
        return EMB_ECANCELED;
    }
    if (s->count > 0u) {
        s->count--;
        EMBK_TRACE(EMB_TRACE_SEM_TAKE, embk_thread_index(self), s, EMB_OK);
        embk_unlock_object(s, key);
        return EMB_OK;
    }
    if (nowait) {
        EMBK_TRACE(EMB_TRACE_SEM_TAKE, embk_thread_index(self), s, EMB_ETIMEDOUT);
        embk_unlock_object(s, key);
        return EMB_ETIMEDOUT;
    }
    r = embk_block_on(&s->waiters, EMB_WAIT_REASON_OBJECT, deadline, key, &data);
    EMBK_TRACE(EMB_TRACE_SEM_TAKE, embk_thread_index(self), s, embk_wait_result_status(r));
    return embk_wait_result_status(r); /* the unit arrived by hand-off */
}

emb_status_t emb_sem_take(emb_sem_t sem, emb_timeout_t timeout)
{
    return take_common(sem_from_handle(sem), embk_deadline_from_timeout(timeout),
                       EMB_TIMEOUT_IS_NO_WAIT(timeout));
}

emb_status_t emb_sem_take_until(emb_sem_t sem, emb_instant_t deadline)
{
    bool nowait = deadline.ticks != EMB_TICK_MAX && embk_deadline_passed(deadline.ticks);
    return take_common(sem_from_handle(sem), deadline.ticks, nowait);
}

emb_status_t emb_sem_give(emb_sem_t sem)
{
    embk_sem_t *s = sem_from_handle(sem);
    embk_thread_t *w;
    emb_irq_key_t key;
    EMBK_REQUIRE(s != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(s);
    w = embk_wait_first(&s->waiters);
    if (w != NULL) {
        (void)embk_wait_wake(w, EMBK_WAKE_SATISFIED, 1u); /* hand-off: the count is untouched */
        EMBK_TRACE(EMB_TRACE_SEM_GIVE, embk_thread_index(w), s, s->count);
    } else {
        if (s->count == s->max) {
            embk_unlock_object(s, key);
            return ((s->obj.flags & EMB_SEM_SATURATE) != 0u) ? EMB_OK : EMB_EOVERFLOW;
        }
        s->count++;
        EMBK_TRACE(EMB_TRACE_SEM_GIVE, 0xFFu, s, s->count);
#if CONFIG_EMB_NOTIFY
        if (s->count == 1u && s->bind_thread != NULL) {
            embk_notify_set_locked(s->bind_thread,
                                   s->bind_bit); /* ADR-027: the 0 to 1 transition */
        }
#endif
    }
    embk_sched_reschedule_if_needed();
    embk_unlock_object(s, key);
    return EMB_OK;
}

emb_sem_count_t emb_sem_count(emb_sem_t sem)
{
    const embk_sem_t *s = sem_from_handle(sem);
    return (s == NULL) ? 0u : s->count;
}

#if CONFIG_EMB_NOTIFY
emb_status_t emb_sem_bind_notify(emb_sem_t sem, emb_thread_t thread, emb_notify_bits_t bit)
{
    embk_sem_t *s = sem_from_handle(sem);
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    EMBK_REQUIRE_NOT_ISR(); /* bindings are set up before the kernel runs too (A18) */
    EMBK_REQUIRE(s != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 2u);
    EMBK_REQUIRE(bit != 0u && (bit & (emb_notify_bits_t)(bit - 1u)) == 0u &&
                     (bit & (emb_notify_bits_t)~EMB_NOTIFY_APP_MASK) == 0u,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    key = embk_lock_object(s);
    if (s->bind_thread != NULL) {
        embk_unlock_object(s, key);
        return EMB_EEXIST;
    }
    s->bind_thread = t;
    s->bind_bit = bit;
    embk_unlock_object(s, key);
    return EMB_OK;
}
#endif

#endif /* CONFIG_EMB_SEM */
