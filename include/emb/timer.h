/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Software timers (SPEC-003 §7): one-shot and drift-free periodic callbacks. */
#ifndef EMB_TIMER_H
#define EMB_TIMER_H

#include <emb/status.h>
#include <emb/time.h>
#include <emb/types.h>
#if CONFIG_EMB_WORKQ
#include <emb/work.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

EMB_DECLARE_HANDLE(emb_timer_t);

/* Defined by the generated <emb/storage.h>. */
typedef union emb_timer_storage emb_timer_storage_t;

typedef void (*emb_timer_fn_t)(emb_timer_t timer, void *arg);

/* What emb_timer_stop() found: nothing, an armed deadline or a queued callback, or a
 * callback that is running. */
typedef uint8_t emb_timer_state_t;
#define EMB_TIMER_IDLE    ((emb_timer_state_t)0u)
#define EMB_TIMER_PENDING ((emb_timer_state_t)1u)
#define EMB_TIMER_RUNNING ((emb_timer_state_t)2u)

/* The callback runs in the expiry interrupt instead of on a work queue (SPEC-003 §7.3). */
#define EMB_TIMER_ISR_CONTEXT ((uint8_t)0x01u)

typedef struct emb_timer_attr {
    const char *name;
    emb_timer_fn_t fn; /* required */
    void *arg;
#if CONFIG_EMB_WORKQ
    emb_workq_t workq; /* where the callback runs, e.g. emb_workq_system(); unused with
                          EMB_TIMER_ISR_CONTEXT */
#endif
    uint8_t flags;        /* EMB_TIMER_ISR_CONTEXT */
    uint8_t reserved_[3]; /* explicit padding */
} emb_timer_attr_t;

/**
 * emb_timer_attr_default() - Fill timer attributes with defaults.
 * @out_attr: Receives name, callback and argument NULL, no queue and flags 0, which init
 *            refuses until a callback and a queue or EMB_TIMER_ISR_CONTEXT are set; NULL
 *            is a no-op.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      API-020 KRN-TIM-025
 * @since    0.3
 * @stable   no
 */
void emb_timer_attr_default(emb_timer_attr_t *out_attr);

/**
 * emb_timer_init() - Construct a stopped timer in caller-provided storage.
 * @storage: Aligned caller-owned storage, never initialized or destroyed; retain it until
 *           successful destruction.
 * @attr: Required attributes, copied. Keep name storage alive.
 * @out_timer: Required output handle; cleared before argument validation.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1); O(active timers) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     borrows:storage
 * @config   CONFIG_EMB_TIMER
 * @req      API-021 KRN-TIM-025 KRN-TIM-005 KRN-TIM-013 KRN-OBJ-003 KRN-OBJ-007
 * @since    0.3
 * @stable   no
 *
 * A timer without EMB_TIMER_ISR_CONTEXT names the work queue its callback runs on; the
 * queue cannot be destroyed while the timer exists.
 *
 * Return: EMB_OK; EMB_EINVAL for bad storage, output or flags, a missing callback or a bad
 *         queue handle (misuse); EMB_ENOTSUP for a thread-context timer without a queue or
 *         without CONFIG_EMB_WORKQ; EMB_ENOMEM when the queue is named by 255 timers;
 *         EMB_EBUSY when @storage still holds a timer that was not destroyed (misuse).
 */
emb_status_t emb_timer_init(emb_timer_storage_t *storage, const emb_timer_attr_t *attr,
                            emb_timer_t *out_timer);

