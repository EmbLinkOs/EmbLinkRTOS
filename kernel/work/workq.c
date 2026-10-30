/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Work queues (SPEC-006 §4): a kernel thread and an intrusive FIFO. A submit appends the
 * item and sets EMB_NOTIFY_K_WORK on the queue's thread, which drains the FIFO one item
 * at a time, marking it RUNNING while its handler runs outside the critical section. A
 * submit during the handler marks the item for one more run, so an item never runs
 * concurrently with itself and no submission is lost. cancel_sync and flush wait on the
 * completion queue with the awaited item (a stack marker for flush) in wake_data.
 */
#include <emb/storage.h>
#include <emb/thread.h>
#include <emb/work.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/thread.h>
#include <embk/wait.h>
#include <embk/work.h>

#if CONFIG_EMB_WORKQ

EMB_STATIC_ASSERT(sizeof(embk_workq_t) <= EMB_WORKQ_STORAGE_SIZE, "generated storage too small");
EMB_STATIC_ASSERT(sizeof(embk_work_t) <= sizeof(emb_work_t), "generated storage too small");

#if CONFIG_EMB_OBJ_LIST
embk_list_t embk_workq_all = EMBK_LIST_INITIALIZER(embk_workq_all); /* usable before kernel init */
#endif

embk_workq_t *embk_workq_from_raw(uintptr_t raw)
{
    return (embk_workq_t *)embk_obj_from_raw(raw, EMBK_OBJ_WORKQ, offsetof(embk_workq_t, obj));
}

static EMB_INLINE embk_workq_t *workq_from_handle(emb_workq_t h)
{
    return embk_workq_from_raw(h.raw);
}

static EMB_INLINE embk_work_t *work_of(const emb_work_t *work)
{
    embk_work_t *w = (embk_work_t *)(void *)(uintptr_t)work; /* the storage holds it (CS-4.5) */
    return (w != NULL && w->tag == EMBK_WORK_TAG) ? w : NULL;
}

/* The index the trace records for the caller: none before the kernel runs. */
static EMB_INLINE uintptr_t caller_index(void)
{
    return (embk_cpu.current != NULL) ? embk_thread_index(embk_cpu.current) : 0xFFu;
}

/* Wake every completion waiter for @w, in queue order; lock held. */
static void wake_done_locked(embk_workq_t *q, const embk_work_t *w)
{
    embk_wait_iter_t it;
    embk_thread_t *t;
    embk_wait_iter_begin(&q->done_q, &it);
    while ((t = embk_wait_iter_next(&q->done_q, &it)) != NULL) {
        if (t->wake_data == (uintptr_t)w) {
            (void)embk_wait_wake(t, EMBK_WAKE_SATISFIED, 0u);
        }
    }
}

/* Unlink a pending item: it will not run. Lock held. */
static void unlink_locked(embk_work_t *w)
{
    embk_list_remove(&w->node);
    w->state = EMB_WORK_IDLE;
    w->queue = NULL;
}

/* The queue's thread (KRN-WQ-009): drain, then wait for K_WORK; exit once stopping. */
static void workq_main(void *arg)
{
    embk_workq_t *q = (embk_workq_t *)arg;
    for (;;) {
        emb_irq_key_t key = embk_lock_object(q);
        embk_list_node_t *n = embk_list_pop_first(&q->fifo);
        embk_work_t *w;
        if (n == NULL) {
            bool stop = q->stopping != 0u;
            embk_unlock_object(q, key);
            if (stop) {
                embk_thread_exit_kernel(); /* wakes the destroying thread */
            }
            (void)emb_notify_wait(EMB_NOTIFY_K_WORK, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR,
                                  EMB_WAIT_FOREVER, NULL);
            continue;
        }
        w = EMB_CONTAINER_OF(n, embk_work_t, node);
        if ((w->flags & EMBK_WORK_MARKER) != 0u) {
            w->state = EMB_WORK_IDLE; /* everything queued before the flush has run */
            wake_done_locked(q, w);
            embk_sched_reschedule_if_needed();
            embk_unlock_object(q, key);
            continue;
        }
        w->state = EMB_WORK_RUNNING;
        q->running = w;
        EMBK_TRACE(EMB_TRACE_WORK_RUN, embk_thread_index(&q->thread), w, 0u);
        embk_unlock_object(q, key);
        w->fn((emb_work_t *)(void *)w);
        key = embk_lock_object(q);
        EMBK_TRACE(EMB_TRACE_WORK_RUN, embk_thread_index(&q->thread), w, 1u);
#if CONFIG_EMB_MUTEX
        if (q->thread.owned != NULL) {
            /* the next handler would inherit the lock (SPEC-006 §6) */
            embk_fault_raise_api(EMB_API_ID_WORK_SUBMIT, EMB_FAULT_API_LIFECYCLE, 0u, 0u,
                                 EMBK_WHERE);
        }
#endif
        q->running = NULL;
        if ((w->flags & EMBK_WORK_RESUBMITTED) != 0u) {
            w->flags &= (uint8_t)~EMBK_WORK_RESUBMITTED;
            w->state = EMB_WORK_PENDING;
            embk_list_append(&q->fifo, &w->node);
        } else {
            w->state = EMB_WORK_IDLE;
            w->queue = NULL;
        }
        wake_done_locked(q, w);
        embk_sched_reschedule_if_needed();
        embk_unlock_object(q, key);
    }
}

