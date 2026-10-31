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

/* The idle context: priority 0, never READY in the structure, not an application thread
 * (SPEC-008 §11). With the table scheduler and no idle thread it is slim: it never
 * blocks, times out, takes notifications, joins or changes priority, so only the control
 * block's prefix that the scheduler, the switch path and diagnostics read (arch through
 * lock_depth) is allocated, by sched/idle_context.c; nothing reads past it (ADR-050). */
#define EMBK_IDLE_SLIM (CONFIG_EMB_SCHED_TABLE && !CONFIG_EMB_IDLE_THREAD)
extern embk_thread_t embk_idle_thread;
#define EMBK_IDLE (&embk_idle_thread)

#if CONFIG_EMB_SCHED_TABLE
/* The thread of priority p is in slot p - 1 (ADR-036); idle, at 0, has none. */
extern embk_thread_t *embk_thread_table[CONFIG_EMB_PRIORITY_COUNT - 1];
#define EMBK_TABLE_SLOT(p) embk_thread_table[(unsigned)(p) - 1u]
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
#if CONFIG_EMB_SCHED_TABLE
/* The highest effective priority among the table slots in @set (the slot order is the
 * base-priority order; raised threads are scanned when any exists). NULL when empty. */
embk_thread_t *embk_sched_table_best(embk_word_t set);
#endif

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
#if CONFIG_EMB_MUTEX
extern uint8_t embk_sched_raised_count; /* threads with eff_prio != base_prio; lock: sched */
#endif
#endif

#if CONFIG_EMB_TIME_SLICING
/* The current thread's quantum expired, timer interrupt: with an equal-priority peer READY,
 * the next decision moves it behind its peers (KRN-SCH-026). */
void embk_sched_slice_expired(void);
#endif

/* A thread became runnable at priority @prio: should it preempt the current thread? */
bool embk_sched_should_preempt(uint8_t prio);

#endif /* EMBK_SCHED_H */
