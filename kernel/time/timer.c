/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Software timers (SPEC-003 §7). An armed timer sits in its own deadline list (ADR-047),
 * which the expiry pass merges with the thread timeouts. A periodic timer re-arms from
 * its previous deadline before its callback runs, so expiry k falls at first + k * period;
 * periods already past at expiry are skipped in closed form and counted as overruns. The
 * callback runs on the timer's work queue, where an expiry whose previous callback is still
 * queued coalesces into it (an overrun), or with EMB_TIMER_ISR_CONTEXT in the expiry pass.
 */
#include <emb/storage.h>
#include <emb/timer.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/time.h>
#include <embk/timer.h>
#include <embk/wait.h>
#if CONFIG_EMB_WORKQ
#include <embk/work.h>
#endif

#if CONFIG_EMB_TIMER

EMB_STATIC_ASSERT(sizeof(embk_timer_t) <= EMB_TIMER_STORAGE_SIZE, "generated storage too small");

#if CONFIG_EMB_OBJ_LIST
embk_list_t embk_timer_all = EMBK_LIST_INITIALIZER(embk_timer_all); /* usable before kernel init */
#endif

static EMB_INLINE embk_timer_t *timer_from_handle(emb_timer_t h)
{
    return (embk_timer_t *)embk_obj_from_raw(h.raw, EMBK_OBJ_TIMER, offsetof(embk_timer_t, obj));
}

static EMB_INLINE emb_timer_t handle_of(const embk_timer_t *t)
{
    emb_timer_t h;
    h.raw = embk_obj_to_raw(t);
    return h;
}

static EMB_INLINE uint32_t add_sat_u32(uint32_t a, uint32_t b)
{
    return (a > UINT32_MAX - b) ? UINT32_MAX : a + b;
}

/* The finite tick count a duration arms with (SPEC-003 §5.2). */
static EMB_INLINE emb_tick_t interval_of(emb_duration_t d)
{
    return emb_timeout_from_duration(d).ticks;
}

#if CONFIG_EMB_WORKQ
static void timer_work(emb_work_t *work)
{
    embk_timer_t *t = EMB_CONTAINER_OF((embk_work_t *)(void *)work, embk_timer_t, work);
    t->fn(handle_of(t), t->arg);
}
#endif

void embk_timer_expired(embk_timer_t *t, emb_tick_t now, emb_irq_key_t *key)
{
    uint32_t missed = 0u;
    t->expiries = add_sat_u32(t->expiries, 1u);
    if (t->period != 0u) {
        emb_tick_t next = t->node.deadline + t->period; /* from the previous deadline */
        if (!embk_tick_before(now, next)) {
            /* whole periods already past, in closed form (§16): next lands after now */
            emb_tick_t skip = ((emb_tick_t)(now - next) / t->period) + 1u;
#if CONFIG_EMB_TICK_32BIT
            missed = (uint32_t)skip;
#else
            missed = (skip > UINT32_MAX) ? UINT32_MAX : (uint32_t)skip;
#endif
            next += skip * t->period;
        }
        t->overruns = add_sat_u32(t->overruns, missed);
        embk_timer_node_arm(&t->node, next); /* before the callback: it may stop the timer */
    }
    EMBK_TRACE(EMB_TRACE_TIMER_EXPIRE, missed, t, t->expiries);
#if CONFIG_EMB_WORKQ
    if (t->queue != NULL) {
        /* a callback already queued, or due again after the running one, absorbs this
         * expiry: an overrun (§7.4) */
        if (t->work.state == EMB_WORK_PENDING ||
            (t->work.state == EMB_WORK_RUNNING && (t->work.flags & EMBK_WORK_RESUBMITTED) != 0u)) {
            t->overruns = add_sat_u32(t->overruns, 1u);
        } else {
            (void)embk_work_submit_locked(t->queue, &t->work);
        }
        return;
    }
#endif
    /* EMB_TIMER_ISR_CONTEXT: interrupt context, the timeout structure released (§7.3) */
    t->in_callback = 1u;
    embk_unlock_timeout(*key);
    t->fn(handle_of(t), t->arg);
    *key = embk_lock_timeout();
    t->in_callback = 0u;
}