void emb_workq_attr_default(emb_workq_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->stack = NULL;
    out_attr->stack_size = 0u;
    out_attr->priority = EMB_PRIORITY_MIN;
    out_attr->reserved_[0] = 0u;
    out_attr->reserved_[1] = 0u;
    out_attr->reserved_[2] = 0u;
}

emb_status_t emb_workq_init(emb_workq_storage_t *storage, const emb_workq_attr_t *attr,
                            emb_workq_t *out_workq)
{
    embk_workq_t *q;
    emb_thread_attr_t ta;
    emb_irq_key_t key;
    emb_status_t st;
    if (out_workq != NULL) {
        out_workq->raw = 0u;
    }
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_WORKQ_INIT);
    EMBK_API_REQUIRE(EMB_API_ID_WORKQ_INIT, embk_cpu.kernel_state != EMBK_KERNEL_UNINIT,
                     EMB_FAULT_API_LIFECYCLE, EMB_ESTATE, 0u);
    EMBK_API_REQUIRE(EMB_API_ID_WORKQ_INIT, storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_WORKQ_INIT, attr != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE(EMB_API_ID_WORKQ_INIT, out_workq != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     3u);
    EMBK_API_REQUIRE(EMB_API_ID_WORKQ_INIT,
                     ((uintptr_t)storage % (uintptr_t)EMB_WORKQ_STORAGE_ALIGN) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    q = (embk_workq_t *)(void *)storage;
#if CONFIG_EMB_OBJ_LIST
    /* storage is reused only through destroy (SPEC-009 §6, KRN-OBJ-007) */
    key = embk_lock_sched();
    if (embk_list_contains(&embk_workq_all, &q->all_node)) {
        embk_unlock_sched(key);
        EMBK_API_MISUSE(EMB_API_ID_WORKQ_INIT, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
    embk_unlock_sched(key);
#endif
    embk_obj_init(&q->obj, EMBK_OBJ_WORKQ, 0u, attr->name);
    embk_list_init(&q->fifo);
    embk_wait_queue_init(&q->done_q, 0u);
    q->running = NULL;
    q->stopping = 0u;
    q->timer_refs = 0u;
    emb_thread_attr_default(&ta);
    ta.name = attr->name;
    ta.stack = attr->stack;
    ta.stack_size = attr->stack_size;
    ta.priority = attr->priority;
    st = embk_thread_init_kernel(EMB_API_ID_WORKQ_INIT, &q->thread, &ta, workq_main, q);
    if (st != EMB_OK) {
        q->obj.state = EMBK_OBJ_UNINIT;
        return st;
    }
    key = embk_lock_sched();
#if CONFIG_EMB_OBJ_LIST
    embk_list_append(&embk_workq_all, &q->all_node);
#endif
    out_workq->raw = embk_obj_to_raw(q);
    embk_unlock_sched(key);
    return EMB_OK;
}

emb_status_t emb_workq_destroy(emb_workq_t workq)
{
    embk_workq_t *q = workq_from_handle(workq);
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    bool deferred;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_WORKQ_DESTROY);
    self = embk_cpu.current;
    if (q == NULL) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(EMB_API_ID_WORKQ_DESTROY, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    key = embk_lock_object(q);
    if (self == &q->thread || !embk_list_is_empty(&q->fifo) || q->running != NULL ||
        q->stopping != 0u || q->timer_refs != 0u || !embk_wait_queue_is_empty(&q->done_q)) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), q, EMB_EBUSY);
        embk_unlock_object(q, key);
        EMBK_API_MISUSE(EMB_API_ID_WORKQ_DESTROY, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
    q->stopping = 1u;
    embk_notify_set_locked(&q->thread, EMB_NOTIFY_K_WORK);
    /* the join is not a cancellation point: the queue must not be left half destroyed */
    deferred = self->cancel_disable < UINT8_MAX;
    if (deferred) {
        self->cancel_disable++; /* own field, thread context */
    }
    r = embk_block_on(&q->thread.join_q, EMB_WAIT_REASON_JOIN, EMBK_DEADLINE_FOREVER, key, &data);
    (void)data;
    if (deferred) {
        self->cancel_disable--;
    }
    EMBK_ASSERT(r == EMBK_WAKE_SATISFIED);
    (void)r;
    embk_thread_destroy_kernel(&q->thread);
    key = embk_lock_sched();
#if CONFIG_EMB_OBJ_LIST
    embk_list_remove(&q->all_node);
#endif
    q->obj.state = EMBK_OBJ_UNINIT;
    embk_unlock_sched(key);
    EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), q, EMB_OK);
    return EMB_OK;
}

