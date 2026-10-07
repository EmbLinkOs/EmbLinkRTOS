/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Thread lifecycle (SPEC-008), sleep and yield (SPEC-003 §6), priority changes
 * (SPEC-005 §10). Execution lives strictly inside the object's lifetime
 * (KRN-THR-006); storage reuse is a defined point, never an idle-task side effect.
 */
#include <emb/storage.h>
#include <emb/thread.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/sync.h>
#include <embk/thread.h>
#include <embk/time.h>
#include <embk/wait.h>

EMB_STATIC_ASSERT(offsetof(embk_thread_t, arch) == 0u,
                  "ports treat a thread pointer as its arch block");

#if CONFIG_EMB_THREAD_LIST
embk_list_t embk_thread_all;
#endif
#if !CONFIG_EMB_SCHED_TABLE
static uint8_t next_index; /* lock: scheduler domain */
#endif
#if CONFIG_EMB_IDLE_THREAD
EMB_THREAD_STACK(idle_stack, CONFIG_EMB_IDLE_STACK_SIZE);
#endif

#define IDLE_INDEX ((uint8_t)0xFEu)

void embk_thread_init_subsystem(void)
{
#if CONFIG_EMB_THREAD_LIST
    embk_list_init(&embk_thread_all);
#endif
#if !CONFIG_EMB_SCHED_TABLE
    next_index = 0u;
#endif
}

static void tcb_reset(embk_thread_t *t)
{
    (void)EMB_MEMSET(t, 0, sizeof(*t));
    embk_wait_queue_init(&t->self_q, EMBK_WAIT_SELF);
    embk_wait_queue_init(&t->join_q, EMBK_WAIT_JOIN);
    embk_list_node_init(&t->timeout.node);
#if !CONFIG_EMB_SCHED_TABLE
    embk_list_node_init(&t->wait_node);
    embk_list_node_init(&t->ready_node);
#endif
    t->wake_result = EMBK_WAKE_NONE;
}

#if CONFIG_EMB_IDLE_THREAD
static void idle_entry(void *arg)
{
    (void)arg;
    embk_idle_loop();
}
#endif

void embk_thread_init_idle(embk_thread_t *t, void *stack, size_t stack_size)
{
    tcb_reset(t);
    embk_obj_init(&t->obj, EMBK_OBJ_THREAD, 0u, "idle");
    t->base_prio = EMB_PRIORITY_IDLE;
    t->eff_prio = EMB_PRIORITY_IDLE;
    t->wait_state = EMBK_WAIT_READY;
    t->tflags = EMBK_THREAD_STARTED | EMBK_THREAD_KERNEL | EMBK_THREAD_DETACHED;
    t->cancel_disable = UINT8_MAX;
#if !CONFIG_EMB_SCHED_TABLE
    t->index = IDLE_INDEX;
#else
    embk_thread_table[EMB_PRIORITY_IDLE] = t;
#endif
#if CONFIG_EMB_IDLE_THREAD
    (void)stack;
    (void)stack_size;
    t->stack_base = idle_stack;
    t->stack_size = sizeof(idle_stack);
    (void)EMB_MEMSET(idle_stack, EMB_STACK_FILL, sizeof(idle_stack));
    emb_arch_context_init(t, idle_stack, sizeof(idle_stack), idle_entry, NULL, true);
#else
    t->stack_base =
        (uint8_t *)stack; /* the port supplies the pre-kernel stack at start (SPEC-012 §3) */
    t->stack_size = stack_size;
#endif
}

/* ---- attributes and construction (SPEC-008 §2, §4) --------------------------------- */

void emb_thread_attr_default(emb_thread_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->stack = NULL;
    out_attr->stack_size = 0u;
    out_attr->priority = EMB_PRIORITY_MIN;
    out_attr->flags = 0u;
    out_attr->cpu_affinity = 0u;
    out_attr->reserved_ = 0u;
}

