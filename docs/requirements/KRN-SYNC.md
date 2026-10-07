# KRN-SYNC - Synchronization

Group `KRN-SYNC`. Design: `docs/specs/SPEC-005-synchronization.md`. Related groups: KRN-WAIT (the protocol every primitive here uses), KRN-SCH (effective priority in the ready structure), KRN-THR (priority change, termination), KRN-TP (demotion versus inheritance), KRN-NOTIF (binding), KRN-SMP (spinlocks), KRN-OBJ (destroy).

KRN-SYNC-001 to 007 originate in the v0.1 specification (§8.6); KRN-SYNC-008 to 017 in `docs/architecture/03-kernel-architecture.md` §5.2 (014 to 017 added by ADR-028 and ADR-030). All are restated here as the authoritative copy. New requirements start at 018.

---

### KRN-SYNC-001  Common wait mechanism
**Statement.** Blocking synchronization shall use the common kernel wait mechanism.
**Rationale.** One protocol under every primitive (SPEC-004) means one set of race rules, one model, one set of tests.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: every blocking entry in `kernel/sync/` calls `embk_wait_prepare` and `embk_wait_commit` or `embk_block_on`; a grep in CI rejects any other path to `BLOCKED`.
**Trace.** SPEC-005 §1; SPEC-004; v0.1 §8.6

### KRN-SYNC-002  Explicit, validated ownership
**Statement.** Mutex ownership shall be explicit and validated where configuration permits.
**Rationale.** A mutex is not a semaphore; unlock by a non-owner is the classic silent corruption.
**Status.** Proposed 2026-10-07
**Verification.** Test: `misuse/mutex_unlock_non_owner` in checked and release builds (TEST-010); `sync/mutex_is_owner`.
**Trace.** SPEC-005 §3.1, §3.3; KRN-SYNC-008

### KRN-SYNC-003  Priority inheritance available
**Statement.** The mutex subsystem shall support priority inheritance for configurations that enable real-time mutexes.
**Rationale.** Priority inversion is a first-class real-time problem (v0.1 §8.3).
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/pi_classic_inversion` checks that the medium-priority thread never runs while the high one waits on the low one's mutex; model scenario `classic_inversion`.
**Trace.** SPEC-005 §2, §3.2; ADR-028

### KRN-SYNC-004  Nested dependency chains
**Statement.** Priority inheritance shall support nested dependency chains.
**Rationale.** H waits for A held by M, M waits for B held by L: H's priority must reach L (v0.1 §8.3).
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/pi_nested_chain` to depth 3; model scenario `nested_chain_timeout`.
**Trace.** SPEC-005 §2.2; KRN-SYNC-009, 019

### KRN-SYNC-005  ISR-safe signaling is distinct
**Statement.** ISR-safe signaling operations shall be distinct from blocking thread-only operations.
**Rationale.** The ISR-safe path has no ownership and no inheritance; the annotation is the contract (SPEC-001 §5.2).
**Status.** Proposed 2026-10-07
**Verification.** Analysis: `tools/apidoc` checks that `give`, `set`, `clear` carry `@ctx thread isr` and every `lock`, `take`, `wait` carries `@ctx thread`.
**Trace.** SPEC-005 §1; SPEC-001 §5.2; KRN-IRQ-010

### KRN-SYNC-006  Deterministic wait ordering
**Statement.** Synchronization object wait ordering shall be documented and deterministic.
**Rationale.** Schedulability analysis needs to know who wakes first.
**Status.** Proposed 2026-10-07
**Verification.** Test: the `wait/order_*` group runs against every primitive of SPEC-005.
**Trace.** SPEC-005 §1; KRN-WAIT-001, 002

### KRN-SYNC-007  No hidden allocation
**Statement.** No synchronization primitive used on a real-time path shall require hidden dynamic allocation after initialization.
**Rationale.** Determinism and the tiny profile.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: no allocator symbol is reachable from `kernel/sync/` (link-map check in CI).
**Trace.** SPEC-005 §1; KRN-WAIT-010; KRN-MEM

