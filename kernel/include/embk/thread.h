/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Thread lifecycle internals (SPEC-008). */
#ifndef EMBK_THREAD_H
#define EMBK_THREAD_H

#include <embk/kernel.h>

void embk_thread_init_subsystem(void);

/* Initialize @t as the idle context (SPEC-008 §11), lock not needed (prekernel). */
void embk_thread_init_idle(embk_thread_t *t, void *stack, size_t stack_size);

/* Deliver a pending cancel request to the current thread if enabled, lock held. */
bool embk_thread_take_cancel_locked(embk_thread_t *self);

#if CONFIG_EMB_THREAD_LIST
extern embk_list_t embk_thread_all; /* lock: scheduler domain */
#endif

#if CONFIG_EMB_NOTIFY
/* OR @bits into @t's word and wake it if satisfied; lock held; kernel bits allowed. */
void embk_notify_set_locked(embk_thread_t *t, emb_notify_bits_t bits);
#endif

#endif /* EMBK_THREAD_H */