emb_status_t emb_thread_init(emb_thread_storage_t *storage, const emb_thread_attr_t *attr,
                             emb_thread_entry_t entry, void *arg, emb_thread_t *out_thread)
{
    embk_thread_t *t;
    emb_irq_key_t key;

    if (out_thread != NULL) {
        out_thread->raw = 0u;
    }
    EMBK_REQUIRE_NOT_ISR();
    EMBK_REQUIRE(embk_cpu.kernel_state != EMBK_KERNEL_UNINIT, EMB_FAULT_API_LIFECYCLE, EMB_ESTATE,
                 0u);
    EMBK_REQUIRE(storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_REQUIRE(attr != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_REQUIRE(entry != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_REQUIRE(out_thread != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 5u);
    EMBK_REQUIRE(attr->priority >= EMB_PRIORITY_MIN && attr->priority <= EMB_PRIORITY_MAX,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_REQUIRE(attr->stack != NULL && ((uintptr_t)attr->stack % (uintptr_t)EMB_STACK_ALIGN) == 0u,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    {
        size_t min_stack = EMB_THREAD_STACK_MIN;
        if (min_stack < EMBK_STACK_GUARD_BYTES + (size_t)CONFIG_EMB_ARCH_CONTEXT_FRAME) {
            min_stack = EMBK_STACK_GUARD_BYTES + (size_t)CONFIG_EMB_ARCH_CONTEXT_FRAME;
        }
        EMBK_REQUIRE(attr->stack_size >= min_stack, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    }
    EMBK_REQUIRE(((uintptr_t)storage % (uintptr_t)EMB_THREAD_STORAGE_ALIGN) == 0u,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMB_STATIC_ASSERT(sizeof(embk_thread_t) <= EMB_THREAD_STORAGE_SIZE,
                      "generated storage too small");

    t = (embk_thread_t *)(void *)
        storage; /* the storage union is sized and aligned for it (CS-4.5) */
    key = embk_lock_sched();
#if CONFIG_EMB_SCHED_TABLE
    if (embk_thread_table[attr->priority] != NULL) {
        embk_unlock_sched(key);
        return EMB_EBUSY; /* one thread per priority (ADR-036) */
    }
#endif
    tcb_reset(t);
    embk_obj_init(&t->obj, EMBK_OBJ_THREAD, 0u, attr->name);
    t->base_prio = attr->priority;
    t->eff_prio = attr->priority;
    t->wait_state = EMBK_WAIT_BLOCKED; /* INACTIVE: BLOCKED with reason START (KRN-THR-009) */
    t->wait_reason = EMB_WAIT_REASON_START;
    t->tflags = 0u;
    if ((attr->flags & EMB_THREAD_DETACHED) != 0u) {
        t->tflags |= EMBK_THREAD_DETACHED;
    }
    if ((attr->flags & EMB_THREAD_CRITICAL) != 0u) {
        t->tflags |= EMBK_THREAD_CRITICAL;
    }
    t->cancel_disable = ((attr->flags & EMB_THREAD_CANCEL_DISABLED) != 0u) ? 1u : 0u;
    t->stack_base = (uint8_t *)attr->stack;
    t->stack_size = attr->stack_size;
#if CONFIG_EMB_CHECKED || CONFIG_EMB_STACK_STATS
    (void)EMB_MEMSET(attr->stack, EMB_STACK_FILL, attr->stack_size);
#elif CONFIG_EMB_STACK_CHECK
    (void)EMB_MEMSET(attr->stack, EMB_STACK_FILL, EMBK_STACK_GUARD_BYTES);
#endif
    emb_arch_context_init(t, attr->stack, attr->stack_size, entry, arg, false);
#if CONFIG_EMB_SCHED_TABLE
    embk_thread_table[attr->priority] = t;
#else
    t->index = next_index;
    if (next_index < IDLE_INDEX - 1u) {
        next_index++;
    }
#endif
#if CONFIG_EMB_THREAD_LIST
    embk_list_append(&embk_thread_all, &t->all_node);
#endif
    embk_unlock_sched(key);
    *out_thread = embk_thread_handle(t);
    EMBK_TRACE(EMB_TRACE_THREAD_INIT, embk_thread_index(t), attr->priority, 0u);
    return EMB_OK;
}

emb_status_t emb_thread_start(emb_thread_t thread)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_sched();
    if ((t->tflags & EMBK_THREAD_STARTED) != 0u) {
        embk_unlock_sched(key);
        return EMB_ESTATE;
    }
    t->tflags |= EMBK_THREAD_STARTED;
    t->wait_state = EMBK_WAIT_READY;
    t->wait_reason = EMB_WAIT_REASON_NONE;
    EMBK_TRACE(EMB_TRACE_THREAD_START, embk_thread_index(t), 0u, 0u);
    if ((t->tflags & EMBK_THREAD_SUSPENDED) == 0u) {
        embk_sched_make_ready(t, false);
    }
    embk_sched_reschedule_if_needed();
    embk_unlock_sched(key);
    return EMB_OK;
}

emb_thread_t emb_thread_self(void)
{
    emb_thread_t h;
    h.raw = 0u;
    if (embk_in_thread()) {
        h = embk_thread_handle(embk_cpu.current);
    }
    return h;
}

/* ---- termination (SPEC-008 §5) ------------------------------------------------------ */

void emb_thread_exit(int code)
{
    embk_thread_t *self;
    embk_thread_t *joiner;
    emb_irq_key_t key;

    if (!embk_in_thread() || embk_cpu.current == &embk_idle_thread) {
        embk_fault_raise(EMB_FAULT_API_CONTEXT, 0u, 0u, __func__);
    }
    self = embk_cpu.current;
    key = embk_lock_sched();
    self->cancel_disable = UINT8_MAX; /* step 1: no further delivery */
#if CONFIG_EMB_MUTEX
    embk_mutex_release_all_on_exit(self); /* step 2 */
#endif
    EMBK_TRACE(EMB_TRACE_THREAD_EXIT, embk_thread_index(self), code, 0u); /* step 3 */
    /* step 4, one critical section */
    self->exit_code = code;
    joiner = embk_wait_first(&self->join_q);
    if (joiner != NULL) {
        self->tflags |= EMBK_THREAD_JOINED;
        (void)embk_wait_wake(joiner, EMBK_WAKE_SATISFIED, (uintptr_t)(intptr_t)code);
    }
    embk_timeout_disarm(&self->timeout);
#if CONFIG_EMB_NOTIFY
    self->notify_bits = 0u;
#endif
    EMBK_ASSERT(self->wait_queue == NULL);
    self->tflags |= EMBK_THREAD_TERMINATED;
    self->wait_state = EMBK_WAIT_READY;
    (void)key;
    embk_sched_switch_final(); /* step 5 is the port's switch-out completion */
}

void embk_thread_launch(emb_thread_entry_t entry, void *arg)
{
    /* A fresh context starts with interrupts enabled (SPEC-012 §4.2): no critical
     * section is held, whatever the switching thread held when it left. */
#if CONFIG_EMB_CHECKED
    embk_cpu.irq_lock_depth = 0u;
#endif
    if (entry == NULL) {
        embk_idle_loop();
    }
    entry(arg);
    emb_thread_exit(0);
}

bool embk_thread_take_cancel_locked(embk_thread_t *self)
{
    if ((self->tflags & EMBK_THREAD_CANCEL_PENDING) != 0u && self->cancel_disable == 0u) {
        self->tflags &= (uint8_t)~EMBK_THREAD_CANCEL_PENDING;
        return true;
    }
    return false;
}

/* ---- join and detach (SPEC-008 §6) ------------------------------------------------- */

emb_status_t emb_thread_join(emb_thread_t thread, emb_timeout_t timeout, int *out_code)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    emb_tick_t deadline;

    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    self = embk_cpu.current;
    EMBK_REQUIRE(t != self, EMB_FAULT_API_OWNER, EMB_EDEADLK, 1u);
    EMBK_REQUIRE_CAN_BLOCK(timeout.ticks);
    key = embk_lock_sched();
    if ((t->tflags & (EMBK_THREAD_DETACHED | EMBK_THREAD_KERNEL)) != 0u) {
        embk_unlock_sched(key);
        return ((t->tflags & EMBK_THREAD_KERNEL) != 0u) ? EMB_EPERM : EMB_EINVAL;
    }
    if (embk_thread_take_cancel_locked(self)) {
        embk_unlock_sched(key);
        return EMB_ECANCELED;
    }
    if ((t->tflags & EMBK_THREAD_TERMINATED) != 0u) {
        int code = t->exit_code;
        t->tflags |= EMBK_THREAD_JOINED;
        embk_unlock_sched(key);
        if (out_code != NULL) {
            *out_code = code;
        }
        EMBK_TRACE(EMB_TRACE_THREAD_JOIN, embk_thread_index(self), embk_thread_index(t), EMB_OK);
        return EMB_OK;
    }
    if (!embk_wait_queue_is_empty(&t->join_q)) {
        embk_unlock_sched(key);
        return EMB_EBUSY; /* one joiner */
    }
    if (EMB_TIMEOUT_IS_NO_WAIT(timeout)) {
        embk_unlock_sched(key);
        return EMB_ETIMEDOUT;
    }
    deadline = embk_deadline_from_timeout(timeout);
    r = embk_block_on(&t->join_q, EMB_WAIT_REASON_JOIN, deadline, key, &data);
    EMBK_TRACE(EMB_TRACE_THREAD_JOIN, embk_thread_index(self), embk_thread_index(t),
               embk_wait_result_status(r));
    if (r == EMBK_WAKE_SATISFIED) {
        if (out_code != NULL) {
            *out_code = (int)(intptr_t)data;
        }
        return EMB_OK;
    }
    return embk_wait_result_status(r);
}

emb_status_t emb_thread_detach(emb_thread_t thread)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_sched();
    if ((t->tflags & EMBK_THREAD_KERNEL) != 0u) {
        embk_unlock_sched(key);
        return EMB_EPERM;
    }
    if (!embk_wait_queue_is_empty(&t->join_q)) {
        embk_unlock_sched(key);
        return EMB_EBUSY;
    }
    t->tflags |= EMBK_THREAD_DETACHED;
    if ((t->tflags & EMBK_THREAD_TERMINATED) != 0u) {
        t->tflags |= EMBK_THREAD_JOINED; /* reusable now */
    }
    embk_unlock_sched(key);
    return EMB_OK;
}

/* ---- suspend, resume, cancel (SPEC-008 §7) ------------------------------------------ */

emb_status_t emb_thread_suspend(emb_thread_t thread)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE((t->tflags & EMBK_THREAD_KERNEL) == 0u, EMB_FAULT_API_OWNER, EMB_EPERM, 1u);
    if (t == embk_cpu.current) {
        EMBK_REQUIRE_CAN_BLOCK(1u); /* self-suspend switches away */
    }
    key = embk_lock_sched();
    if ((t->tflags & EMBK_THREAD_STARTED) == 0u || (t->tflags & EMBK_THREAD_TERMINATED) != 0u) {
        embk_unlock_sched(key);
        return EMB_ESTATE;
    }
    if ((t->tflags & EMBK_THREAD_SUSPENDED) != 0u) {
        embk_unlock_sched(key);
        return EMB_OK; /* idempotent */
    }
    t->tflags |= EMBK_THREAD_SUSPENDED;
    EMBK_TRACE(EMB_TRACE_SUSPEND, embk_thread_index(t), 0u, 0u);
    if (embk_sched_is_ready(t)) {
        embk_sched_remove_ready(t);
    }
    if (t == embk_cpu.current) {
        embk_sched_switch_away(); /* returns after resume */
    }
    embk_unlock_sched(key);
    return EMB_OK;
}

emb_status_t emb_thread_resume(emb_thread_t thread)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_sched();
    if ((t->tflags & EMBK_THREAD_STARTED) == 0u || (t->tflags & EMBK_THREAD_TERMINATED) != 0u) {
        embk_unlock_sched(key);
        return EMB_ESTATE;
    }
    if ((t->tflags & EMBK_THREAD_SUSPENDED) == 0u) {
        embk_unlock_sched(key);
        return EMB_OK; /* idempotent */
    }
    t->tflags &= (uint8_t)~EMBK_THREAD_SUSPENDED;
    EMBK_TRACE(EMB_TRACE_RESUME, embk_thread_index(t), 0u, 0u);
    /* runnable unless committed to BLOCKED: a thread suspended inside the INTEND_TO_BLOCK
     * window resumes into the window (reference model finding, SPEC-004 §6.5) */
    if (t->wait_state != EMBK_WAIT_BLOCKED && t != embk_cpu.current && !embk_sched_is_ready(t)) {
        embk_sched_make_ready(t, false);
    }
    embk_sched_reschedule_if_needed();
    embk_unlock_sched(key);
    return EMB_OK;
}

emb_status_t emb_thread_cancel(emb_thread_t thread)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    bool delivered;
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE((t->tflags & EMBK_THREAD_KERNEL) == 0u, EMB_FAULT_API_OWNER, EMB_EPERM, 1u);
    key = embk_lock_sched();
    if ((t->tflags & EMBK_THREAD_STARTED) == 0u || (t->tflags & EMBK_THREAD_TERMINATED) != 0u) {
        embk_unlock_sched(key);
        return EMB_ESTATE;
    }
    t->tflags |= EMBK_THREAD_CANCEL_PENDING;
    delivered = embk_wait_cancel(t);
    EMBK_TRACE(EMB_TRACE_CANCEL, embk_thread_index(t), delivered, 0u);
    embk_sched_reschedule_if_needed();
    embk_unlock_sched(key);
    return EMB_OK;
}

emb_status_t emb_thread_cancel_point(void)
{
    emb_irq_key_t key;
    bool taken;
    EMBK_REQUIRE_THREAD();
    key = embk_lock_sched();
    taken = embk_thread_take_cancel_locked(embk_cpu.current);
    embk_unlock_sched(key);
    return taken ? EMB_ECANCELED : EMB_OK;
}

emb_status_t emb_thread_cancel_disable(void)
{
    embk_thread_t *self;
    EMBK_REQUIRE_THREAD();
    self = embk_cpu.current;
    EMBK_REQUIRE(self->cancel_disable < UINT8_MAX, EMB_FAULT_API_ARGUMENT, EMB_EOVERFLOW, 0u);
    self->cancel_disable++; /* own field, thread context: no lock needed */
    return EMB_OK;
}

emb_status_t emb_thread_cancel_enable(void)
{
    embk_thread_t *self;
    EMBK_REQUIRE_THREAD();
    self = embk_cpu.current;
    EMBK_REQUIRE(self->cancel_disable != 0u, EMB_FAULT_API_ARGUMENT, EMB_ESTATE, 0u);
    self->cancel_disable--;
    return EMB_OK;
}

/* ---- priorities (SPEC-005 §10) ----------------------------------------------------- */

emb_status_t emb_thread_set_priority(emb_thread_t thread, uint8_t priority)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE(priority >= EMB_PRIORITY_MIN && priority <= EMB_PRIORITY_MAX,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_REQUIRE((t->tflags & EMBK_THREAD_KERNEL) == 0u, EMB_FAULT_API_OWNER, EMB_EPERM, 1u);
    key = embk_lock_sched();
#if CONFIG_EMB_SCHED_TABLE
    if (priority != t->base_prio) {
        uint8_t old = t->base_prio;
        if (embk_thread_table[priority] != NULL) {
            embk_unlock_sched(key);
            return EMB_EBUSY;
        }
        /* the slot is the identity on this profile: move every bit that names it */
        {
            bool ready = embk_sched_is_ready(t);
            embk_wait_queue_t *q = t->wait_queue;
            if (ready) {
                embk_sched_remove_ready(t);
            }
            if (q != NULL) {
                q->set &= (embk_word_t)~EMBK_WORD_BIT(old);
            }
            embk_thread_table[old] = NULL;
            embk_thread_table[priority] = t;
            t->base_prio = priority;
            if (q != NULL) {
                q->set |= EMBK_WORD_BIT(priority);
            }
            if (ready) {
                /* re-insert directly: embk_sched_make_ready would also test preemption, which the
                 * recomputation below does with the final effective priority */
                embk_sched_make_ready(t, false);
            }
        }
        if (t->eff_prio != old) {
            embk_sched_raised_count--; /* recomputed against the new base below */
        }
        t->eff_prio = old; /* recompute from the new base */
        if (t->eff_prio != t->base_prio) {
            embk_sched_raised_count++;
        }
    }
#else
    t->base_prio = priority;
#endif
    embk_prio_propagate(t);
    EMBK_TRACE(EMB_TRACE_PRIORITY, embk_thread_index(t), t->base_prio, t->eff_prio);
    embk_sched_reschedule_if_needed();
    embk_unlock_sched(key);
    return EMB_OK;
}

uint8_t emb_thread_get_priority(emb_thread_t thread)
{
    const embk_thread_t *t = embk_thread_from_handle(thread);
    return (t == NULL) ? 0u : t->base_prio;
}

uint8_t emb_thread_get_effective_priority(emb_thread_t thread)
{
    const embk_thread_t *t = embk_thread_from_handle(thread);
    return (t == NULL) ? 0u : t->eff_prio;
}

/* ---- yield and sleep (SPEC-003 §6) ------------------------------------------------- */

void emb_thread_yield(void)
{
    emb_irq_key_t key;
    if (!embk_in_thread()) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise(EMB_FAULT_API_CONTEXT, 0u, 0u, __func__);
#else
        return;
#endif
    }
    key = embk_lock_sched();
    embk_sched_yield_locked();
    embk_unlock_sched(key);
}