void embk_timer_init_isr(embk_timer_t *t, void (*fn)(struct emb_timer_t timer, void *arg),
                         void *arg)
{
    embk_obj_init(&t->obj, EMBK_OBJ_TIMER, EMB_TIMER_ISR_CONTEXT, NULL);
    embk_list_node_init(&t->node.node);
    t->period = 0u;
    t->interval = 0u;
    t->expiries = 0u;
    t->overruns = 0u;
    t->fn = fn;
    t->arg = arg;
    t->pass = 0u;
    t->in_callback = 0u;
#if CONFIG_EMB_WORKQ
    t->queue = NULL;
    embk_work_init(&t->work, timer_work);
#endif
}

void emb_timer_attr_default(emb_timer_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->fn = NULL;
    out_attr->arg = NULL;
#if CONFIG_EMB_WORKQ
    out_attr->workq.raw = 0u;
#endif
    out_attr->flags = 0u;
    out_attr->reserved_[0] = 0u;
    out_attr->reserved_[1] = 0u;
    out_attr->reserved_[2] = 0u;
}

emb_status_t emb_timer_init(emb_timer_storage_t *storage, const emb_timer_attr_t *attr,
                            emb_timer_t *out_timer)
{
    embk_timer_t *t;
    emb_irq_key_t key;
    bool isr;
#if CONFIG_EMB_WORKQ
    embk_workq_t *q = NULL;
#endif
    if (out_timer != NULL) {
        out_timer->raw = 0u;
    }
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_TIMER_INIT);
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_INIT, storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_INIT,
                     attr != NULL && attr->fn != NULL &&
                         (attr->flags & (uint8_t)~EMB_TIMER_ISR_CONTEXT) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_INIT, out_timer != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     3u);
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_INIT,
                     ((uintptr_t)storage % (uintptr_t)EMB_TIMER_STORAGE_ALIGN) == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    isr = (attr->flags & EMB_TIMER_ISR_CONTEXT) != 0u;
#if CONFIG_EMB_WORKQ
    if (!isr) {
        if (attr->workq.raw == 0u) {
            return EMB_ENOTSUP; /* no implicit queue: name one, or ISR context (A55) */
        }
        q = embk_workq_from_raw(attr->workq.raw);
        EMBK_API_REQUIRE(EMB_API_ID_TIMER_INIT, q != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 2u);
    }
#else
    if (!isr) {
        return EMB_ENOTSUP; /* thread-context callbacks need CONFIG_EMB_WORKQ */
    }
#endif
    t = (embk_timer_t *)(void *)storage;
    key = embk_lock_sched();
