/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Scheduler lock and priority constants (SPEC-002 §5, SPEC-008 §1). */
#ifndef EMB_SCHED_H
#define EMB_SCHED_H

#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_PRIORITY_COUNT CONFIG_EMB_PRIORITY_COUNT
#define EMB_PRIORITY_IDLE  0u
#define EMB_PRIORITY_MIN   1u
#define EMB_PRIORITY_MAX   ((uint8_t)(CONFIG_EMB_PRIORITY_COUNT - 1))

/**
 * emb_sched_lock() - Prevent thread preemption until the matching unlock.
 *
 * Interrupts still run; a wake they cause is applied when the lock depth returns
 * to zero (KRN-SCH-016, KRN-SCH-018). Blocking with a nonzero timeout while locked
 * is misuse (SPEC-001 §5.3).
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-SCH-016 KRN-SCH-017 KRN-SCH-019
 * @since    0.2
 * @stable   yes
 */
void emb_sched_lock(void);

/**
 * emb_sched_unlock() - Undo one emb_sched_lock(); at depth zero, reschedule if owed.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1), plus one reschedule at depth 0
 * @owns     none
 * @config
 * @req      KRN-SCH-017 KRN-SCH-018
 * @since    0.2
 * @stable   yes
 */
void emb_sched_unlock(void);

/**
 * emb_sched_lock_depth() - Current scheduler lock nesting depth.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-SCH-017
 * @since    0.2
 * @stable   yes
 */
unsigned emb_sched_lock_depth(void);

#ifdef __cplusplus
}
#endif

#endif /* EMB_SCHED_H */