static emb_status_t sleep_until_deadline(emb_tick_t deadline)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    EMBK_REQUIRE_CAN_BLOCK(1u);
    self = embk_cpu.current;
    key = embk_lock_sched();
    if (embk_thread_take_cancel_locked(self)) {
        embk_unlock_sched(key);
        return EMB_ECANCELED;
    }
    EMBK_TRACE(EMB_TRACE_SLEEP, embk_thread_index(self), (uint32_t)deadline, 0u);
    r = embk_block_on(&self->self_q, EMB_WAIT_REASON_SLEEP, deadline, key, &data);
    if (r == EMBK_WAKE_TIMEOUT) {
        return EMB_OK;
    }
    return embk_wait_result_status(r);
}

emb_status_t emb_thread_sleep(emb_duration_t d)
{
    EMBK_REQUIRE_THREAD();
    if (d.ticks == 0u) {
        emb_thread_yield();
        return EMB_OK;
    }
    return sleep_until_deadline(embk_deadline_from_timeout(emb_timeout_from_duration(d)));
}

emb_status_t emb_thread_sleep_until(emb_instant_t t)
{
    EMBK_REQUIRE_THREAD();
#if CONFIG_EMB_TICK_32BIT
    {
        emb_tick_t now = emb_time_now().ticks;
        if (!embk_tick_before(now, t.ticks)) {
            emb_thread_yield();
            return EMB_OK;
        }
        if ((emb_tick_t)(t.ticks - now) > EMB_TIMEOUT_MAX_TICKS) {
            return EMB_EOVERFLOW; /* KRN-TIM-022 */
        }
    }
#else
    if (embk_deadline_passed(t.ticks)) {
        emb_thread_yield();
        return EMB_OK;
    }
    if (t.ticks == EMB_TICK_MAX) {
        t.ticks = EMB_TICK_MAX - 1u; /* finite, never reads as forever */
    }
#endif
    return sleep_until_deadline(t.ticks);
}

