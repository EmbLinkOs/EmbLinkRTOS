/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Thread notifications (SPEC-006 §2): level-sensitive bits with hand-off on satisfaction. */
#include <emb/notify.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/thread.h>
#include <embk/wait.h>

#if CONFIG_EMB_NOTIFY

static bool satisfied(const embk_thread_t *t)
{
    emb_notify_bits_t got = t->notify_bits & t->notify_mask;
    if ((embk_thread_notify_mode(t) & EMB_NOTIFY_ALL) != 0u) {
        return got == t->notify_mask;
    }
    return got != 0u;
}

/* Consume the satisfying bits per the waiter's CLEAR flag; returns them. */
static emb_notify_bits_t consume(embk_thread_t *t)
{
    emb_notify_bits_t got = t->notify_bits & t->notify_mask;
    if ((embk_thread_notify_mode(t) & EMB_NOTIFY_CLEAR) != 0u) {
        t->notify_bits &= (emb_notify_bits_t)~got;
    }
    return got;
}

emb_status_t emb_notify_set(emb_thread_t thread, emb_notify_bits_t bits)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE(bits != 0u && (bits & (emb_notify_bits_t)~EMB_NOTIFY_APP_MASK) == 0u,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    key = embk_lock_object(t);
    if ((t->tflags & EMBK_THREAD_TERMINATED) != 0u) {
        embk_unlock_object(t, key);
        return EMB_ESTATE;
    }
    embk_notify_set_locked(t, bits);
    embk_sched_reschedule_if_needed(); /* P2 when called from a thread */
    embk_unlock_object(t, key);
    return EMB_OK;
}

void embk_notify_set_locked(embk_thread_t *t, emb_notify_bits_t bits)
{
    t->notify_bits |= bits;
    EMBK_TRACE(EMB_TRACE_NOTIFY_SET, embk_thread_index(t), bits, 0u);
    if (t->wait_queue == &t->self_q && t->wait_reason == EMB_WAIT_REASON_NOTIFY && satisfied(t)) {
        (void)embk_wait_wake(t, EMBK_WAKE_SATISFIED, (uintptr_t)consume(t));
    }
}

static emb_status_t wait_common(emb_notify_bits_t mask, uint8_t mode, emb_tick_t deadline,
                                bool nowait, emb_notify_bits_t *out_bits)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;

    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(mask != 0u, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_REQUIRE((mode & (uint8_t) ~(EMB_NOTIFY_ALL | EMB_NOTIFY_CLEAR)) == 0u,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_REQUIRE_CAN_BLOCK(nowait ? 0u : 1u);
    self = embk_cpu.current;
    key = embk_lock_object(self);
    if (embk_thread_take_cancel_locked(self)) {
        embk_unlock_object(self, key);
        return EMB_ECANCELED;
    }
    self->notify_mask = mask;
    embk_thread_set_notify_mode(self, mode);
    if (satisfied(self)) {
        emb_notify_bits_t got = consume(self);
        embk_unlock_object(self, key);
        if (out_bits != NULL) {
            *out_bits = got;
        }
        EMBK_TRACE(EMB_TRACE_NOTIFY_WAIT, embk_thread_index(self), EMB_OK, got);
        return EMB_OK;
    }
    if (nowait) {
        embk_unlock_object(self, key);
        return EMB_ETIMEDOUT;
    }
    r = embk_block_on(&self->self_q, EMB_WAIT_REASON_NOTIFY, deadline, key, &data);
    if (r == EMBK_WAKE_SATISFIED) {
        if (out_bits != NULL) {
            *out_bits = (emb_notify_bits_t)data;
        }
        EMBK_TRACE(EMB_TRACE_NOTIFY_WAIT, embk_thread_index(self), EMB_OK, data);
        return EMB_OK;
    }
    EMBK_TRACE(EMB_TRACE_NOTIFY_WAIT, embk_thread_index(self), embk_wait_result_status(r), 0u);
    return embk_wait_result_status(r);
}

emb_status_t emb_notify_wait(emb_notify_bits_t mask, uint8_t mode, emb_timeout_t timeout,
                             emb_notify_bits_t *out_bits)
{
    return wait_common(mask, mode, embk_deadline_from_timeout(timeout),
                       EMB_TIMEOUT_IS_NO_WAIT(timeout), out_bits);
}

emb_status_t emb_notify_wait_until(emb_notify_bits_t mask, uint8_t mode, emb_instant_t deadline,
                                   emb_notify_bits_t *out_bits)
{
    bool nowait = deadline.ticks != EMB_TICK_MAX && embk_deadline_passed(deadline.ticks);
    return wait_common(mask, mode, deadline.ticks, nowait, out_bits);
}

emb_notify_bits_t emb_notify_get(void)
{
    if (!embk_in_thread()) {
        return 0u;
    }
    return embk_cpu.current->notify_bits;
}

emb_status_t emb_notify_clear(emb_notify_bits_t bits)
{
    emb_irq_key_t key;
    EMBK_REQUIRE_THREAD();
    key = embk_lock_object(embk_cpu.current);
    embk_cpu.current->notify_bits &= (emb_notify_bits_t)~bits;
    embk_unlock_object(embk_cpu.current, key);
    return EMB_OK;
}

#endif /* CONFIG_EMB_NOTIFY */
