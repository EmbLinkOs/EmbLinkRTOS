/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Mutexes with priority inheritance and ceilings (SPEC-005 §2, §3). Effective
 * priority is recomputed from the owned mutexes' first waiters and ceilings; the walk
 * along the owner chain is bounded by CONFIG_EMB_PI_MAX_DEPTH and shared with deadlock
 * detection. The wait queue of an INHERIT mutex carries EMBK_WAIT_INHERIT so that the
 * wait protocol tells the owner about every dequeue (KRN-SYNC-014).
 */
#include <emb/mutex.h>
#include <emb/storage.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/sync.h>
#include <embk/thread.h>
#include <embk/wait.h>

#if CONFIG_EMB_MUTEX

EMB_STATIC_ASSERT(sizeof(embk_mutex_t) <= EMB_MUTEX_STORAGE_SIZE, "generated storage too small");

static EMB_INLINE embk_mutex_t *mutex_from_handle(emb_mutex_t h)
{
    return (embk_mutex_t *)embk_obj_from_raw(h.raw, EMBK_OBJ_MUTEX, offsetof(embk_mutex_t, obj));
}

/* ---- owned list: singly linked, acquisition order (SPEC-005 §2.3) ------------------- */

static void owned_append(embk_thread_t *t, embk_mutex_t *m)
{
    embk_mutex_t **pp = &t->owned;
    /* bounded by the number of owned mutexes */
    while (*pp != NULL) {
        pp = &(*pp)->owned_next;
    }
    m->owned_next = NULL;
    *pp = m;
}

static void owned_remove(embk_thread_t *t, embk_mutex_t *m)
{
    embk_mutex_t **pp = &t->owned;
    while (*pp != NULL && *pp != m) {
        pp = &(*pp)->owned_next;
    }
    EMBK_ASSERT(*pp == m);
    if (*pp == m) {
        *pp = m->owned_next;
    }
    m->owned_next = NULL;
}

/* ---- effective priority (SPEC-005 §2) -------------------------------------------------- */

bool embk_prio_recompute(embk_thread_t *t)
{
    uint8_t eff = t->base_prio;
    uint8_t old = t->eff_prio;
    const embk_mutex_t *m;
    /* bounded by the number of owned mutexes; each term is O(1) */
    for (m = t->owned; m != NULL; m = m->owned_next) {
        if (m->protocol == EMB_MUTEX_INHERIT) {
            const embk_thread_t *w = embk_wait_first(&m->waiters);
            if (w != NULL && w->eff_prio > eff) {
                eff = w->eff_prio;
            }
        } else if (m->protocol == EMB_MUTEX_CEILING) {
            if (m->ceiling > eff) {
                eff = m->ceiling;
            }
        } else {
            /* EMB_MUTEX_NONE contributes nothing */
        }
    }
    if (eff == old) {
        return false;
    }
    t->eff_prio = eff;
    embk_sched_prio_changed(t, old);
    embk_wait_requeue(t); /* KRN-WAIT-003, when blocked on a PRIORITY_FIFO queue */
    EMBK_TRACE(EMB_TRACE_PRIORITY, embk_thread_index(t), t->base_prio, eff);
    return true;
}

void embk_prio_propagate(embk_thread_t *t)
{
    unsigned depth;
    /* bounded by CONFIG_EMB_PI_MAX_DEPTH hops (SPEC-005 §2.2) */
    for (depth = 0u; t != NULL; depth++) {
        const embk_wait_queue_t *q;
        if (depth >= (unsigned)CONFIG_EMB_PI_MAX_DEPTH) {
            EMBK_TRACE(EMB_TRACE_PI_DEPTH, embk_thread_index(t), depth, 0u);
#if CONFIG_EMB_CHECKED
            embk_fault_raise(EMB_FAULT_PI_DEPTH, (uint16_t)depth, 0u, EMBK_WHERE);
#else
            return; /* inheritance truncated at the declared depth */
#endif
        }
        if (!embk_prio_recompute(t)) {
            return; /* a lowering or raise stops where nothing changes */
        }
        q = t->wait_queue;
        if (q == NULL || (q->flags & EMBK_WAIT_INHERIT) == 0u) {
            return;
        }
        t = EMB_CONTAINER_OF_CONST(q, embk_mutex_t, waiters)->owner;
    }
}