### KRN-SYNC-008  Unlock by a non-owner
**Statement.** Unlock by a non-owner shall be a kernel fault in checked builds and `EMB_EPERM` in release builds.
**Rationale.** Misuse, per SPEC-001 §5.3.
**Status.** Proposed 2026-10-07
**Verification.** Test: `misuse/mutex_unlock_non_owner`.
**Trace.** SPEC-005 §3.3, §11

### KRN-SYNC-009  Transitive inheritance with a depth bound
**Statement.** Priority inheritance shall be transitive through chains of mutexes with a configurable maximum depth; exceeding it is a kernel fault in checked builds.
**Rationale.** Unbounded walks are a latency hazard; the bound is a design parameter.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/pi_depth_bound` builds a chain of `CONFIG_EMB_PI_MAX_DEPTH + 1` and checks the fault (checked) and the truncation trace event (release).
**Trace.** SPEC-005 §2.2; ADR-028

### KRN-SYNC-010  Recomputation on unlock
**Statement.** On unlock, the owner's effective priority shall be recomputed as the maximum of its base priority and the highest priority waiter across all mutexes it still owns.
**Rationale.** The one-mutex simplification of other kernels (R-001 §5) is wrong as soon as two mutexes are held.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/pi_two_mutexes_restore`; model invariant `eff == effective(base, owned)`.
**Trace.** SPEC-005 §2.1, §3.3

### KRN-SYNC-011  Owner termination
**Statement.** When a mutex owner terminates while owning mutexes, the behavior is a configuration choice: fault (default) or release-with-`EMB_EOWNERDEAD` delivered to the next owner.
**Rationale.** Both are legitimate: a fault for systems where it must never happen, recovery for systems that restart partitions.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/owner_death_fault`, `sync/owner_death_release_with_waiter`, `sync/owner_death_release_no_waiter`; model scenarios `owner_death_*`.
**Trace.** SPEC-005 §3.6; KRN-SYNC-025

### KRN-SYNC-012  Ceiling protocol on the same object type
**Statement.** Priority ceiling protocol shall be available as a per-mutex option on the same object type, using the effective-priority mechanism.
**Rationale.** One mutex type, two protocols; the effective-priority machinery already exists.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/ceiling_lock_raises`, `sync/ceiling_unlock_restores`; model scenario `ceiling_mutex`.
**Trace.** SPEC-005 §3.4; ADR-030

### KRN-SYNC-013  Recursive locking is an option
**Statement.** Recursive locking shall be a per-mutex creation option, disabled by default.
**Rationale.** Recursion hides locking-discipline errors; it is offered, not default.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/mutex_recursive`, `sync/mutex_nonrecursive_self_relock`.
**Trace.** SPEC-005 §3.2; KRN-SYNC-023

### KRN-SYNC-014  Owned-mutex list and immediate recomputation
**Statement.** Each thread shall keep an intrusive list of the mutexes it owns; the recomputation of KRN-SYNC-010 shall use it and shall run at unlock, at waiter timeout or cancel, and at waiter priority change, immediately and not deferred to the waiter's resumption.
**Rationale.** Deferred drops leave a thread boosted for an unbounded time (Zephyr's documented limitation, R-001 §5).
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/pi_timeout_disinherits_immediately` checks the owner's priority in the same tick as the timeout; model scenario `nested_chain_timeout`.
**Trace.** SPEC-005 §2.3, §3.3; ADR-028

### KRN-SYNC-015  Deadlock detection
**Statement.** The inheritance walk shall detect a cycle that returns to the caller and shall report `EMB_EDEADLK` (kernel-reserved status range) in release builds and fault in checked builds.
**Rationale.** The walk visits the chain anyway; detection is free.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/deadlock_two_lockers`; model scenario `deadlock_two_lockers` (every terminal state has a detected cycle, none a silent one).
**Trace.** SPEC-005 §3.5; KRN-SYNC-024

### KRN-SYNC-016  Any unlock order
**Statement.** Mutexes may be unlocked in any order; there is no LIFO requirement.
**Rationale.** ChibiOS's LIFO rule is a usability trap (R-001 §5); the owned list makes any order cheap.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/unlock_non_lifo`.
**Trace.** SPEC-005 §3.3