#if CONFIG_EMB_OBJ_LIST
    /* storage is reused only through destroy (SPEC-009 §6, KRN-OBJ-007) */
    if (embk_list_contains(&embk_timer_all, &t->all_node)) {
        embk_unlock_sched(key);
        EMBK_API_MISUSE(EMB_API_ID_TIMER_INIT, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
#endif
#if CONFIG_EMB_WORKQ
    if (q != NULL) {
        if (q->timer_refs == UINT8_MAX) {
            embk_unlock_sched(key);
            return EMB_ENOMEM;
        }
        q->timer_refs++; /* the queue outlives the timer (E2) */
    }
    t->queue = q;
    embk_work_init(&t->work, timer_work);
#endif
    embk_obj_init(&t->obj, EMBK_OBJ_TIMER, attr->flags & EMB_TIMER_ISR_CONTEXT, attr->name);
    embk_list_node_init(&t->node.node);
    t->period = 0u;
    t->interval = 0u;
    t->expiries = 0u;
    t->overruns = 0u;
    t->fn = attr->fn;
    t->arg = attr->arg;
    t->pass = 0u;
    t->in_callback = 0u;
#if CONFIG_EMB_OBJ_LIST
    embk_list_append(&embk_timer_all, &t->all_node);
#endif
    embk_unlock_sched(key);
    out_timer->raw = embk_obj_to_raw(t);
    return EMB_OK;
}

/* The state stop finds and leaves: disarmed, a queued callback dropped; lock held. */
uint8_t embk_timer_stop_locked(embk_timer_t *t)
{
    emb_timer_state_t st = EMB_TIMER_IDLE;
    if (embk_list_node_is_linked(&t->node.node)) {
        embk_timer_node_disarm(&t->node);
        st = EMB_TIMER_PENDING;
    }
#if CONFIG_EMB_WORKQ
    if (t->queue != NULL) {
        uint8_t was = embk_work_cancel_locked(&t->work);
        if (was == EMB_WORK_PENDING) {
            st = EMB_TIMER_PENDING;
        } else if (was == EMB_WORK_RUNNING) {
            st = EMB_TIMER_RUNNING;
        } else {
            /* the callback is neither queued nor running */
        }
    }
#endif
    if (t->in_callback != 0u) {
        st = EMB_TIMER_RUNNING; /* from its own ISR_CONTEXT callback */
    }
    return st;
}

emb_status_t emb_timer_destroy(emb_timer_t timer)
{
    embk_timer_t *t = timer_from_handle(timer);
    emb_irq_key_t key;
    bool busy;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_TIMER_DESTROY);
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_DESTROY, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_sched();
    busy = embk_list_node_is_linked(&t->node.node) || t->in_callback != 0u;
#if CONFIG_EMB_WORKQ
    busy = busy || (t->queue != NULL && t->work.state != EMB_WORK_IDLE);
#endif
    if (busy) {
        embk_unlock_sched(key);
        EMBK_API_MISUSE(EMB_API_ID_TIMER_DESTROY, EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
#if CONFIG_EMB_WORKQ
    if (t->queue != NULL) {
        t->queue->timer_refs--;
    }
#endif
#if CONFIG_EMB_OBJ_LIST
    embk_list_remove(&t->all_node);
#endif
    t->obj.state = EMBK_OBJ_UNINIT;
    embk_unlock_sched(key);
    return EMB_OK;
}

/* (Re)arm at @deadline; lock held. The counts are kept. */
void embk_timer_arm_locked(embk_timer_t *t, emb_tick_t deadline, emb_tick_t period,
                           emb_tick_t interval)
{
    embk_timer_node_disarm(&t->node);
    t->period = period;
    t->interval = interval;
    t->obj.flags |= EMBK_TIMER_STARTED;
    embk_timer_node_arm(&t->node, deadline);
    EMBK_TRACE(EMB_TRACE_TIMER_START, (uint32_t)deadline, t, (uint32_t)period);
}

static emb_status_t start_common(uint16_t api_id, emb_timer_t timer, emb_tick_t first,
                                 emb_tick_t period)
{
    embk_timer_t *t = timer_from_handle(timer);
    emb_irq_key_t key;
    emb_timeout_t after;
    EMBK_API_REQUIRE(api_id, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    after.ticks = first;
    key = embk_lock_timeout();
    embk_timer_arm_locked(t, embk_deadline_from_timeout(after), period,
                          (period != 0u) ? period : first);
    embk_unlock_timeout(key);
    return EMB_OK;
}

emb_status_t emb_timer_start(emb_timer_t timer, emb_duration_t first)
{
    return start_common(EMB_API_ID_TIMER_START, timer, interval_of(first), 0u);
}

emb_status_t emb_timer_start_at(emb_timer_t timer, emb_instant_t at)
{
    embk_timer_t *t = timer_from_handle(timer);
    emb_irq_key_t key;
    emb_tick_t now;
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_START_AT, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_timeout();
    now = embk_time_ticks_locked();
    embk_timer_arm_locked(t, at.ticks, 0u,
                          embk_tick_before(now, at.ticks) ? (emb_tick_t)(at.ticks - now) : 0u);
    embk_unlock_timeout(key);
    return EMB_OK;
}

emb_status_t emb_timer_start_periodic(emb_timer_t timer, emb_duration_t first,
                                      emb_duration_t period)
{
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_START_PERIODIC, period.ticks != 0u, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 3u);
    return start_common(EMB_API_ID_TIMER_START_PERIODIC, timer, interval_of(first),
                        interval_of(period));
}

emb_status_t emb_timer_restart(emb_timer_t timer)
{
    embk_timer_t *t = timer_from_handle(timer);
    emb_irq_key_t key;
    emb_timeout_t after;
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_RESTART, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_timeout();
    if ((t->obj.flags & EMBK_TIMER_STARTED) == 0u) {
        embk_unlock_timeout(key);
        return EMB_ESTATE;
    }
    after.ticks = t->interval;
    embk_timer_arm_locked(t, embk_deadline_from_timeout(after), t->period, t->interval);
    embk_unlock_timeout(key);
    return EMB_OK;
}

static EMB_INLINE uintptr_t caller_index(void)
{
    return (embk_cpu.current != NULL) ? embk_thread_index(embk_cpu.current) : 0xFFu;
}

emb_status_t emb_timer_stop(emb_timer_t timer, emb_timer_state_t *out_state)
{
    embk_timer_t *t = timer_from_handle(timer);
    emb_irq_key_t key;
    emb_timer_state_t st;
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_STOP, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_timeout();
    st = embk_timer_stop_locked(t);
    EMBK_TRACE(EMB_TRACE_TIMER_STOP, caller_index(), t, st);
    embk_unlock_timeout(key);
    if (out_state != NULL) {
        *out_state = st;
    }
    return EMB_OK;
}

emb_status_t emb_timer_stop_sync(emb_timer_t timer, emb_timeout_t timeout)
{
    embk_timer_t *t = timer_from_handle(timer);
    emb_tick_t deadline = embk_deadline_from_timeout(timeout); /* fixed at the call */
    emb_irq_key_t key;
    emb_timer_state_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_TIMER_STOP_SYNC);
    if (t == NULL) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(embk_cpu.current), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(EMB_API_ID_TIMER_STOP_SYNC, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    }
    key = embk_lock_timeout();
#if CONFIG_EMB_WORKQ
    if (t->queue != NULL && embk_cpu.current == &t->queue->thread &&
        t->work.state == EMB_WORK_RUNNING) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(embk_cpu.current), &t->work, EMB_EPERM);
        embk_unlock_timeout(key);
        EMBK_API_MISUSE(EMB_API_ID_TIMER_STOP_SYNC, EMB_FAULT_API_CONTEXT, EMB_EPERM, 1u);
    }