void embk_mutex_on_waiters_changed(embk_mutex_t *m)
{
    if (m->owner != NULL) {
        embk_prio_propagate(m->owner);
    }
}

/* ---- attributes and construction (§3.1) ------------------------------------------------ */

void emb_mutex_attr_default(emb_mutex_attr_t *out_attr)
{
    if (out_attr == NULL) {
        return;
    }
    out_attr->name = NULL;
    out_attr->protocol = EMB_MUTEX_INHERIT;
    out_attr->ceiling = 0u;
    out_attr->flags = 0u;
    out_attr->reserved_ = 0u;
}

emb_status_t emb_mutex_init(emb_mutex_storage_t *storage, const emb_mutex_attr_t *attr,
                            emb_mutex_t *out_mutex)
{
    embk_mutex_t *m;
    emb_mutex_attr_t def;
    if (out_mutex != NULL) {
        out_mutex->raw = 0u;
    }
    EMBK_REQUIRE_NOT_ISR();
    EMBK_REQUIRE(storage != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_REQUIRE(out_mutex != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_REQUIRE(((uintptr_t)storage % (uintptr_t)EMB_MUTEX_STORAGE_ALIGN) == 0u,
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (attr == NULL) {
        emb_mutex_attr_default(&def);
        attr = &def;
    }
    EMBK_REQUIRE(attr->protocol <= EMB_MUTEX_NONE, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_REQUIRE(attr->protocol != EMB_MUTEX_CEILING ||
                     (attr->ceiling >= EMB_PRIORITY_MIN && attr->ceiling <= EMB_PRIORITY_MAX),
                 EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
#if !CONFIG_EMB_MUTEX_INHERIT
    if (attr->protocol == EMB_MUTEX_INHERIT) {
        return EMB_ENOTSUP;
    }
#endif
    m = (embk_mutex_t *)(void *)storage;
    embk_obj_init(&m->obj, EMBK_OBJ_MUTEX,
                  attr->flags & (EMB_MUTEX_RECURSIVE | EMB_OBJ_ABORT_WAITERS), attr->name);
    embk_wait_queue_init(&m->waiters,
                         (attr->protocol == EMB_MUTEX_INHERIT) ? EMBK_WAIT_INHERIT : 0u);
    m->owner = NULL;
    m->owned_next = NULL;
    m->protocol = attr->protocol;
    m->ceiling = attr->ceiling;
    m->count = 0u;
    m->reserved_ = 0u;
    out_mutex->raw = embk_obj_to_raw(m);
    return EMB_OK;
}

emb_status_t emb_mutex_destroy(emb_mutex_t mutex)
{
    embk_mutex_t *m = mutex_from_handle(mutex);
    emb_irq_key_t key;
    EMBK_REQUIRE_NOT_ISR();
    EMBK_REQUIRE(m != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(m);
    if (m->owner != NULL && m->owner != embk_cpu.current) {
        embk_unlock_object(m, key);
        EMBK_MISUSE(EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
    }
    if (!embk_wait_queue_is_empty(&m->waiters)) {
        if ((m->obj.flags & EMB_OBJ_ABORT_WAITERS) == 0u) {
            embk_unlock_object(m, key);
            EMBK_MISUSE(EMB_FAULT_API_LIFECYCLE, EMB_EBUSY, 1u);
        }
        (void)embk_wait_flush(&m->waiters,
                              EMBK_WAKE_DESTROYED); /* recomputes the owner through the hook */
    }
    if (m->owner != NULL) {
        owned_remove(m->owner, m);
        m->owner = NULL;
        embk_prio_propagate(embk_cpu.current);
    }
    m->obj.state = EMBK_OBJ_UNINIT;
    embk_sched_reschedule_if_needed();
    embk_unlock_object(m, key);
    return EMB_OK;
}

/* ---- lock (§3.2, §3.5) ------------------------------------------------------------------ */

/* Does the owner chain starting at @owner reach @self? Bounded by CONFIG_EMB_PI_MAX_DEPTH. */
static bool chain_reaches(const embk_thread_t *owner, const embk_thread_t *self)
{
    unsigned depth;
    const embk_thread_t *t = owner;
    for (depth = 0u; t != NULL && depth < (unsigned)CONFIG_EMB_PI_MAX_DEPTH; depth++) {
        const embk_wait_queue_t *q;
        if (t == self) {
            return true;
        }
        q = t->wait_queue;
        if (q == NULL || (q->flags & EMBK_WAIT_INHERIT) == 0u) {
            return false;
        }
        t = EMB_CONTAINER_OF_CONST(q, embk_mutex_t, waiters)->owner;
    }
    return false;
}

static emb_status_t lock_common(embk_mutex_t *m, emb_tick_t deadline, bool nowait)
{
    embk_thread_t *self;
    emb_irq_key_t key;
    embk_wait_result_t r;
    uintptr_t data;
    emb_status_t st;

    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(m != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    EMBK_REQUIRE_CAN_BLOCK(nowait ? 0u : 1u);
    self = embk_cpu.current;
    key = embk_lock_object(m);
    if (embk_thread_take_cancel_locked(self)) {
        embk_unlock_object(m, key);
        return EMB_ECANCELED;
    }
    if (m->owner == NULL) {
        if (m->protocol == EMB_MUTEX_CEILING && self->base_prio > m->ceiling) {
            embk_unlock_object(m, key);
            EMBK_MISUSE(EMB_FAULT_API_OWNER, EMB_EPERM, 1u); /* ceiling violation (§3.4) */
        }
        m->owner = self;
        m->count = 1u;
        owned_append(self, m);
        if (m->protocol == EMB_MUTEX_CEILING) {
            (void)embk_prio_recompute(self); /* raise to the ceiling at once */
        }
        st = ((m->obj.flags & EMBK_MUTEX_INCONSISTENT) != 0u) ? EMB_EOWNERDEAD : EMB_OK;
        embk_unlock_object(m, key);
        EMBK_TRACE(EMB_TRACE_MUTEX_LOCK, embk_thread_index(self), m, st);
        return st;
    }
    if (m->owner == self) {
        if ((m->obj.flags & EMB_MUTEX_RECURSIVE) != 0u) {
            if (m->count == UINT8_MAX) {
                embk_unlock_object(m, key);
                return EMB_EOVERFLOW;
            }
            m->count++;
            embk_unlock_object(m, key);
            return EMB_OK;
        }
        embk_unlock_object(m, key);
        EMBK_MISUSE(EMB_FAULT_API_OWNER, EMB_EDEADLK, 1u); /* self-deadlock */
    }
    if (nowait) {
        embk_unlock_object(m, key);
        return EMB_ETIMEDOUT;
    }
    if (m->protocol == EMB_MUTEX_INHERIT && chain_reaches(m->owner, self)) {
        embk_unlock_object(m, key);
        EMBK_MISUSE(EMB_FAULT_API_OWNER, EMB_EDEADLK, 1u); /* KRN-SYNC-015 */
    }
    embk_wait_prepare(&m->waiters, EMB_WAIT_REASON_OBJECT);
    if (m->protocol == EMB_MUTEX_INHERIT) {
        embk_prio_propagate(m->owner); /* raise the owner and its chain */
    }
    embk_unlock_object(m, key);
    r = embk_wait_commit(deadline, &data);
    if (r == EMBK_WAKE_SATISFIED) {
        st = ((data & EMBK_MUTEX_OWNERDEAD_DATA) != 0u) ? EMB_EOWNERDEAD
                                                        : EMB_OK; /* self owns m by hand-off */
    } else {
        st = embk_wait_result_status(r);
    }
    EMBK_TRACE(EMB_TRACE_MUTEX_LOCK, embk_thread_index(self), m, st);
    return st;
}

emb_status_t emb_mutex_lock(emb_mutex_t mutex, emb_timeout_t timeout)
{
    return lock_common(mutex_from_handle(mutex), embk_deadline_from_timeout(timeout),
                       EMB_TIMEOUT_IS_NO_WAIT(timeout));
}

emb_status_t emb_mutex_lock_until(emb_mutex_t mutex, emb_instant_t deadline)
{
    bool nowait = deadline.ticks != EMB_TICK_MAX && embk_deadline_passed(deadline.ticks);
    return lock_common(mutex_from_handle(mutex), deadline.ticks, nowait);
}

/* ---- unlock (§3.3) ------------------------------------------------------------------------ */

/* Give @m to its first waiter or free it; lock held. @dead: the owner is terminating. */
static void hand_over(embk_mutex_t *m, embk_thread_t *from, bool dead)
{
    embk_thread_t *w;
    owned_remove(from, m);
    w = embk_wait_first(&m->waiters);
    if (w != NULL) {
        uintptr_t data = (dead || (m->obj.flags & EMBK_MUTEX_INCONSISTENT) != 0u)
                             ? EMBK_MUTEX_OWNERDEAD_DATA
                             : 0u;
        m->owner = w;
        m->count = 1u;
        owned_append(w, m);
        (void)embk_wait_wake(w, EMBK_WAKE_SATISFIED,
                             data); /* no barging (KRN-WAIT-012); the hook recomputes w */
        EMBK_TRACE(EMB_TRACE_MUTEX_UNLOCK, embk_thread_index(from), m, embk_thread_index(w));
    } else {
        m->owner = NULL;
        m->count = 0u;
        EMBK_TRACE(EMB_TRACE_MUTEX_UNLOCK, embk_thread_index(from), m, 0xFFu);
    }
    if (dead) {
        if (w == NULL) {
            m->obj.flags |= EMBK_MUTEX_INCONSISTENT;
        }
    } else {
        m->obj.flags &=
            (uint8_t)~EMBK_MUTEX_INCONSISTENT; /* cleared by the recovering owner's unlock */
    }
}

emb_status_t emb_mutex_unlock(emb_mutex_t mutex)
{
    embk_mutex_t *m = mutex_from_handle(mutex);
    embk_thread_t *self;
    emb_irq_key_t key;
    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(m != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    self = embk_cpu.current;
    key = embk_lock_object(m);
    if (m->owner != self) {
        embk_unlock_object(m, key);
        EMBK_MISUSE(EMB_FAULT_API_OWNER, EMB_EPERM, 1u); /* KRN-SYNC-008 */
    }
    if (m->count > 1u) {
        m->count--;
        embk_unlock_object(m, key);
        return EMB_OK;
    }
    hand_over(m, self, false);
    embk_prio_propagate(self); /* KRN-SYNC-010: over the still-owned list */
    embk_sched_reschedule_if_needed();
    embk_unlock_object(m, key);
    return EMB_OK;
}

bool emb_mutex_is_owner(emb_mutex_t mutex)
{
    const embk_mutex_t *m = mutex_from_handle(mutex);
    return m != NULL && embk_in_thread() && m->owner == embk_cpu.current;
}

emb_status_t emb_mutex_mark_consistent(emb_mutex_t mutex)
{
    embk_mutex_t *m = mutex_from_handle(mutex);
    emb_irq_key_t key;
    EMBK_REQUIRE_THREAD();
    EMBK_REQUIRE(m != NULL, EMB_FAULT_API_HANDLE, EMB_EINVAL, 1u);
    key = embk_lock_object(m);
    if (m->owner != embk_cpu.current) {
        embk_unlock_object(m, key);
        return EMB_EPERM;
    }
    m->obj.flags &= (uint8_t)~EMBK_MUTEX_INCONSISTENT;
    embk_unlock_object(m, key);
    return EMB_OK;
}

/* ---- owner termination (§3.6) ---------------------------------------------------------------- */

void embk_mutex_release_all_on_exit(embk_thread_t *t)
{
    if (t->owned == NULL) {
        return;
    }
#if CONFIG_EMB_MUTEX_OWNER_DEATH_FAULT
#if CONFIG_EMB_CHECKED
    embk_fault_raise(EMB_FAULT_API_LIFECYCLE, 2u, 0u, "thread exit with owned mutexes");
#else
    EMBK_TRACE(EMB_TRACE_FAULT, EMB_FAULT_API_LIFECYCLE, 2u,
               0u); /* recorded, not fatal, in release builds */
#endif
#endif
    /* bounded by the number of owned mutexes, acquisition order */
    while (t->owned != NULL) {
        hand_over(t->owned, t, true);
    }
    t->eff_prio = t->base_prio;
}

#endif /* CONFIG_EMB_MUTEX */
