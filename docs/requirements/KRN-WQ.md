# KRN-WQ - Work Queues and Delayed Work

Group `KRN-WQ`. Design: `docs/specs/SPEC-006-notifications-and-work-queues.md` §4, §5. Related groups: KRN-NOTIF (the `K_WORK` bit), KRN-TIM (delayed work is a timer; timer callbacks are work items), KRN-THR (the queue's thread), KRN-SYNC (mutex ownership across handlers).

KRN-WQ-001 to 004 originate in `docs/architecture/03-kernel-architecture.md` §6.2. All are restated here as the authoritative copy. New requirements start at 005.

---

## Requirements

### KRN-WQ-001  Intrusive, static, ISR-safe submit
**Statement.** Work items shall be intrusive and statically allocatable; submission shall be ISR-safe and O(1).
**Rationale.** A bottom half is submitted from interrupts; it must cost one push and one bit set.
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08).
**Verification.** Test: `workq_submit_from_isr_runs_at_exit`, `workq_runs_items_in_fifo_order_on_its_thread`; Analysis: no allocation and no list walk in `emb_work_submit`; model scenario `work_isr_submit`.
**Trace.** SPEC-006 §4.3

### KRN-WQ-002  FIFO per queue in thread context
**Statement.** Work executes in FIFO order per queue in thread context.
**Rationale.** Deterministic order; handlers may use thread-only API.
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08).
**Verification.** Test: `workq_runs_items_in_fifo_order_on_its_thread`, `workq_cancel_reports_pending_running_and_idle` (a handler that sleeps); model scenario `work_fifo_and_idempotent_submit`; differential agreement on the native presets.
**Trace.** SPEC-006 §4.3

### KRN-WQ-003  Delayed work over software timers
**Statement.** Delayed work shall be implemented over software timers, not a separate timing mechanism.
**Rationale.** One timeout structure (ADR-010); one set of accuracy rules.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `emb_dwork_t` embeds `emb_timer_storage_t`; no other timing code in `kernel/work/`.
**Trace.** SPEC-006 §4.1, §4.3; SPEC-003 §7

### KRN-WQ-004  Cancellation reports the state
**Statement.** Cancellation shall report whether the item was pending, running, or idle.
**Rationale.** The caller must know whether a handler may still run.
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08).
**Verification.** Test: `workq_cancel_reports_pending_running_and_idle`; model scenario `work_cancel_states`.
**Trace.** SPEC-006 §4.3

### KRN-WQ-005  Submit outcomes
**Statement.** Submitting a queued item shall return `EMB_EEXIST` and change nothing; submitting an item running on the same queue shall return `EMB_OK` and re-append it after its handler returns; submitting an item running on another queue shall return `EMB_EBUSY`; an item shall never run concurrently with itself.
**Rationale.** Idempotent submission without lost re-submissions and without handler re-entrancy (Zephyr's v2 rule, R-001 §6).
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08).
**Verification.** Test: `workq_runs_items_in_fifo_order_on_its_thread` (EEXIST), `workq_resubmit_while_running_runs_again`; `workq_item_belongs_to_one_queue_at_a_time` (EBUSY); model scenarios `work_fifo_and_idempotent_submit`, `work_resubmit_while_running` and the item-state invariants.
**Trace.** SPEC-006 §4.3, §12.5

### KRN-WQ-006  Synchronous cancel and flush
**Statement.** `cancel_sync` shall wait until a running handler has returned; `flush` shall return when every item queued before the call has run; calling either from the queue's own thread shall be misuse.
**Rationale.** Teardown needs a point after which no handler touches the caller's state.
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08).
**Verification.** Test: `workq_cancel_reports_pending_running_and_idle`, `workq_flush_waits_for_items_queued_before_it`; model scenarios `work_flush_and_timeout`, `work_flush_waits_for_running_handler`; the `differential_timer_regressions` flush input. Misuse from the queue's own thread is checked by the model and by inspection.
**Trace.** SPEC-006 §4.3, §6

### KRN-WQ-007  Delayed work scheduling
**Statement.** `dwork_schedule` on a pending item shall return `EMB_EEXIST` and change nothing; `reschedule` shall replace a pending delay; cancel shall stop an armed timer or remove a queued item and report `PENDING` for both; the timer's expiry shall submit the item in O(1) from the expiry path.
**Rationale.** One queue hop per delayed item; deterministic re-arm semantics.
**Status.** Accepted 2026-10-07
**Verification.** Test: `wq/dwork_schedule_pending`, `wq/dwork_reschedule`, `wq/dwork_cancel_states`.
**Trace.** SPEC-006 §4.3, §12.6, §12.7

### KRN-WQ-008  System work queue
**Statement.** With `CONFIG_EMB_SYSTEM_WORKQ` the system work queue shall exist from kernel start at `CONFIG_EMB_SYSTEM_WORKQ_PRIO` with `CONFIG_EMB_SYSTEM_WORKQ_STACK`; it shall be the default queue of software timers; the tiny profile shall default to no system queue.
**Rationale.** Timer callbacks need a thread; the tiny profile cannot afford one by default.
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08); the queue is linked only when referenced (ADR-048, A50).
**Verification.** Test: `workq_system_queue_runs_items`; footprint of the M1 images, which do not reference it, is unchanged.
**Trace.** SPEC-006 §4.3, §8; SPEC-003 §7.3; ADR-019

### KRN-WQ-009  Work queue thread
**Statement.** A work queue's thread shall wait on the `K_WORK` notification bit, drain the FIFO one item at a time with the item marked `RUNNING` outside the critical section, and run handlers at the queue's priority.
**Rationale.** Fixes the implementation shape so that latency is the thread's dispatch latency plus the handlers ahead.
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08).
**Verification.** Analysis: `kernel/work/workq.c` review; Test: `workq_thread_is_a_kernel_thread`; the model's queue thread follows the kernel's trace in the differential bridge.
**Trace.** SPEC-006 §4.1, §4.3, §4.4

### KRN-WQ-010  Lifecycle misuse
**Statement.** Destroying a queue with queued or running items, flushing or synchronously cancelling from the queue's own thread, and returning from a handler while owning a mutex shall be misuse per SPEC-001 §5.3.
**Rationale.** Each would leave work or locks in an undefined owner.
**Status.** Accepted 2026-10-07; implemented 2026-10-27 (M2-08) for destroy, flush and cancel_sync misuse and a handler returning with a mutex owned.
**Verification.** Test: `workq_destroy_refuses_a_busy_queue`; model scenarios; the owned-mutex check by inspection.
**Trace.** SPEC-006 §6

### KRN-WQ-011  Trace and statistics
**Statement.** Submit, start with submit-to-start latency, end with duration, cancel, and delayed scheduling shall be trace events; maximum FIFO length, worst latency, and longest handler with its address shall be optional statistics per queue.
**Rationale.** Work queue latency is the latency of every timer callback and bottom half.
**Status.** Accepted 2026-10-07; trace records implemented (A52); statistics are not.
**Verification.** Test: trace decoder (not written); statistics API test (no API).
**Trace.** SPEC-006 §7; OBS-005
