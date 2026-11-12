/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The wall clock (SPEC-003 §10, KRN-TIM-031, A123): calendar time as nanoseconds since the Unix
 * epoch (UTC), the kernel's monotonic clock plus an offset. Setting or adjusting it changes the
 * offset only: no timeout, timer or deadline moves, and nothing in the kernel reads it. A
 * board's RTC seeds it at boot when its time is set (SPEC-017 §10). */
#ifndef EMB_WALLCLOCK_H
#define EMB_WALLCLOCK_H

#include <emb/status.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * emb_wallclock_get() - The wall clock's time.
 * @out_ns_since_epoch: Receives nanoseconds since 1970-01-01T00:00:00Z.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WALLCLOCK
 * @req      KRN-TIM-031
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOTSET (never set since boot, nor seeded by an RTC).
 */
emb_status_t emb_wallclock_get(int64_t *out_ns_since_epoch);

/**
 * emb_wallclock_set() - Set the wall clock: its offset from the monotonic clock.
 * @ns_since_epoch: The time now, in nanoseconds since 1970-01-01T00:00:00Z.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WALLCLOCK
 * @req      KRN-TIM-031
 * @since    0.3
 * @stable   no
 *
 * No deadline moves: timeouts and timers count the monotonic clock. The RTC is not set by this:
 * an application that learns the time sets both.
 *
 * Return: EMB_OK; EMB_EINVAL (a negative time: before the epoch).
 */
emb_status_t emb_wallclock_set(int64_t ns_since_epoch);

/**
 * emb_wallclock_adjust() - Step the wall clock by a correction.
 * @delta_ns: Added to its time, either sign.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WALLCLOCK
 * @req      KRN-TIM-031
 * @since    0.3
 * @stable   no
 *
 * A step: the time jumps by @delta_ns at once (slewing is not in M4, A123). No deadline moves.
 *
 * Return: EMB_OK; EMB_ENOTSET (never set); EMB_EINVAL (it would go before the epoch).
 */
emb_status_t emb_wallclock_adjust(int64_t delta_ns);

#ifdef __cplusplus
}
#endif

#endif /* EMB_WALLCLOCK_H */
