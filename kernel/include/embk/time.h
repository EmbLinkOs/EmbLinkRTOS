/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Clock, timeout list, and timer hooks (SPEC-003). Lock: the timeout domain. */
#ifndef EMBK_TIME_H
#define EMBK_TIME_H

#include <embk/kernel.h>

void embk_time_init(void);
void embk_time_start(
    void); /* at kernel start: start the periodic tick or program the first deadline */

/* The clock, lock held; emb_time_now() is the lock-free public read. */
emb_tick_t embk_time_ticks_locked(void);

/* Timeout list (SPEC-003 §5.1), lock held. */
void embk_timeout_arm(embk_timeout_t *node, emb_tick_t deadline);
void embk_timeout_disarm(embk_timeout_t *node);
bool embk_timeout_is_armed(const embk_timeout_t *node);
bool embk_timeout_next(emb_tick_t *out_deadline);

/* Tickless: reprogram the hardware deadline from the next timeout, lock held. */
void embk_time_program(void);

#if CONFIG_EMB_TIME_SLICING
/* At dispatch, lock held: start the quantum of @t, or stop it for the idle context and
 * levels above CONFIG_EMB_TIMESLICE_PRIORITY (SPEC-003 §8). */
void embk_time_slice_start(const embk_thread_t *t);
#endif

#if CONFIG_EMB_TIMER
/* The timer list (ADR-047), lock held: thread timeouts and timers expire together in
 * deadline order, threads first on equal deadlines. The arm records the current pass. */
void embk_timer_node_arm(embk_timeout_t *node, emb_tick_t deadline);
void embk_timer_node_disarm(embk_timeout_t *node);
/* The expiry pass in progress, or the last one; a node armed in it waits for the next. */
uint8_t embk_time_pass(void);
#endif

#endif /* EMBK_TIME_H */