### KRN-SYNC-017  Static ceilings from the system description
**Statement.** A ceiling mutex declared in the system description shall receive its ceiling from the generator as the highest base priority among its declared users; locking it shall raise the effective priority to the ceiling in O(1) and, when every user is declared, shall never need a wait queue.
**Rationale.** RTIC's compile-time ceilings for threads in a C kernel (ADR-030).
**Status.** Proposed 2026-10-07
**Verification.** Test: generator test `hw/gen_ceiling` plus `sync/ceiling_static_no_queue` on the tiny profile.
**Trace.** SPEC-005 §3.4; ADR-030

### KRN-SYNC-018  Effective priority definition
**Statement.** A thread's effective priority shall be the maximum of its scheduling base priority, the effective priority of the highest waiter on each `INHERIT` mutex it owns, and the ceiling of each `CEILING` mutex it owns; a change shall be applied to the ready structure, to the thread's wait-queue position, or to the reschedule decision in the operation that caused it.
**Rationale.** One definition that every operation recomputes; no incremental bookkeeping that can drift.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: model invariant checked after every step; checked builds assert it after every priority-touching operation (SPEC-005 §11).
**Trace.** SPEC-005 §2.1

### KRN-SYNC-019  Propagation in both directions
**Statement.** A change of a thread's effective priority shall propagate along the chain of `INHERIT` mutexes it waits on, for raises and for lowerings, in the same operation, stopping where a hop's effective priority does not change or at the configured depth.
**Rationale.** Non-propagated lowerings leave distant owners boosted (Zephyr); propagating both keeps the invariant of KRN-SYNC-018 global.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/pi_chain_lowering`; model scenarios `nested_chain_timeout`, `waiter_priority_change`.
**Trace.** SPEC-005 §2.2

### KRN-SYNC-020  Mutex lock cost
**Statement.** An uncontended lock or unlock shall be O(1); a contended lock shall be O(waiters on that mutex) for the enqueue plus O(depth) for the walk, each hop a bounded section.
**Rationale.** R-003 target T9.
**Status.** Proposed 2026-10-07
**Verification.** Benchmark: harness operation "mutex lock and unlock uncontended; contended with chains of depth 1, 2, 4" (TEST-012); monitor of ADR-034 for section lengths.
**Trace.** SPEC-005 §3.2, §2.2; R-003 §4 T9

### KRN-SYNC-021  Hand-off on unlock
**Statement.** Unlock shall transfer ownership to the highest waiter in the same step as its wake; the new owner shall inherit from the remaining waiters at once; a thread that arrives between the unlock and the new owner's run shall not acquire the mutex.
**Rationale.** No barging, wait-order fairness (KRN-WAIT-012).
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/mutex_no_barging`; model scenario `two_waiters_handoff`.
**Trace.** SPEC-005 §3.3

### KRN-SYNC-022  Waiter removal hook
**Statement.** A mutex's wait queue shall carry the inherit flag; the wait protocol shall call the mutex's waiters-changed hook after any dequeue or requeue that is not a hand-off, and the hook shall recompute the owner's chain.
**Rationale.** Timeout, cancellation, destroy, and priority change all change the owner's contribution; one hook covers them.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/pi_cancel_disinherits`, `sync/pi_destroy_disinherits`; model scenario `cancel_waiter_disinherits`.
**Trace.** SPEC-005 §3.3; SPEC-004 §5.2

### KRN-SYNC-023  Recursion and self-relock
**Statement.** A `RECURSIVE` mutex shall count nested locks by its owner up to 255 and return `EMB_EOVERFLOW` beyond; a non-recursive mutex locked again by its owner shall return `EMB_EDEADLK` (fault in checked builds).
**Rationale.** Self-deadlock is the most common mutex bug; it must not hang.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/mutex_recursive_depth`, `misuse/mutex_self_relock`.
**Trace.** SPEC-005 §3.2

### KRN-SYNC-024  `EMB_EDEADLK`
**Statement.** `EMB_EDEADLK` shall be defined with value -32 in the kernel-reserved status range; it shall be returned for a detected cycle through `INHERIT` mutexes and for a non-recursive self-relock; cycles through `NONE` mutexes are not detected and the documentation shall say so.
**Rationale.** A stable code for a condition that is always a program error.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: `emb/status.h` and `emb_status_name()`; Test: `sync/deadlock_two_lockers`.
**Trace.** SPEC-005 §3.1, §3.5; SPEC-001 §4.2

### KRN-SYNC-025  Owner death policies
**Statement.** `CONFIG_EMB_MUTEX_OWNER_DEATH=FAULT` (default) shall fault in checked builds and record a non-fatal crash-record event in release builds while releasing the mutexes; `RELEASE` shall hand each owned mutex to its highest waiter with `EMB_EOWNERDEAD` or mark it inconsistent so that the next lock returns `EMB_EOWNERDEAD`; the inconsistent mark shall be cleared by the recovering owner's unlock or by `emb_mutex_mark_consistent()`.
**Rationale.** Robust-mutex semantics simple enough for an MCU; the next owner always learns that state may be inconsistent.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/owner_death_*`; model scenarios `owner_death_with_waiter`, `owner_death_no_waiter`.
**Trace.** SPEC-005 §3.6; KRN-SYNC-011; 08 Q5

