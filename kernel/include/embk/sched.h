/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Scheduler internals (03 §2, SPEC-002 §5, §6, ADR-036). Two ready structures behind
 * one interface: per-priority FIFO lists under a bitmap (base), or the table of one
 * thread per priority with a one-word ready set (tiny). The running thread is never
 * in the ready structure; the idle context never enters it.
 *
 * Every function expects the scheduler lock domain held (@lock sched) unless noted.
 */
#ifndef EMBK_SCHED_H
#define EMBK_SCHED_H

#include <embk/kernel.h>

extern embk_thread_t embk_idle_thread; /* the idle context; prio 0; never READY in the structure */

#if CONFIG_EMB_SCHED_TABLE
extern embk_thread_t *embk_thread_table[CONFIG_EMB_PRIORITY_COUNT]; /* slot = priority (ADR-036) */
#endif

void embk_sched_init(void);

/* Insert @t into the ready structure, behind its peers, or ahead of them when
 * @ahead (KRN-SCH-041). Sets reschedule_pending when @t should preempt. */
void embk_sched_make_ready(embk_thread_t *t, bool ahead);

/* Remove @t from the ready structure; @t must be in it. */
void embk_sched_remove_ready(embk_thread_t *t);

/* Is @t in the ready structure? */
bool embk_sched_is_ready(const embk_thread_t *t);

/* Highest-priority READY thread, or NULL. The structure is not changed. */
embk_thread_t *embk_sched_peek(void);

/* The scheduling decision of a preemption point: updates embk_cpu.current and the
 * ready structure, emits the switch trace, and returns the thread to switch to, or
 * NULL when the current thread keeps running. @yield: the current thread goes behind
 * its peers instead of ahead of them. Clears reschedule_pending. */
embk_thread_t *embk_sched_select(bool yield);

/* P2 (SPEC-002 §6.3): when a reschedule is pending, the scheduler is not locked, and
 * the caller holds the critical section, select and switch; returns when the caller
 * runs again. */
void embk_sched_reschedule_if_needed(void);

/* Block path: the current thread is no longer runnable; switch away now regardless
 * of reschedule_pending (the scheduler lock cannot be held here). */
void embk_sched_switch_away(void);

/* The last switch of a terminating thread (SPEC-008 §5 step 4). */
EMB_NORETURN void embk_sched_switch_final(void);

/* Yield: current behind its peers if one exists (KRN-SCH-008). */
void embk_sched_yield_locked(void);

/* Is the current thread runnable (not blocked, suspended, or terminated)? */
bool embk_sched_current_runnable(void);

/* @t's effective priority changed from @old_eff to t->eff_prio: reposition it (SPEC-005 §2.1). */
void embk_sched_prio_changed(embk_thread_t *t, uint8_t old_eff);

#if CONFIG_EMB_SCHED_TABLE
extern uint8_t embk_sched_raised_count; /* threads with eff_prio != base_prio; lock: sched */
#endif

/* A thread became runnable at priority @prio: should it preempt the current thread? */
bool embk_sched_should_preempt(uint8_t prio);

#endif /* EMBK_SCHED_H */