/* ---- queries (SPEC-008 §3) --------------------------------------------------------- */

emb_status_t emb_thread_state(emb_thread_t thread, uint8_t *out_state, uint8_t *out_reason)
{
    const embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    uint8_t state;
    uint8_t reason = EMB_WAIT_REASON_NONE;
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE(out_state != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    key = embk_lock_sched();
    if ((t->tflags & EMBK_THREAD_STARTED) == 0u) {
        state = EMB_THREAD_INACTIVE;
    } else if ((t->tflags & EMBK_THREAD_TERMINATED) != 0u) {
        state = EMB_THREAD_TERMINATED;
    } else if (t == embk_cpu.current) {
        state = EMB_THREAD_RUNNING;
    } else if ((t->tflags & EMBK_THREAD_SUSPENDED) != 0u) {
        state = EMB_THREAD_BLOCKED;
        reason = EMB_WAIT_REASON_SUSPEND;
    } else if (t->wait_state == EMBK_WAIT_BLOCKED) {
        state = EMB_THREAD_BLOCKED;
        reason = t->wait_reason;
    } else {
        state = EMB_THREAD_READY;
    }
    embk_unlock_sched(key);
    *out_state = state;
    if (out_reason != NULL) {
        *out_reason = reason;
    }
    return EMB_OK;
}

const char *emb_thread_name(emb_thread_t thread)
{
    const embk_thread_t *t = embk_thread_from_handle(thread);
#if CONFIG_EMB_OBJ_NAMES
    if (t != NULL && t->obj.name != NULL) {
        return t->obj.name;
    }
#else
    (void)t;
#endif
    return "";
}

uint8_t emb_thread_index(emb_thread_t thread)
{
    const embk_thread_t *t = embk_thread_from_handle(thread);
    return (t == NULL) ? 0xFFu : embk_thread_index(t);
}

emb_status_t emb_thread_stack_info(emb_thread_t thread, size_t *out_size, size_t *out_high_water)
{
    const embk_thread_t *t = embk_thread_from_handle(thread);
    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    if (out_size != NULL) {
        *out_size = t->stack_size;
    }
    if (out_high_water != NULL) {
        size_t used = 0u;
#if CONFIG_EMB_STACK_STATS
        if (t->stack_base != NULL) {
            size_t i = 0u;
            /* bounded by the stack size: first byte that is not the fill pattern */
            while (i < t->stack_size && t->stack_base[i] == EMB_STACK_FILL) {
                i++;
            }
            used = t->stack_size - i;
        }
#endif
        *out_high_water = used;
    }
    return EMB_OK;
}

#if CONFIG_EMB_TLS_SLOTS > 0
emb_status_t emb_tls_set(uint8_t slot, void *p)
{
    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(slot < (uint8_t)CONFIG_EMB_TLS_SLOTS, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    embk_cpu.current->tls[slot] = p;
    return EMB_OK;
}

void *emb_tls_get(uint8_t slot)
{
    if (!embk_in_thread() || slot >= (uint8_t)CONFIG_EMB_TLS_SLOTS) {
        return NULL;
    }
    return embk_cpu.current->tls[slot];
}
#endif

#if CONFIG_EMB_THREAD_LIST
void emb_thread_foreach(emb_thread_visit_t cb, void *arg)
{
    embk_list_node_t *n;
    if (cb == NULL || !embk_in_thread()) {
        return;
    }
    emb_sched_lock();
    /* bounded by the number of threads */
    for (n = embk_list_first(&embk_thread_all); n != NULL;
         n = embk_list_next(&embk_thread_all, n)) {
        cb(embk_thread_handle(EMB_CONTAINER_OF(n, embk_thread_t, all_node)), arg);
    }
    emb_sched_unlock();
}
#endif

emb_status_t emb_thread_destroy(emb_thread_t thread)
{
    embk_thread_t *t = embk_thread_from_handle(thread);
    emb_irq_key_t key;
    bool ok;
    EMBK_REQUIRE_NOT_ISR();
    EMBK_REQUIRE(t != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE((t->tflags & EMBK_THREAD_KERNEL) == 0u, EMB_FAULT_API_OWNER, EMB_EPERM, 1u);
    key = embk_lock_sched();
    ok = (t->tflags & EMBK_THREAD_STARTED) == 0u ||
         ((t->tflags & EMBK_THREAD_TERMINATED) != 0u && (t->tflags & EMBK_THREAD_JOINED) != 0u &&
          emb_arch_switch_out_done(t));
    if (!ok) {
        embk_unlock_sched(key);
        EMBK_MISUSE(EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
#if CONFIG_EMB_THREAD_LIST
    embk_list_remove(&t->all_node);
#endif
#if CONFIG_EMB_SCHED_TABLE
    embk_thread_table[t->base_prio] = NULL;
#endif
    t->obj.state = EMBK_OBJ_UNINIT;
    embk_unlock_sched(key);
    return EMB_OK;
}