### KRN-SYNC-026  Ceiling rules
**Statement.** Locking a `CEILING` mutex shall raise the caller's effective priority to the ceiling immediately; a caller whose base priority exceeds the ceiling, or that is not a declared user of a statically declared mutex, is misuse (`EMB_EPERM` in release builds); `CONFIG_EMB_MUTEX_CEILING_STATIC_ONLY` shall build ceiling mutexes without a wait queue.
**Rationale.** Immediate ceiling gives deadlock freedom and a single blocking per job; the misuse rule is what makes "never a waiter" true.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/ceiling_*`, `misuse/ceiling_violation`; footprint test on the tiny profile.
**Trace.** SPEC-005 §3.4; KRN-SYNC-017

### KRN-SYNC-027  No ISR operation on mutexes
**Statement.** Mutexes, condition variables, and barriers shall have no ISR-callable operation; semaphores and event flags shall have ISR-safe `give`, `set`, and `clear`.
**Rationale.** An ISR cannot own anything, so it cannot participate in inheritance (R-001 §5, FreeRTOS).
**Status.** Proposed 2026-10-07
**Verification.** Analysis: `@ctx` annotations; Test: `misuse/mutex_from_isr`.
**Trace.** SPEC-005 §1, §3.1, §11

### KRN-SYNC-028  Semaphore semantics
**Statement.** A semaphore shall have a configurable count width (8, 16, or 32 bits), an initial count and a maximum; `give` with a waiter shall hand the unit over without touching the count, otherwise increment, returning `EMB_EOVERFLOW` at the maximum unless `SATURATE` is set; `take` shall decrement when positive else wait; the kernel shall offer no `give_all` and no `reset`.
**Rationale.** Hand-off gives fairness; the bound makes binary semaphores a configuration, not a type; resets with waiters are the hazard ADR-020 refuses.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/sem_*`; model: the semaphore of `tools/model` with unit conservation.
**Trace.** SPEC-005 §4

### KRN-SYNC-029  Event flag semantics
**Statement.** Each event waiter shall carry its own mask and mode; `set` shall wake every satisfied waiter in queue order, handing it the satisfied bits, and shall apply a waiter's `CLEAR` before evaluating the next waiter; `clear` shall never wake; `set` shall be ISR-safe and O(waiters on that group).
**Rationale.** Deterministic consumption of overlapping bits; a single batch wake.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/event_any_all_clear`, `sync/event_overlapping_clear_order`.
**Trace.** SPEC-005 §5

### KRN-SYNC-030  Condition variable semantics
**Statement.** `cond_wait` shall require ownership with recursion count 1, shall enqueue and then fully unlock the mutex atomically with respect to `signal`, and shall relock the mutex before returning on every result; `signal` shall wake the highest waiter and `broadcast` all; the kernel shall never wake a condition waiter without a signal or broadcast.
**Rationale.** Classic, analyzable semantics; the relock-always rule is what callers can reason about.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/cond_*` including relock after timeout; `misuse/cond_wait_not_owner`.
**Trace.** SPEC-005 §6

