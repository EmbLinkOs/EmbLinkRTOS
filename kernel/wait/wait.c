/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The wait and wake protocol (SPEC-004): three bounded sections to block, a wake that
 * never spins, a generation that makes stale timeouts and cancels harmless, and one
 * path for every wake source. Uniprocessor implementation: the wait-state transitions
 * run inside the critical section (SPEC-004 §12, no compare-and-set needed), and the
 * three lock domains are the same critical section (§8), released between sections.
 */
#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/sync.h>
#include <embk/time.h>
#include <embk/wait.h>

/* ---- queue ------------------------------------------------------------------- */

void embk_wait_queue_init(embk_wait_queue_t *q, uint8_t flags)
{
#if CONFIG_EMB_SCHED_TABLE
    q->set = 0u;
    flags &= (uint8_t)~EMBK_WAIT_FIFO; /* a bitmap has no arrival order (SPEC-004 §11) */
#else
    embk_list_init(&q->waiters);
#endif
    q->flags = flags;
}

bool embk_wait_queue_is_empty(const embk_wait_queue_t *q)
{
#if CONFIG_EMB_SCHED_TABLE
    return q->set == 0u;
#else
    return embk_list_is_empty(&q->waiters);
#endif
}

static void enqueue(embk_wait_queue_t *q, embk_thread_t *t)
{
#if CONFIG_EMB_SCHED_TABLE
    q->set |= EMBK_WORD_BIT(t->base_prio);
#else
    if ((q->flags & EMBK_WAIT_FIFO) != 0u) {
        embk_list_append(&q->waiters, &t->wait_node);
        return;
    }
    {
        /* PRIORITY_FIFO: walk from the tail; bounded by the number of waiters (O(waiters)) */
        embk_list_node_t *p = embk_list_last(&q->waiters);
        while (p != NULL && EMB_CONTAINER_OF(p, embk_thread_t, wait_node)->eff_prio < t->eff_prio) {
            p = embk_list_prev(&q->waiters, p);
        }
        if (p == NULL) {
            embk_list_prepend(&q->waiters, &t->wait_node);
        } else {
            embk_list_insert_after(p, &t->wait_node);
        }
    }
#endif
}

static void dequeue(embk_wait_queue_t *q, embk_thread_t *t)
{
#if CONFIG_EMB_SCHED_TABLE
    q->set &= (embk_word_t)~EMBK_WORD_BIT(t->base_prio);
#else
    (void)q;
    embk_list_remove(&t->wait_node);
#endif
}

embk_thread_t *embk_wait_first(const embk_wait_queue_t *q)
{
#if CONFIG_EMB_SCHED_TABLE
    embk_word_t set = q->set;
    embk_thread_t *best;
    if (set == 0u) {
        return NULL;
    }
    best = embk_thread_table[embk_word_highest(set)];
    if (embk_sched_raised_count != 0u) {
        /* a blocked waiter may itself be raised; bounded by CONFIG_EMB_PRIORITY_COUNT */
        embk_word_t rest = set & (embk_word_t)~EMBK_WORD_BIT(best->base_prio);
        while (rest != 0u) {
            unsigned i = embk_word_highest(rest);
            embk_thread_t *c = embk_thread_table[i];
            if (c->eff_prio > best->eff_prio) {
                best = c;
            }
            rest &= (embk_word_t)~EMBK_WORD_BIT(i);
        }
    }
    return best;
#else
    embk_list_node_t *n = embk_list_first(&q->waiters);
    return (n == NULL) ? NULL : EMB_CONTAINER_OF(n, embk_thread_t, wait_node);
#endif
}

/* ---- blocking (§5.1) ------------------------------------------------------------ */

