/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Delayed work (SPEC-006 §4.3, KRN-WQ-003, KRN-WQ-007): a public work item and an
 * internal ISR_CONTEXT timer whose only action is the O(1) submit, so no second queue hop
 * is paid. While the delay runs the item holds a timer reference on its target queue,
 * which therefore cannot be destroyed (A55). The target is the armed marker: cancel clears
 * it, so an expiry already past its timer but not yet submitting does nothing.
 */
#include <emb/storage.h>
#include <emb/timer.h>
#include <emb/work.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/time.h>
#include <embk/timer.h>
#include <embk/wait.h>
#include <embk/work.h>

#if CONFIG_EMB_WORKQ && CONFIG_EMB_TIMER

EMB_STATIC_ASSERT(sizeof(embk_dwork_t) <= EMB_DWORK_STORAGE_SIZE, "generated storage too small");

static EMB_INLINE embk_work_t *item_of(emb_dwork_t *dw)
{
    embk_work_t *w = (embk_work_t *)(void *)&dw->work;
    return (w->tag == EMBK_WORK_TAG) ? w : NULL;
}

static EMB_INLINE embk_dwork_t *dwork_of(emb_dwork_t *dw)
{
    return (embk_dwork_t *)(void *)&dw->timer_;
}

static EMB_INLINE uintptr_t caller_index(void)
{
    return (embk_cpu.current != NULL) ? embk_thread_index(embk_cpu.current) : 0xFFu;
}

/* The internal timer's callback: the expiry path, interrupt context, timeout lock released. */
static void fire(emb_timer_t timer, void *arg)
{
    emb_dwork_t *dw = (emb_dwork_t *)arg;
    embk_dwork_t *d = dwork_of(dw);
    emb_irq_key_t key = embk_lock_sched();
    (void)timer;
    if (d->target != NULL) {
        (void)embk_work_submit_locked(d->target, item_of(dw)); /* O(1) (KRN-WQ-007) */
        d->target->timer_refs--;
        d->target = NULL;
    }
    embk_unlock_sched(key); /* the interrupt's exit reschedules */
}

/* Stop the delay and release its queue reference; lock held. */
static bool disarm_locked(embk_dwork_t *d)
{
    if (d->target == NULL) {
        return false;
    }
    (void)embk_timer_stop_locked(&d->timer);
    d->target->timer_refs--;
    d->target = NULL;
    return true;
}

void emb_dwork_init(emb_dwork_t *dwork, emb_work_fn_t fn)
{
    embk_dwork_t *d;
    if (dwork == NULL) {
        return;
    }
    d = dwork_of(dwork);
    embk_work_init((embk_work_t *)(void *)&dwork->work, fn);
    embk_timer_init_isr(&d->timer, fire, dwork);
    d->target = NULL;
}

static emb_status_t schedule_common(uint16_t api_id, emb_workq_t workq, emb_dwork_t *dwork,
                                    emb_tick_t deadline, bool replace)
{
    embk_workq_t *q = embk_workq_from_raw(workq.raw);
    embk_work_t *w = (dwork != NULL) ? item_of(dwork) : NULL;
    embk_dwork_t *d;
    emb_irq_key_t key;
    emb_status_t st = EMB_OK;
    EMBK_API_REQUIRE(api_id, q != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(api_id, w != NULL && w->fn != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    d = dwork_of(dwork);
    key = embk_lock_sched();
    if (q->stopping != 0u) {
        st = EMB_ESTATE; /* being destroyed */
    } else if (w->state == EMB_WORK_PENDING || (d->target != NULL && !replace)) {
        st = EMB_EEXIST; /* queued, or the delay runs: nothing changes */
    } else if (w->state == EMB_WORK_RUNNING && w->queue != q) {
        st = EMB_EBUSY; /* an item belongs to one queue at a time */
    } else if (d->target != q && q->timer_refs == UINT8_MAX) {
        st = EMB_ENOMEM;
    } else {
        (void)disarm_locked(d); /* reschedule: the old delay goes */
        d->target = q;
        q->timer_refs++;
        embk_timer_arm_locked(&d->timer, deadline, 0u, 0u);
    }
    EMBK_TRACE(EMB_TRACE_DWORK_SCHEDULE, caller_index(), dwork, st);
    embk_unlock_sched(key);
    return st;
}

static emb_tick_t deadline_after(emb_duration_t delay)
{
    return embk_deadline_from_timeout(emb_timeout_from_duration(delay));
}

emb_status_t emb_dwork_schedule(emb_workq_t workq, emb_dwork_t *dwork, emb_duration_t delay)
{
    return schedule_common(EMB_API_ID_DWORK_SCHEDULE, workq, dwork, deadline_after(delay), false);
}

emb_status_t emb_dwork_schedule_at(emb_workq_t workq, emb_dwork_t *dwork, emb_instant_t at)
{
    return schedule_common(EMB_API_ID_DWORK_SCHEDULE_AT, workq, dwork, at.ticks, false);
}

emb_status_t emb_dwork_reschedule(emb_workq_t workq, emb_dwork_t *dwork, emb_duration_t delay)
{
    return schedule_common(EMB_API_ID_DWORK_RESCHEDULE, workq, dwork, deadline_after(delay), true);
}

emb_status_t emb_dwork_cancel(emb_dwork_t *dwork, emb_work_state_t *out_state)
{
    embk_work_t *w = (dwork != NULL) ? item_of(dwork) : NULL;
    emb_irq_key_t key;
    emb_work_state_t was;
    EMBK_API_REQUIRE(EMB_API_ID_DWORK_CANCEL, w != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    key = embk_lock_sched();
    was = disarm_locked(dwork_of(dwork)) ? EMB_WORK_PENDING : embk_work_cancel_locked(w);
    embk_unlock_sched(key);
    if (out_state != NULL) {
        *out_state = was;
    }
    return EMB_OK;
}

emb_status_t emb_dwork_cancel_sync(emb_dwork_t *dwork, emb_timeout_t timeout)
{
    embk_work_t *w = (dwork != NULL) ? item_of(dwork) : NULL;
    emb_tick_t deadline = embk_deadline_from_timeout(timeout); /* fixed at the call */
    emb_irq_key_t key;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DWORK_CANCEL_SYNC);
    if (w == NULL) {
        EMBK_TRACE(EMB_TRACE_WORK_WAIT, embk_thread_index(embk_cpu.current), 0u, EMB_EINVAL);
        EMBK_API_MISUSE(EMB_API_ID_DWORK_CANCEL_SYNC, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    }
    key = embk_lock_sched();
    (void)disarm_locked(dwork_of(dwork));
    embk_unlock_sched(key);
    return embk_work_cancel_sync(EMB_API_ID_DWORK_CANCEL_SYNC, w, deadline,
                                 EMB_TIMEOUT_IS_NO_WAIT(timeout));
}

#endif /* CONFIG_EMB_WORKQ && CONFIG_EMB_TIMER */