/**
 * emb_timer_destroy() - End a stopped timer (KRN-OBJ-001).
 * @timer: Initialized timer, not armed, with no callback queued or running.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     gives:storage
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-OBJ-001 KRN-TIM-028
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EBUSY (misuse) while armed, queued or running: emb_timer_stop_sync()
 *         first; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_timer_destroy(emb_timer_t timer);

/**
 * emb_timer_start() - Arm a one-shot expiry @first from now.
 * @timer: Initialized timer; a running one is re-armed and its counts kept.
 * @first: Delay; it saturates to EMB_TIMEOUT_MAX_TICKS, and 0 expires at the next pass.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(armed timers) insertion
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-025 KRN-TIM-006 KRN-IRQ-033
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_timer_start(emb_timer_t timer, emb_duration_t first);

/**
 * emb_timer_start_at() - Arm a one-shot expiry at an absolute instant.
 * @timer: Initialized timer.
 * @at:    Kernel-clock instant; a past one expires at the next pass.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(armed timers) insertion
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-025 KRN-TIM-011
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_timer_start_at(emb_timer_t timer, emb_instant_t at);

/**
 * emb_timer_start_periodic() - Arm @first from now, then every @period from each deadline.
 * @timer:  Initialized timer.
 * @first:  Delay to the first expiry.
 * @period: Nonzero period; it saturates to EMB_TIMEOUT_MAX_TICKS.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(armed timers) insertion
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-025 KRN-TIM-015 KRN-TIM-027
 * @since    0.3
 * @stable   no
 *
 * Expiry k falls at first + k * period from the start, whatever the callbacks' latency.
 * Periods missed entirely are skipped and counted as overruns (emb_timer_get_counts()).
 *
 * Return: EMB_OK; EMB_EINVAL for a zero period (misuse) or a bad handle.
 */
emb_status_t emb_timer_start_periodic(emb_timer_t timer, emb_duration_t first,
                                      emb_duration_t period);

/**
 * emb_timer_restart() - Re-arm with the last delay or period, from now.
 * @timer: Initialized timer that was started before.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(armed timers) insertion
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-025
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ESTATE when the timer was never started; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_timer_restart(emb_timer_t timer);

/**
 * emb_timer_stop() - Disarm and drop a queued callback; never waits.
 * @timer:     Initialized timer.
 * @out_state: Receives EMB_TIMER_RUNNING when a callback runs (it completes),
 *             EMB_TIMER_PENDING when an armed deadline or a queued callback was dropped,
 *             else EMB_TIMER_IDLE; may be NULL.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-025 KRN-TIM-028
 * @since    0.3
 * @stable   no
 *
 * From the timer's own callback it returns EMB_TIMER_RUNNING: the normal way for a
 * periodic timer to end itself.
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_timer_stop(emb_timer_t timer, emb_timer_state_t *out_state);

/**
 * emb_timer_stop_sync() - Stop, then wait until a running callback has returned.
 * @timer:   Initialized timer.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1); O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-028 KRN-WAIT-012
 * @since    0.3
 * @stable   no
 *
 * On EMB_OK the timer can be destroyed unless something started it again meanwhile.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT with the callback still running; EMB_ECANCELED; EMB_EPERM
 *         (misuse) from the timer's own callback (it would wait for itself) or outside
 *         thread context; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_timer_stop_sync(emb_timer_t timer, emb_timeout_t timeout);

/**
 * emb_timer_is_running() - Whether a deadline is armed; false for a bad handle.
 * @timer: Initialized timer.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-025
 * @since    0.3
 * @stable   no
 */
bool emb_timer_is_running(emb_timer_t timer);

/**
 * emb_timer_remaining() - Time to the armed deadline.
 * @timer:   Initialized timer.
 * @out_rem: Receives the remaining time; zero when not armed or already due.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-025
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle or a NULL @out_rem.
 */
emb_status_t emb_timer_remaining(emb_timer_t timer, emb_duration_t *out_rem);

/**
 * emb_timer_get_counts() - Expiries and overruns since init.
 * @timer:         Initialized timer.
 * @out_expiries:  Receives the expiries; may be NULL.
 * @out_overruns:  Receives missed periods plus callbacks coalesced into a queued one;
 *                 may be NULL. Both counts saturate.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TIMER
 * @req      KRN-TIM-027
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a bad handle.
 */
emb_status_t emb_timer_get_counts(emb_timer_t timer, uint32_t *out_expiries,
                                  uint32_t *out_overruns);

#ifdef __cplusplus
}
#endif

#endif /* EMB_TIMER_H */