void embk_wait_prepare(embk_wait_queue_t *q, uint8_t reason)
{
    embk_thread_t *self = embk_cpu.current;
    EMBK_ASSERT(self->wait_state == EMBK_WAIT_READY);
    EMBK_ASSERT(self->wait_queue == NULL);
    self->wait_reason = reason;
    self->wait_queue = q;
    self->wake_result = EMBK_WAKE_NONE;
    enqueue(q, self);
    self->wait_state = EMBK_WAIT_INTEND_TO_BLOCK;
    EMBK_TRACE(EMB_TRACE_BLOCK, embk_thread_index(self), reason, q);
}

embk_wait_result_t embk_wait_commit(emb_tick_t deadline, uintptr_t *data_out)
{
    embk_thread_t *self = embk_cpu.current;
    emb_irq_key_t key;
    embk_wait_result_t result;

    if (deadline != EMBK_DEADLINE_FOREVER) {
        key = embk_lock_timeout(); /* section 2 */
        if (self->wait_state == EMBK_WAIT_INTEND_TO_BLOCK) {
            embk_timeout_arm(&self->timeout, deadline, self->wait_gen);
        }
        embk_unlock_timeout(key);
    }

    key = embk_lock_sched(); /* section 3 */
    if (self->wait_state == EMBK_WAIT_INTEND_TO_BLOCK) {
        self->wait_state = EMBK_WAIT_BLOCKED;
        embk_sched_switch_away(); /* P2; returns when woken, lock held */
    } else {
        /* a waker won in the window: nothing to do but drop the timeout */
        embk_timeout_disarm(&self->timeout);
    }
    EMBK_ASSERT(self->wait_state == EMBK_WAIT_READY);
    EMBK_ASSERT(self->wake_result != EMBK_WAKE_NONE);
    EMBK_ASSERT(self->wait_queue == NULL);
    result = self->wake_result;
    *data_out = self->wake_data;
    embk_unlock_sched(key);
    return result;
}

embk_wait_result_t embk_block_on(embk_wait_queue_t *q, uint8_t reason, emb_tick_t deadline,
                                 emb_irq_key_t key, uintptr_t *data_out)
{
    embk_wait_prepare(q, reason);
    embk_unlock_object(q, key);
    return embk_wait_commit(deadline, data_out);
}

/* ---- waking (§5.2) -------------------------------------------------------------- */

bool embk_wait_wake(embk_thread_t *t, embk_wait_result_t result, uintptr_t data)
{
    embk_wait_queue_t *q = t->wait_queue;
    uint8_t was;
    if (q == NULL) {
        return false; /* no longer waiting */
    }
    dequeue(q, t);
    t->wait_queue = NULL;
    t->wake_result = result; /* KRN-WAIT-005: result before READY */
    t->wake_data = data;
    t->wait_gen++; /* every later timeout, cancel, or flush for this wait is stale */
    was = t->wait_state;
    t->wait_state = EMBK_WAIT_READY;
#if CONFIG_EMB_MUTEX
    if ((q->flags & EMBK_WAIT_INHERIT) != 0u) {
        embk_mutex_on_waiters_changed(
            EMB_CONTAINER_OF(q, embk_mutex_t, waiters)); /* KRN-SYNC-014 */
    }
#endif
    EMBK_TRACE(EMB_TRACE_WAKE, embk_thread_index(t), result, data);
    if (was == EMBK_WAIT_INTEND_TO_BLOCK) {
        return true; /* t sees READY at commit; it is running or already in the ready structure */
    }
    EMBK_ASSERT(was == EMBK_WAIT_BLOCKED);
    embk_timeout_disarm(&t->timeout);
    if ((t->tflags & EMBK_THREAD_SUSPENDED) == 0u) {
        embk_sched_make_ready(t, false);
    }
    return true;
}

unsigned embk_wait_wake_all(embk_wait_queue_t *q, embk_wait_result_t result, uintptr_t data)
{
    unsigned n = 0u;
    embk_thread_t *t;
    /* bounded by the number of waiters */
    while ((t = embk_wait_first(q)) != NULL) {
        (void)embk_wait_wake(t, result, data);
        n++;
    }
    return n;
}

