/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The wait and wake protocol (SPEC-004 §4). Internal interface; contexts per SPEC-001 §5. */
#ifndef EMBK_WAIT_H
#define EMBK_WAIT_H

#include <embk/kernel.h>

/* A deadline value that means "no deadline". */
#define EMBK_DEADLINE_FOREVER EMB_TICK_MAX

void embk_wait_queue_init(embk_wait_queue_t *q, uint8_t flags);
bool embk_wait_queue_is_empty(const embk_wait_queue_t *q);

/* Phase 1 (section 1): with the object lock held and the condition unsatisfied,
 * enqueue the current thread in @q by policy and mark it INTEND_TO_BLOCK. */
void embk_wait_prepare(embk_wait_queue_t *q, uint8_t reason);

/* Phases 2 and 3: with no lock held, arm the deadline (EMBK_DEADLINE_FOREVER for
 * none), commit to BLOCKED or observe an early wake, switch, and return the result
 * with the hand-off word in *data_out. */
embk_wait_result_t embk_wait_commit(emb_tick_t deadline, uintptr_t *data_out);

/* The common shape: releases @key after prepare, then commit. */
embk_wait_result_t embk_block_on(embk_wait_queue_t *q, uint8_t reason, emb_tick_t deadline,
                                 emb_irq_key_t key, uintptr_t *data_out);

/* Wakers, object lock held. */
embk_thread_t *embk_wait_first(const embk_wait_queue_t *q);
bool embk_wait_wake(embk_thread_t *t, embk_wait_result_t result, uintptr_t data);
unsigned embk_wait_wake_all(embk_wait_queue_t *q, embk_wait_result_t result, uintptr_t data);
void embk_wait_requeue(embk_thread_t *t);

/* Timeout expiry (SPEC-004 §5.3), lock held. */
void embk_wait_wake_timeout(embk_thread_t *t);

/* Cancellation (§6.6), lock held: wakes @t with CANCELED if it waits with a cancelable
 * reason and cancellation is enabled; returns whether it did. */
bool embk_wait_cancel(embk_thread_t *t);

/* Destruction (§6.7), lock held: every waiter leaves with @result. */
unsigned embk_wait_flush(embk_wait_queue_t *q, embk_wait_result_t result);

/* Map a wake result to the public status (SPEC-004 §2.3). */
emb_status_t embk_wait_result_status(embk_wait_result_t r);

/* Deadline helpers (SPEC-003 §5.2). */
emb_tick_t embk_deadline_from_timeout(emb_timeout_t timeout);
bool embk_tick_before(emb_tick_t a, emb_tick_t b);
bool embk_deadline_passed(emb_tick_t deadline);

#endif /* EMBK_WAIT_H */