emb_status_t emb_workq_flush(emb_workq_t workq, emb_timeout_t timeout)
{
    embk_workq_t *q = workq_from_handle(workq);
    embk_thread_t *self;
    embk_work_t marker;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    emb_status_t st;
    bool must_wait;
    emb_tick_t deadline = embk_deadline_from_timeout(timeout); /* fixed at the call */
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_WORKQ_FLUSH);
    self = embk_cpu.current;
    if (q == NULL) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(EMB_API_ID_WORKQ_FLUSH, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    EMBK_API_REQUIRE_CAN_BLOCK(EMB_API_ID_WORKQ_FLUSH, EMB_TIMEOUT_IS_NO_WAIT(timeout) ? 0u : 1u);
    key = embk_lock_object(q);
    if (self == &q->thread) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), q, EMB_EPERM);
        embk_unlock_object(q, key);
        EMBK_API_MISUSE(EMB_API_ID_WORKQ_FLUSH, EMB_FAULT_API_CONTEXT, EMB_EPERM, 1u);
    }
    must_wait = false;
    if (q->stopping != 0u) {
        st = EMB_ESTATE;
    } else if (embk_thread_take_cancel_locked(self)) {
        st = EMB_ECANCELED;
    } else if (embk_list_is_empty(&q->fifo) && q->running == NULL) {
        st = EMB_OK; /* idle: nothing queued before the call */
    } else if (EMB_TIMEOUT_IS_NO_WAIT(timeout)) {
        st = EMB_ETIMEDOUT;
    } else {
        st = EMB_OK;
        must_wait = true;
    }
    if (!must_wait) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), q, st);
        embk_unlock_object(q, key);
        return st;
    }
    embk_list_node_init(&marker.node);
    marker.fn = NULL;
    marker.queue = q;
    marker.tag = EMBK_WORK_TAG;
    marker.state = EMB_WORK_PENDING;
    marker.flags = EMBK_WORK_MARKER;
    embk_list_append(&q->fifo, &marker.node);
    embk_notify_set_locked(&q->thread, EMB_NOTIFY_K_WORK);
    self->wake_data = (uintptr_t)&marker; /* the awaited item until the queue reaches it */
    r = embk_block_on(&q->done_q, EMB_WAIT_REASON_OBJECT, deadline, key, &data);
    (void)data;
    if (r != EMBK_WAKE_SATISFIED) {
        key = embk_lock_object(q);
        if (marker.state == EMB_WORK_PENDING) {
            embk_list_remove(&marker.node); /* it lives on this stack */
        }
        embk_unlock_object(q, key);
    }
    st = embk_wait_result_status(r);
    EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), q, st);
    return st;
}

void embk_work_init(embk_work_t *w, void (*fn)(struct emb_work *work))
{
    embk_list_node_init(&w->node);
    w->fn = fn;
    w->queue = NULL;
    w->tag = EMBK_WORK_TAG;
    w->state = EMB_WORK_IDLE;
    w->flags = 0u;
}

void emb_work_init(emb_work_t *work, emb_work_fn_t fn)
{
    if (work != NULL) {
        embk_work_init((embk_work_t *)(void *)work, fn);
    }
}