void embk_wait_requeue(embk_thread_t *t)
{
    embk_wait_queue_t *q = t->wait_queue;
    if (q == NULL) {
        return;
    }
#if !CONFIG_EMB_SCHED_TABLE
    if ((q->flags & EMBK_WAIT_FIFO) == 0u) {
        dequeue(q, t);
        enqueue(q, t); /* KRN-WAIT-003; the owner's recomputation is the next hop of the walk */
    }
#else
    (void)q;
#endif
}

/* ---- timeout, cancel, flush (§5.3, §6.6, §6.7) ------------------------------------- */

void embk_wait_wake_timeout(embk_thread_t *t, embk_wait_gen_t gen)
{
    if (t->wait_gen != gen || t->wait_queue == NULL) {
        return; /* stale: the wait completed */
    }
    EMBK_TRACE(EMB_TRACE_TIMEOUT_EXPIRE, embk_thread_index(t), 0u, 0u);
    (void)embk_wait_wake(t, EMBK_WAKE_TIMEOUT, 0u);
}

bool embk_wait_cancel(embk_thread_t *t)
{
    uint8_t r;
    if (t->cancel_disable != 0u || t->wait_queue == NULL) {
        return false;
    }
    r = t->wait_reason;
    if (r != EMB_WAIT_REASON_SLEEP && r != EMB_WAIT_REASON_OBJECT && r != EMB_WAIT_REASON_JOIN &&
        r != EMB_WAIT_REASON_NOTIFY) {
        return false;
    }
    t->tflags &= (uint8_t)~EMBK_THREAD_CANCEL_PENDING; /* delivery clears the request */
    (void)embk_wait_wake(t, EMBK_WAKE_CANCELED, 0u);
    return true;
}

unsigned embk_wait_flush(embk_wait_queue_t *q, embk_wait_result_t result)
{
    return embk_wait_wake_all(q, result, 0u);
}

emb_status_t embk_wait_result_status(embk_wait_result_t r)
{
    switch (r) {
    case EMBK_WAKE_SATISFIED:
        return EMB_OK;
    case EMBK_WAKE_TIMEOUT:
        return EMB_ETIMEDOUT;
    case EMBK_WAKE_CANCELED:
        return EMB_ECANCELED;
    case EMBK_WAKE_DESTROYED:
        return EMB_EDESTROYED;
    case EMBK_WAKE_STALE:
        return EMB_ESTALE;
    default:
        EMBK_ASSERT(false);
        return EMB_EINTR;
    }
}

/* ---- deadlines (SPEC-003 §5.2) -------------------------------------------------- */

bool embk_tick_before(emb_tick_t a, emb_tick_t b)
{
#if CONFIG_EMB_TICK_32BIT
    return (int32_t)(a - b) < 0; /* wrap-safe within 2^31 - 1 ticks */
#else
    return a < b;
#endif
}

bool embk_deadline_passed(emb_tick_t deadline)
{
    return !embk_tick_before(emb_time_now().ticks, deadline);
}

emb_tick_t embk_deadline_from_timeout(emb_timeout_t timeout)
{
    emb_tick_t now;
    emb_tick_t ticks = timeout.ticks;
    if (ticks == EMB_TICK_MAX) {
        return EMBK_DEADLINE_FOREVER;
    }
    if (ticks > EMB_TIMEOUT_MAX_TICKS) {
        ticks = EMB_TIMEOUT_MAX_TICKS;
    }
    now = emb_time_now().ticks;
#if CONFIG_EMB_TICK_32BIT
    return now + ticks; /* wraps by design; compared with embk_tick_before */
#else
    if (now > (EMB_TICK_MAX - 1u) - ticks) {
        return EMB_TICK_MAX - 1u; /* finite, never reads as forever */
    }
    return now + ticks;
#endif
}
