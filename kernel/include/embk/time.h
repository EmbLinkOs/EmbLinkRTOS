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
void embk_timeout_arm(embk_timeout_t *node, emb_tick_t deadline, embk_wait_gen_t gen);
void embk_timeout_disarm(embk_timeout_t *node);
bool embk_timeout_is_armed(const embk_timeout_t *node);
bool embk_timeout_next(emb_tick_t *out_deadline);

/* Tickless: reprogram the hardware deadline from the next timeout, lock held. */
void embk_time_program(void);

#endif /* EMBK_TIME_H */
