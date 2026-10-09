# KRN-WQ - Work Queues and Delayed Work

Group `KRN-WQ`. Design: `docs/specs/SPEC-006-notifications-and-work-queues.md` §4, §5. Related groups: KRN-NOTIF (the `K_WORK` bit), KRN-TIM (delayed work is a timer; timer callbacks are work items), KRN-THR (the queue's thread), KRN-SYNC (mutex ownership across handlers).

KRN-WQ-001 to 004 originate in `docs/architecture/03-kernel-architecture.md` §6.2. All are restated here as the authoritative copy. New requirements start at 005.

---

### KRN-WQ-001  Intrusive, static, ISR-safe submit
**Statement.** Work items shall be intrusive and statically allocatable; submission shall be ISR-safe and O(1).
**Rationale.** A bottom half is submitted from interrupts; it must cost one push and one bit set.
**Status.** Accepted 2026-10-07
**Verification.** Test: `wq/submit_from_isr`; Analysis: no allocation and no list walk in `emb_work_submit`.
**Trace.** SPEC-006 §4.3

### KRN-WQ-002  FIFO per queue in thread context
**Statement.** Work executes in FIFO order per queue in thread context.
**Rationale.** Deterministic order; handlers may use thread-only API.
**Status.** Accepted 2026-10-07
**Verification.** Test: `wq/fifo_order`, `wq/handler_may_block`.
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
**Status.** Accepted 2026-10-07
**Verification.** Test: `wq/cancel_pending`, `wq/cancel_running`, `wq/cancel_idle`.
**Trace.** SPEC-006 §4.3

### KRN-WQ-005  Submit outcomes
**Statement.** Submitting a queued item shall return `EMB_EEXIST` and change nothing; submitting an item running on the same queue shall return `EMB_OK` and re-append it after its handler returns; submitting an item running on another queue shall return `EMB_EBUSY`; an item shall never run concurrently with itself.
**Rationale.** Idempotent submission without lost re-submissions and without handler re-entrancy (Zephyr's v2 rule, R-001 §6).
**Status.** Accepted 2026-10-07
**Verification.** Test: `wq/submit_queued_eexist`, `wq/resubmit_while_running_runs_again`, `wq/submit_other_queue_ebusy`.
**Trace.** SPEC-006 §4.3, §12.5

### KRN-WQ-006  Synchronous cancel and flush
**Statement.** `cancel_sync` shall wait until a running handler has returned; `flush` shall return when every item queued before the call has run; calling either from the queue's own thread shall be misuse.
**Rationale.** Teardown needs a point after which no handler touches the caller's state.
**Status.** Accepted 2026-10-07
**Verification.** Test: `wq/cancel_sync_waits`, `wq/flush`, `misuse/wq_flush_from_handler`.
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
**Status.** Accepted 2026-10-07
**Verification.** Test: `wq/system_queue_exists`; footprint test of the tiny profile without it.
**Trace.** SPEC-006 §4.3, §8; SPEC-003 §7.3; ADR-019

### KRN-WQ-009  Work queue thread
**Statement.** A work queue's thread shall wait on the `K_WORK` notification bit, drain the FIFO one item at a time with the item marked `RUNNING` outside the critical section, and run handlers at the queue's priority.
**Rationale.** Fixes the implementation shape so that latency is the thread's dispatch latency plus the handlers ahead.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `kernel/work/` review; Test: `wq/latency_measured` with the trace events.
**Trace.** SPEC-006 §4.1, §4.3, §4.4

### KRN-WQ-010  Lifecycle misuse
**Statement.** Destroying a queue with queued or running items, flushing or synchronously cancelling from the queue's own thread, and returning from a handler while owning a mutex shall be misuse per SPEC-001 §5.3.
**Rationale.** Each would leave work or locks in an undefined owner.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/wq_*` in checked and release configurations (TEST-010).
**Trace.** SPEC-006 §6

### KRN-WQ-011  Trace and statistics
**Statement.** Submit, start with submit-to-start latency, end with duration, cancel, and delayed scheduling shall be trace events; maximum FIFO length, worst latency, and longest handler with its address shall be optional statistics per queue.
**Rationale.** Work queue latency is the latency of every timer callback and bottom half.
**Status.** Accepted 2026-10-07
**Verification.** Test: trace decoder; statistics API test.
**Trace.** SPEC-006 §7; OBS-005