### KRN-SYNC-031  Barrier semantics
**Statement.** With `CONFIG_EMB_BARRIER`, the arrival that completes the count shall wake all waiters with one reschedule and return serial to exactly one caller; a waiter that times out or is canceled shall leave the barrier without releasing the others.
**Rationale.** Phase synchronization for parallel work on multicore targets.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/barrier_*`.
**Trace.** SPEC-005 §7

### KRN-SYNC-032  Priority change preserves contributions
**Statement.** Changing a thread's base priority shall preserve inherited and ceiling contributions and shall propagate along the chain the thread waits on; a thread's visible priority shall be queryable as base and as effective.
**Rationale.** Setting the priority of a boosted owner must not drop the boost (ChibiOS gets this right; several others do not).
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/set_priority_keeps_boost`, `sync/set_priority_of_waiter_propagates`; model scenario `waiter_priority_change`.
**Trace.** SPEC-005 §10, §2.3

### KRN-SYNC-033  Spinlock rules
**Statement.** Spinlocks shall be acquired with interrupts masked, shall never be held across a blocking call or a preemption point, shall not be recursive, shall follow the declared lock order, and shall compile to the critical section on uniprocessor builds; checked SMP builds shall validate these rules.
**Rationale.** SMP correctness depends on discipline that only a validator keeps honest.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: spinlock validator; Test: SMP `misuse/spin_*`.
**Trace.** SPEC-005 §8; SPEC-002 §4.3; KRN-SMP-002

### KRN-SYNC-034  Atomics
**Statement.** The atomics API shall provide 32-bit and pointer-sized operations with explicit memory orders on 32-bit targets, 8-bit hardware atomics on AVR with 32-bit operations implemented by critical section, and no 64-bit atomic type.
**Rationale.** 09 §6; KRN-TIM-016.
**Status.** Proposed 2026-10-07
**Verification.** Test: `sync/atomic_*` on every architecture; Analysis: no 64-bit atomic builtin in the link map.
**Trace.** SPEC-005 §9

### KRN-SYNC-035  Binding on semaphores and events
**Statement.** Semaphores and event groups shall support the notification binding of ADR-027; a count transition from zero to one, or a set that leaves changed nonzero bits after serving direct waiters, shall signal the binding; a transition consumed by a hand-off shall not.
**Rationale.** Multi-object wait without poll objects (KRN-NOTIF-004).
**Status.** Proposed 2026-10-07
**Verification.** Test: `notif/bind_sem`, `notif/bind_event`; model scenario `binding_hook`.
**Trace.** SPEC-005 §4.2, §5.2; SPEC-004 §6.3

### KRN-SYNC-036  Trace and statistics
**Statement.** Lock, unlock with hand-off target, every inheritance change with its cause, depth-exceeded, deadlock-detected, semaphore give with hand-off, event set with woken count, and condition signal shall be trace events; contention counts, maximum boost, and maximum walk depth shall be optional statistics.
**Rationale.** Inheritance bugs are invisible without a trace that names the cause.
**Status.** Proposed 2026-10-07
**Verification.** Test: the trace decoder reconstructs every priority change of the conformance run.
**Trace.** SPEC-005 §12; OBS-005

### KRN-SYNC-037  Tiny profile options
**Statement.** The tiny profile shall offer inheritance as an option (default on), ceiling mutexes without a wait queue, 8-bit semaphore counts and event bits, and shall build condition variables and barriers only when configured.
**Rationale.** Footprint targets T1 and T2 with correctness kept by default.
**Status.** Proposed 2026-10-07
**Verification.** Test: footprint of the tiny reference configuration; conformance `sync/` group on it.
**Trace.** SPEC-005 §13; ADR-036

### KRN-SYNC-038  Model coverage for inheritance
**Statement.** The reference model shall include the mutex with inheritance, ceiling, recursion, hand-off, owner death, and deadlock detection, and shall be explored exhaustively over at least: the three-thread inversion, a two-level chain with timeout, deadlock in both lock orders, owner death with and without a waiter, a waiter's priority change, and cancellation of a waiter.
**Rationale.** The algorithm's correctness claims (R-001 §5 columns) are checked by exploration before implementation.
**Status.** Proposed 2026-10-07
**Verification.** Analysis: `tools/model/tests/test_explore.py` covers the listed scenarios; CI runs it.
**Trace.** SPEC-005 §15; KRN-WAIT-007; TEST-008
