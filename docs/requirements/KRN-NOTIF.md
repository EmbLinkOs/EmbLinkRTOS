# KRN-NOTIF - Notifications and Notification Binding

Group `KRN-NOTIF`. Design: `docs/specs/SPEC-006-notifications-and-work-queues.md` §2, §3. Related groups: KRN-WAIT (a notification wait is a wait with reason NOTIFY), KRN-SYNC (semaphores and events as bound objects), KRN-IPC (queues and pipes as bound objects), KRN-WQ (work queues run on a kernel bit), KRN-CAP (the NOTIFY and BIND rights).

KRN-NOTIF-001 to 006 originate in `docs/architecture/03-kernel-architecture.md` §6.1 (004 to 006 added by ADR-027). All are restated here as the authoritative copy. New requirements start at 007.

---

### KRN-NOTIF-001  Per-thread bits, ISR-settable
**Statement.** Each thread shall own a notification bit set that can be set from ISR context without blocking.
**Rationale.** The cheapest completion path from an interrupt to a thread (R-001 §6).
**Status.** Accepted 2026-10-07
**Verification.** Test: `notif/set_from_isr`; Analysis: `emb_notify_set` is `@ctx thread isr`.
**Trace.** SPEC-006 §2; 03 §6.1

### KRN-NOTIF-002  Owner-only wait
**Statement.** Only the owning thread shall wait on its notifications.
**Rationale.** One possible waiter makes the structure a single slot and every operation O(1).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `emb_notify_wait` has no thread argument.
**Trace.** SPEC-006 §2.2

### KRN-NOTIF-003  O(1), allocation free
**Statement.** Notification set and wait shall be O(1) and allocation free.
**Rationale.** It is the primitive everything else is built on.
**Status.** Accepted 2026-10-07
**Verification.** Benchmark: harness operation T8 (R-003 §4); Analysis: no list and no allocation in the path.
**Trace.** SPEC-006 §2.2; R-003 §4 T8

### KRN-NOTIF-004  Binding on every waitable object
**Statement.** Every waitable kernel object shall support binding to one notification bit of one thread; the set shall be O(1) and shall happen on every transition to the ready condition.
**Rationale.** Multi-object wait without poll objects (ADR-027).
**Status.** Accepted 2026-10-07
**Verification.** Test: `notif/bind_<object>` for every object type; model scenario `binding_hook`.
**Trace.** SPEC-006 §3; ADR-027

### KRN-NOTIF-005  Direct waiters unaffected
**Statement.** Binding shall not change the object's own wait-queue semantics for threads blocked directly on it.
**Rationale.** A binding is an observer of transitions, not a waiter.
**Status.** Accepted 2026-10-07
**Verification.** Test: `notif/bind_with_direct_waiter` (the direct waiter is served, no bit is set).
**Trace.** SPEC-006 §3.2; SPEC-004 §6.3

### KRN-NOTIF-006  Bit budget
**Statement.** The `base` profile shall reserve at least 16 notification bits for application use after the kernel-reserved bits; the `tiny` profile may select 8 bits with at least 4 reserved for the application.
**Rationale.** Bindings need spare bits (answers 08 Q7).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `EMB_NOTIFY_APP_BITS` is 24, 12, or 4 for 32, 16, 8-bit words.
**Trace.** SPEC-006 §1, §2.1

### KRN-NOTIF-007  Kernel bits
**Statement.** The high `EMB_NOTIFY_KERNEL_BITS` of the word are reserved for the kernel (`K_WORK`, `K_FAULT`, `K_BUDGET`, rest reserved); application code shall not be able to set them (`EMB_EINVAL`), and they shall not be settable across a partition boundary.
**Rationale.** Work queues, supervisor fault delivery, and budget notices need bits the application cannot forge.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/notify_kernel_bit`; isolated profile test `part/notify_kernel_bit_cross_partition`.
**Trace.** SPEC-006 §2.1, §2.2

### KRN-NOTIF-008  Level semantics and hand-off
**Statement.** Bits set while nobody waits shall persist until consumed; a set that satisfies the waiting owner shall hand over the satisfied bits, apply the waiter's `CLEAR` to those bits, and wake it in the same step; a bound object's transition shall produce no lost wakeup and at most one spurious pass of the bound thread.
**Rationale.** The edge-plus-re-check contract the multi-source pattern relies on (SPEC-006 §3.2, §3.3).
**Status.** Accepted 2026-10-07
**Verification.** Test: `notif/level_then_wait`, `notif/all_across_two_sets`, `notif/bound_multi_source`; model scenarios `notify_set_in_window`, `notify_level`, `notify_all_two_sets`, `bound_two_sems`.
**Trace.** SPEC-006 §2.2, §3.2

### KRN-NOTIF-009  Binding lifecycle
**Statement.** `bind` on an already bound object shall return `EMB_EEXIST`; `unbind` shall remove the binding; destroying the object shall not touch the thread's bits; a set to a terminated or restarted thread shall be dropped (detected by generation where generations are checked).
**Rationale.** Deterministic ownership of the single binding slot; no dangling-pointer action.
**Status.** Accepted 2026-10-07
**Verification.** Test: `notif/bind_twice`, `notif/unbind_rebind`, `notif/bind_target_restarted` (isolated profile).
**Trace.** SPEC-006 §3.1, §3.2

### KRN-NOTIF-010  Rights in isolated profiles
**Statement.** In isolated profiles, setting another partition's thread notification shall require a thread capability with the `NOTIFY` right, and binding an object shall require the object's `BIND` right and the thread's `NOTIFY` right.
**Rationale.** A notification is a cross-partition signal; capabilities govern it like every other cross-partition action.
**Status.** Accepted 2026-10-07
**Verification.** Test: `part/notify_without_right` returns `EMB_EPERM`.
**Trace.** SPEC-006 §2.2, §3.1; KRN-CAP-002

### KRN-NOTIF-011  Trace
**Statement.** Notification set (with whether it woke the owner), wait begin and result, and binding signals shall be trace events compiled out when tracing is disabled.
**Rationale.** "Who set my bit" is the first debugging question of event-driven code.
**Status.** Accepted 2026-10-07
**Verification.** Test: trace decoder reconstructs every set of the conformance run.
**Trace.** SPEC-006 §7; OBS-005
