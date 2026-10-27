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

#if CONFIG_EMB_WORKQ
/* Construct and start @t as a kernel thread: no handle operation reaches it, it is never
 * canceled, and only the kernel ends it. The application thread checks apply; faults
 * name @api_id. */
emb_status_t embk_thread_init_kernel(uint16_t api_id, embk_thread_t *t,
                                     const emb_thread_attr_t *attr, emb_thread_entry_t entry,
                                     void *arg);
/* The current kernel thread terminates with code 0 (SPEC-008 §5); never returns. */
EMB_NORETURN void embk_thread_exit_kernel(void);
/* Release a kernel thread after its joiner woke; lock not held. */
void embk_thread_destroy_kernel(embk_thread_t *t);
#endif

#if CONFIG_EMB_NOTIFY
/* OR @bits into @t's word and wake it if satisfied; lock held; kernel bits allowed. */
void embk_notify_set_locked(embk_thread_t *t, emb_notify_bits_t bits);
#if CONFIG_EMB_TRACE
/* The same for a binding: the trace record names the bound object @obj (SPEC-006 §7). */
void embk_notify_set_bound(embk_thread_t *t, emb_notify_bits_t bits, const void *obj);
#endif
#endif

#endif /* EMBK_THREAD_H */