#endif
    st = embk_timer_stop_locked(t);
    EMBK_TRACE(EMB_TRACE_TIMER_STOP, embk_thread_index(embk_cpu.current), t, st);
    embk_unlock_timeout(key);
#if CONFIG_EMB_WORKQ
    if (t->queue != NULL) {
        /* waits only while the callback still runs; traces every outcome (A19) */
        return embk_work_cancel_sync(EMB_API_ID_TIMER_STOP_SYNC, &t->work, deadline,
                                     EMB_TIMEOUT_IS_NO_WAIT(timeout));
    }
#endif
    (void)deadline;
    (void)st;
    EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(embk_cpu.current), t, EMB_OK);
    return EMB_OK; /* an ISR_CONTEXT callback cannot be running under a thread */
}

bool emb_timer_is_running(emb_timer_t timer)
{
    const embk_timer_t *t = timer_from_handle(timer);
    return t != NULL && embk_list_node_is_linked(&t->node.node);
}

emb_status_t emb_timer_remaining(emb_timer_t timer, emb_duration_t *out_rem)
{
    const embk_timer_t *t = timer_from_handle(timer);
    emb_irq_key_t key;
    emb_tick_t now;
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_REMAINING, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_REMAINING, out_rem != NULL, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    key = embk_lock_timeout();
    now = embk_time_ticks_locked();
    out_rem->ticks =
        (embk_list_node_is_linked(&t->node.node) && embk_tick_before(now, t->node.deadline))
            ? (emb_tick_t)(t->node.deadline - now)
            : 0u;
    embk_unlock_timeout(key);
    return EMB_OK;
}

emb_status_t emb_timer_get_counts(emb_timer_t timer, uint32_t *out_expiries, uint32_t *out_overruns)
{
    const embk_timer_t *t = timer_from_handle(timer);
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_TIMER_GET_COUNTS, t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_timeout();
    if (out_expiries != NULL) {
        *out_expiries = t->expiries;
    }
    if (out_overruns != NULL) {
        *out_overruns = t->overruns;
    }
    embk_unlock_timeout(key);
    return EMB_OK;
}

#endif /* CONFIG_EMB_TIMER */
