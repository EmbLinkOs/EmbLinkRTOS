/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Notification bindings of ready-state objects (SPEC-006 §3, ADR-027, ADR-046). An object
 * names one (thread, bit) pair; the thread counts the bindings that name it, and cannot be
 * destroyed while any does, so a binding never outlives its target. Lock: the object's,
 * which is the one critical section on a uniprocessor.
 */
#ifndef EMBK_BINDING_H
#define EMBK_BINDING_H

#include <embk/kernel.h>
#include <embk/thread.h>

#if CONFIG_EMB_NOTIFY
typedef struct embk_binding {
    embk_thread_t *thread; /* NULL when unbound */
    emb_notify_bits_t bit; /* one application bit (A23) */
} embk_binding_t;

static EMB_INLINE void embk_binding_init(embk_binding_t *b)
{
    b->thread = NULL;
    b->bit = 0u;
}

/* Bind @t's @bit, already validated, lock held. EMB_EEXIST when bound; EMB_ESTATE for a
 * terminated target; EMB_EPERM for a kernel thread; EMB_ENOMEM when @t's count is full. */
emb_status_t embk_binding_bind(embk_binding_t *b, embk_thread_t *t, emb_notify_bits_t bit);

/* Remove the binding and release its target, lock held; false when there was none. */
bool embk_binding_unbind(embk_binding_t *b);

/* @obj became ready for its bound operation without a hand-off, lock held. */
static EMB_INLINE void embk_binding_signal(const embk_binding_t *b, const void *obj)
{
    if (b->thread != NULL) {
#if CONFIG_EMB_TRACE
        embk_notify_set_bound(b->thread, b->bit, obj);
#else
        (void)obj;
        embk_notify_set_locked(b->thread, b->bit);
#endif
    }
}
#endif /* CONFIG_EMB_NOTIFY */

#endif /* EMBK_BINDING_H */