emb_status_t embk_work_submit_locked(embk_workq_t *q, embk_work_t *w)
{
    emb_status_t st = EMB_OK;
    if (q->stopping != 0u) {
        st = EMB_ESTATE; /* being destroyed */
    } else if (w->state == EMB_WORK_PENDING) {
        st = EMB_EEXIST; /* never queued twice (KRN-WQ-005) */
    } else if (w->state == EMB_WORK_RUNNING) {
        if (w->queue == q) {
            w->flags |= EMBK_WORK_RESUBMITTED; /* queued again when the handler returns */
        } else {
            st = EMB_EBUSY; /* an item belongs to one queue at a time */
        }
    } else {
        w->state = EMB_WORK_PENDING;
        w->queue = q;
        embk_list_append(&q->fifo, &w->node);
        embk_notify_set_locked(&q->thread, EMB_NOTIFY_K_WORK);
    }
    EMBK_TRACE(EMB_TRACE_WORK_SUBMIT, embk_thread_index(&q->thread), w, st);
    return st;
}

emb_status_t emb_work_submit(emb_workq_t workq, emb_work_t *work)
{
    embk_workq_t *q = workq_from_handle(workq);
    embk_work_t *w = work_of(work);
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_WORK_SUBMIT, q != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_WORK_SUBMIT, w != NULL && w->fn != NULL, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    key = embk_lock_object(q);
    st = embk_work_submit_locked(q, w);
    embk_sched_reschedule_if_needed();
    embk_unlock_object(q, key);
    return st;
}

uint8_t embk_work_cancel_locked(embk_work_t *w)
{
    uint8_t was = w->state;
    if (was == EMB_WORK_PENDING) {
        unlink_locked(w);
    } else if (was == EMB_WORK_RUNNING) {
        w->flags &= (uint8_t)~EMBK_WORK_RESUBMITTED; /* the handler completes */
    } else {
        /* idle: nothing to cancel */
    }
    EMBK_TRACE(EMB_TRACE_WORK_CANCEL, caller_index(), w, was);
    return was;
}

emb_status_t emb_work_cancel(emb_work_t *work, emb_work_state_t *out_state)
{
    embk_work_t *w = work_of(work);
    emb_irq_key_t key;
    emb_work_state_t was;
    EMBK_API_REQUIRE(EMB_API_ID_WORK_CANCEL, w != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    key = embk_lock_sched();
    was = embk_work_cancel_locked(w);
    embk_unlock_sched(key);
    if (out_state != NULL) {
        *out_state = was;
    }
    return EMB_OK;
}

emb_status_t embk_work_cancel_sync(uint16_t api_id, embk_work_t *w, emb_tick_t deadline,
                                   bool nowait)
{
    embk_thread_t *self = embk_cpu.current;
    embk_workq_t *q;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    emb_status_t st;
    bool must_wait;
    EMBK_API_REQUIRE_CAN_BLOCK(api_id, nowait ? 0u : 1u);
    key = embk_lock_sched();
    q = w->queue;
    if (q != NULL && self == &q->thread) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), w, EMB_EPERM);
        embk_unlock_sched(key);
        EMBK_API_MISUSE(api_id, EMB_FAULT_API_CONTEXT, EMB_EPERM, 1u);
    }
    must_wait = false;
    if (embk_thread_take_cancel_locked(self)) {
        st = EMB_ECANCELED;
    } else if (w->state != EMB_WORK_RUNNING) {
        if (w->state == EMB_WORK_PENDING) {
            unlink_locked(w);
        }
        st = EMB_OK;
    } else {
        w->flags &= (uint8_t)~EMBK_WORK_RESUBMITTED;
        st = nowait ? EMB_ETIMEDOUT : EMB_OK;
        must_wait = st == EMB_OK;
    }
    if (!must_wait) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), w, st);
        embk_unlock_sched(key);
        return st;
    }
    self->wake_data = (uintptr_t)w; /* the awaited item until its handler returns */
    r = embk_block_on(&q->done_q, EMB_WAIT_REASON_OBJECT, deadline, key, &data);
    (void)data;
    st = embk_wait_result_status(r);
    EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(self), w, st);
    return st;
}

emb_status_t emb_work_cancel_sync(emb_work_t *work, emb_timeout_t timeout)
{
    embk_work_t *w = work_of(work);
    emb_tick_t deadline = embk_deadline_from_timeout(timeout); /* fixed at the call */
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_WORK_CANCEL_SYNC);
    if (w == NULL) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(embk_cpu.current), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(EMB_API_ID_WORK_CANCEL_SYNC, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    }
    return embk_work_cancel_sync(EMB_API_ID_WORK_CANCEL_SYNC, w, deadline,
                                 EMB_TIMEOUT_IS_NO_WAIT(timeout));
}

emb_work_state_t emb_work_state(const emb_work_t *work)
{
    const embk_work_t *w = work_of(work);
    return (w == NULL) ? EMB_WORK_IDLE : w->state;
}

#endif /* CONFIG_EMB_WORKQ */
