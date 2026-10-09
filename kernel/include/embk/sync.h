/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Semaphore and mutex objects and the effective-priority machinery (SPEC-005). */
#ifndef EMBK_SYNC_H
#define EMBK_SYNC_H

#include <emb/sem.h>

#include <embk/kernel.h>

#if CONFIG_EMB_SEM
typedef struct embk_sem {
    embk_obj_t obj; /* obj.flags: EMB_SEM_SATURATE | EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
    embk_wait_queue_t waiters;
    emb_sem_count_t count;
    emb_sem_count_t max;
#if CONFIG_EMB_NOTIFY
    embk_thread_t *bind_thread; /* ADR-027: NULL when unbound */
    emb_notify_bits_t bind_bit;
#endif
} embk_sem_t;
#endif

#if CONFIG_EMB_MUTEX
#define EMBK_MUTEX_INCONSISTENT   ((uint8_t)0x02u) /* obj.flags bit: owner died (SPEC-005 §3.6) */
#define EMBK_MUTEX_OWNERDEAD_DATA ((uintptr_t)1u)  /* hand-off word flag */

typedef struct embk_mutex {
    embk_obj_t
        obj; /* obj.flags: EMB_MUTEX_RECURSIVE | EMBK_MUTEX_INCONSISTENT | EMB_OBJ_ABORT_WAITERS */
    embk_wait_queue_t waiters;
    embk_thread_t *owner;
    struct embk_mutex *owned_next; /* next in the owner's list, acquisition order */
    uint8_t protocol;
    uint8_t ceiling;
    uint8_t count; /* recursion count, 0 when free */
    uint8_t reserved_;
} embk_mutex_t;

/* Recompute and apply @t's effective priority (SPEC-005 §2.1); returns whether it changed. */
bool embk_prio_recompute(embk_thread_t *t);

/* The walk of SPEC-005 §2.2 from @t along the owner chain; lock held. */
void embk_prio_propagate(embk_thread_t *t);

/* Called by the wait protocol after a dequeue or requeue on an INHERIT queue. */
void embk_mutex_on_waiters_changed(embk_mutex_t *m);

/* Thread exit (SPEC-005 §3.6, SPEC-008 §5 step 2), lock held. */
void embk_mutex_release_all_on_exit(embk_thread_t *t);
#else
static EMB_INLINE bool embk_prio_recompute(embk_thread_t *t)
{
    bool changed = t->eff_prio != t->base_prio;
    t->eff_prio = t->base_prio;
    return changed;
}
static EMB_INLINE void embk_prio_propagate(embk_thread_t *t)
{
    (void)embk_prio_recompute(t);
}
#endif

#endif /* EMBK_SYNC_H */
