# KRN-TIM - Kernel Clock, Timeouts, Sleep, Software Timers

Group `KRN-TIM`. Design: `docs/specs/SPEC-003-time-timeouts-and-timers.md` and SPEC-001 §7. Related groups: KRN-IRQ (timer interrupt), KRN-WAIT (timeout as a wake source), KRN-WQ (callback execution), KRN-SCH (time slicing).

KRN-TIM-001 to 007 originate in the v0.1 specification (§7.5); KRN-TIM-008 to 016 in `docs/architecture/03-kernel-architecture.md` §4. All are restated here as the authoritative copy; KRN-TIM-009 carries the refinement made at the same time in document 03. New requirements start at 017.

---

### KRN-TIM-001  Monotonic kernel time
**Statement.** Kernel time used for scheduling and timeouts shall be monotonic.
**Rationale.** A clock that moves backward makes deadlines meaningless.
**Status.** Accepted (v0.1)
**Verification.** Test: `emb_time_now()` never decreases across tick, tickless wake, and wall-clock adjustment tests.
**Trace.** v0.1 §7.5; SPEC-003 §3

### KRN-TIM-002  Independence from wall-clock time
**Statement.** Timeout correctness shall not depend on wall-clock time.
**Rationale.** Calendar time is adjusted, stepped, and synchronized; deadlines must not move with it.
**Status.** Accepted (v0.1)
**Verification.** Test: pending timeouts are unaffected by `emb_wallclock_set` and `emb_wallclock_adjust`.
**Trace.** v0.1 §7.5; SPEC-003 §2, §10

### KRN-TIM-003  Correct across counter wraparound
**Statement.** Finite timeout handling shall remain correct across underlying counter wraparound.
**Rationale.** 24-bit, 32-bit, and 16-bit hardware counters wrap within seconds to days.
**Status.** Accepted (v0.1)
**Verification.** Test: `tests/api/time/` and the timeout conformance tests run with the clock pre-set near every wrap boundary in both tick profiles.
**Trace.** v0.1 §7.5; SPEC-003 §5.2

### KRN-TIM-004  Tickless operation permitted
**Statement.** The kernel shall permit tickless operation.
**Rationale.** Power and deadline precision (ADR-017, 04 §4).
**Status.** Accepted (v0.1)
**Verification.** Test: the conformance suite passes in tickless mode on native and on a Cortex-M emulated target.
**Trace.** v0.1 §7.5; SPEC-003 §4.2

### KRN-TIM-005  Defined callback-context semantics
**Statement.** One-shot and periodic software timers shall have defined callback-context semantics.
**Rationale.** Callback context confusion is a leading cause of RTOS bugs.
**Status.** Accepted (v0.1)
**Verification.** Test: callback context observed with `emb_context()` for default and `ISR_CONTEXT` timers.
**Trace.** v0.1 §7.5; SPEC-003 §7.3

### KRN-TIM-006  Bounded timer operations
**Statement.** Timer operations on real-time paths shall have bounded behavior for a defined configuration.
**Rationale.** Timer start and stop are called from ISRs and critical paths.
**Status.** Accepted (v0.1)
**Verification.** Analysis: `@time` annotations; Demonstration: benchmark of start and stop with N armed timers.
**Trace.** v0.1 §7.5; SPEC-003 §5.1, §7.2

### KRN-TIM-007  Documented conversions
**Statement.** Time conversion and tick-rate behavior shall be explicitly documented to avoid silent truncation or overflow.
**Rationale.** Unit bugs are silent until a field failure.
**Status.** Accepted (v0.1)
**Verification.** Inspection: SPEC-001 §7.3 and each port's time section; Test: conversion vectors (API-023).
**Trace.** v0.1 §7.5; SPEC-001 §7.3; SPEC-003 §11

### KRN-TIM-008  Clock width
**Statement.** Kernel monotonic time shall be a 64-bit tick count in the base and larger profiles; the tiny profile may select a 32-bit count with wrap-safe comparison.
**Rationale.** 64 bits remove wrap handling where the core can afford it; 32 bits keep the ATmega328P affordable (ADR-017).
**Status.** Accepted (03 §4.1)
**Verification.** Test: both profiles in the time test matrix.
**Trace.** 03 §4.1; SPEC-003 §3

### KRN-TIM-009  Tick unit
**Statement.** The tick unit shall be the configuration constant `CONFIG_EMB_TICK_NS`; in tickless mode the achievable resolution is bounded by the hardware timer period, and the unit of the API stays the configured one.
**Rationale.** The API unit must not change with the clock mode, or application code is not portable between them.
**Status.** Accepted (03 §4.1, refined 2026-10-07)
**Verification.** Test: identical conversion results in periodic and tickless modes.
**Trace.** 03 §4.1; SPEC-003 §3, §4.2

### KRN-TIM-010  Distinct time types
**Statement.** Public time types shall be distinct: `emb_instant_t` (absolute), `emb_duration_t` (relative), `emb_timeout_t` (relative or sentinel). Mixing them shall be a compile error where the language allows.
**Rationale.** See API-022.
**Status.** Accepted (03 §4.1)
**Verification.** Test: negative compile tests (API-022).
**Trace.** 03 §4.1; SPEC-001 §7.1

### KRN-TIM-011  Absolute-deadline variants
**Statement.** Absolute-deadline variants shall exist for sleep and for every blocking operation, so that periodic work does not accumulate drift.
**Rationale.** See API-002.
**Status.** Accepted (03 §4.1)
**Verification.** Analysis: API-002 check; Test: a periodic loop with `sleep_until` shows zero accumulated drift over 10 000 periods on the native port.
**Trace.** 03 §4.1; SPEC-003 §6

### KRN-TIM-012  Cycle counter is not the scheduling clock
**Statement.** A high-resolution cycle counter API shall exist for measurement where hardware provides one; it shall not be the scheduling clock.
**Rationale.** Cycle counters wrap fast and may stop in sleep; they measure, they do not schedule.
**Status.** Accepted (03 §4.1)
**Verification.** Analysis: no kernel timeout path reads `emb_cycles_now`.
**Trace.** 03 §4.1; SPEC-003 §9

### KRN-TIM-013  Callbacks on a work queue by default
**Statement.** Software timer callbacks shall execute by default on a work queue in thread context.
**Rationale.** Interrupt-context callbacks are dangerous for general code (ADR-019).
**Status.** Accepted (03 §4.3)
**Verification.** Test: default timer callback observes `EMB_CONTEXT_THREAD` and runs on the configured queue's thread.
**Trace.** 03 §4.3; SPEC-003 §7.3

### KRN-TIM-014  ISR-context callbacks are opt-in and bounded
**Statement.** A timer may be created with `ISR_CONTEXT` only when its callback is declared ISR-safe; such callbacks shall be bounded and shall not block.
**Rationale.** A few callbacks need interrupt-level latency; the rest must not pay for it or risk it.
**Status.** Accepted (03 §4.3)
**Verification.** Test: a blocking call from an `ISR_CONTEXT` callback faults in checked builds.
**Trace.** 03 §4.3; SPEC-003 §7.3

### KRN-TIM-015  Periodic re-arm without drift
**Statement.** One-shot and periodic timers shall be supported; a periodic timer's next expiry shall be computed from the previous expiry, not from callback completion.
**Rationale.** Re-arming from completion accumulates callback latency into the period.
**Status.** Accepted (03 §4.3)
**Verification.** Test: a periodic timer's expiry instants are exact multiples of the period from the first expiry over 10 000 periods on native.
**Trace.** 03 §4.3; SPEC-003 §7.4

### KRN-TIM-016  No 64-bit atomics for time
**Statement.** 64-bit time values shall be read and written under a critical section or a sequence lock; the kernel shall not depend on 64-bit atomic loads, stores, or read-modify-write operations.
**Rationale.** 32-bit targets do not provide them under EmbCC (09 §6).
**Status.** Accepted (03 §4.1)
**Verification.** Analysis: no `_Atomic` 64-bit object in `kernel/`; Test: clock read torture test under continuous timer updates.
**Trace.** 03 §4.1; SPEC-003 §3.1

### KRN-TIM-017  Two clock modes, one semantics
**Statement.** The kernel shall support a periodic-tick mode and a tickless mode selected at configuration time. Every time, timeout, sleep, and timer requirement shall hold identically in both modes, and the conformance suite shall run in both.
**Rationale.** A mode that changes behavior is a fork in disguise.
**Status.** Accepted 2026-10-07
**Verification.** Test: conformance suite in both modes on native and on an emulated Cortex-M.
**Trace.** SPEC-003 §4

### KRN-TIM-018  Next-deadline computation
**Statement.** In tickless mode the kernel shall program the timer source for the earliest of: the earliest armed timeout, the earliest software timer, the running thread's quantum expiry when time slicing applies, the next budget replenishment when temporal protection is enabled, and `now` plus the port's maximum interval. The computation shall be O(1) given the ordered timeout structure.
**Rationale.** Missing an input means a late wake; an expensive computation means it cannot run on every change.
**Status.** Accepted 2026-10-07
**Verification.** Test: each input is the earliest in turn and the observed wake matches; Analysis: no loop over all timers in the reprogram path.
**Trace.** SPEC-003 §4.2

### KRN-TIM-019  Clock update on wake
**Statement.** In tickless mode, on any interrupt that ends idle, the kernel shall update the clock from the hardware counter and process expired deadlines before any scheduling decision is made.
**Rationale.** A wake from a non-timer interrupt must still see correct time and wake threads whose timeouts have passed.
**Status.** Accepted 2026-10-07
**Verification.** Test: a GPIO-class interrupt wakes the idle core after a timeout deadline has passed; the timed-out thread runs before the interrupt's own woken thread when its priority is higher.
**Trace.** SPEC-003 §4.2, §4.3

### KRN-TIM-020  Architecture timer contract
**Statement.** Each port shall implement `emb_arch_timer_init`, `emb_arch_timer_now_raw`, `emb_arch_timer_set_deadline_raw`, `emb_arch_timer_cancel`, `emb_arch_timer_hz`, and `emb_arch_timer_max_ticks`, and its timer ISR shall call `embk_time_timer_isr`; tickless ports shall call `embk_time_on_wake` at the outermost exit of any interrupt that ended idle. Raw-to-tick conversion shall be exact when the periods divide, and otherwise documented with a bounded, non-accumulating error.
**Rationale.** One contract for every timer source keeps the kernel's time code portable (KRN-IRQ-015 for timers).
**Status.** Accepted 2026-10-07
**Verification.** Test: port conformance for the timer contract on each architecture; Analysis: configuration-time divisibility check.
**Trace.** SPEC-003 §4.3

### KRN-TIM-021  Timeout structure
**Statement.** Armed timeouts and timers shall be kept in an intrusive structure with O(1) removal, O(1) access to the earliest deadline, and expiry in deadline order with arming order as the tie-break. The default shall be a doubly linked list ordered by absolute deadline; an alternative structure may be selected per profile behind the same interface only on the basis of measurements.
**Rationale.** ADR-010; intrusive nodes satisfy KRN-TCB-007 and KRN-RQ-004.
**Status.** Accepted 2026-10-07
**Verification.** Test: ordering and tie-break tests; Demonstration: insertion cost measured versus armed count per reference board.
**Trace.** SPEC-003 §5.1; ADR-010

### KRN-TIM-022  32-bit profile timeout range
**Statement.** In the 32-bit tick profile, a finite timeout shall be clamped to `2^31 - 1` ticks and a `_until` deadline more than `2^31 - 1` ticks in the future shall be rejected with `EMB_EOVERFLOW`; deadline comparisons shall use signed wrap-safe differences.
**Rationale.** This is the condition under which wrap-safe comparison is correct (KRN-TIM-003).
**Status.** Accepted 2026-10-07
**Verification.** Test: boundary tests at `2^31 - 1` and `2^31` in the 32-bit profile.
**Trace.** SPEC-003 §5.2

### KRN-TIM-023  Deadline computation
**Statement.** Arming with a duration shall compute the deadline as `now + duration` saturating at `EMB_TICK_MAX`, where a saturated deadline is forever; a deadline at or before `now` shall take the `EMB_NO_WAIT` path.
**Rationale.** API-024 at the kernel boundary.
**Status.** Accepted 2026-10-07
**Verification.** Test: saturation and past-deadline cases for every blocking primitive.
**Trace.** SPEC-003 §5.2; API-024

### KRN-TIM-024  Sleep semantics
**Statement.** `emb_thread_sleep` and `emb_thread_sleep_until` shall block the caller with wait reason `SLEEP` until the deadline, returning `EMB_OK` on expiry or `EMB_ECANCELED` if canceled. A zero duration or a past deadline shall behave exactly as `emb_thread_yield`.
**Rationale.** Sleep is a wait with no object; zero sleep as yield is the common expectation and gives equal-priority threads a turn.
**Status.** Accepted 2026-10-07
**Verification.** Test: conformance tests for sleep, sleep_until, zero sleep ordering among equal-priority threads, and cancellation.
**Trace.** SPEC-003 §6; KRN-THR-009; KRN-SCH-008

### KRN-TIM-025  Software timer API
**Statement.** The kernel shall provide `emb_timer_init`, `emb_timer_destroy`, `emb_timer_start`, `emb_timer_start_at`, `emb_timer_start_periodic`, `emb_timer_stop`, `emb_timer_stop_sync`, `emb_timer_restart`, `emb_timer_is_running`, `emb_timer_remaining`, and `emb_timer_get_counts` with the context classes and bounds of SPEC-003 §7.2. Start, stop, restart, and the queries shall be ISR-safe; `stop_sync` shall be thread-only and blocking with a timeout.
**Rationale.** Timers are driven from ISRs as often as from threads; teardown needs a synchronous stop.
**Status.** Accepted 2026-10-07
**Verification.** Test: API conformance including each function from ISR context where allowed.
**Trace.** SPEC-003 §7.2

### KRN-TIM-026  Expiry processing
**Statement.** At expiry, a work-queue timer shall submit its embedded work item to its configured queue in O(1) without calling application code; an `ISR_CONTEXT` timer shall call its callback directly in the expiry path. Expiry shall never run a work-queue callback in interrupt context.
**Rationale.** KRN-TIM-013 and 014 at the mechanism level; the expiry path must stay bounded regardless of callback behavior.
**Status.** Accepted 2026-10-07
**Verification.** Test: callback context tests; Analysis: expiry path `@time` and stack usage.
**Trace.** SPEC-003 §7.3, §7.4

### KRN-TIM-027  Periodic re-arm and overrun accounting
**Statement.** A periodic timer shall be re-armed from its previous deadline before its callback is run; whole periods already past at expiry shall be skipped and counted as overruns; an expiry whose work item is still queued from the previous expiry shall coalesce and count as an overrun. Expiry and overrun counts shall be readable and a trace point shall record each overrun.
**Rationale.** No drift (KRN-TIM-015), no unbounded backlog, and visibility when the system cannot keep up.
**Status.** Accepted 2026-10-07
**Verification.** Test: overload and sleep-past-several-periods scenarios on native; counts and trace match the model.
**Trace.** SPEC-003 §7.4

### KRN-TIM-028  Timer lifecycle
**Statement.** Destroying a timer whose callback is pending or running shall be misuse: a kernel fault in checked builds and `EMB_EBUSY` in release builds. `emb_timer_stop` shall report whether the callback was idle, pending, or running; `emb_timer_stop_sync` from the timer's own callback shall be misuse.
**Rationale.** Use-after-destroy in a work queue is otherwise undetectable.
**Status.** Accepted 2026-10-07
**Verification.** Test: misuse suite for timers in both build kinds.
**Trace.** SPEC-003 §7.5; KRN-OBJ-001

### KRN-TIM-029  Time slicing quantum as a deadline
**Statement.** When time slicing is enabled for the running thread's priority level, the kernel shall track its quantum expiry as a per-CPU deadline included in the next-deadline computation, and at expiry shall move the thread to the tail of its class only if another READY thread exists at the same effective priority.
**Rationale.** Round-robin without a timer object per thread, and v0.1 §5.8's rule that an uncontested quantum does not rotate.
**Status.** Accepted 2026-10-07
**Verification.** Test: round-robin ordering tests in both clock modes; uncontested quantum does not cause a switch.
**Trace.** SPEC-003 §8; KRN-SCH-007, KRN-SCH-026, KRN-SCH-037

### KRN-TIM-030  Cycle counter API
**Statement.** The kernel shall provide `emb_cycles_now`, `emb_cycles_hz`, and `emb_cycles_to_ns`, O(1) and callable from any context, with a resolution of at least 1 MHz where the hardware allows, sourced per architecture as documented.
**Rationale.** Benchmarks and critical-section statistics need a cheap fine-grained counter (KRN-RT-004 to 006).
**Status.** Accepted 2026-10-07
**Verification.** Test: monotonic within a non-sleeping interval; frequency matches the documented source within tolerance on HIL.
**Trace.** SPEC-003 §9

### KRN-TIM-031  Wall clock is an offset
**Statement.** When `CONFIG_EMB_WALLCLOCK` is enabled, wall-clock time shall be the kernel monotonic clock plus an offset; setting or adjusting it shall change the offset only and shall not move any timeout, timer, or deadline. No kernel path shall read the wall clock.
**Rationale.** KRN-TIM-002 made concrete.
**Status.** Accepted 2026-10-07
**Verification.** Test: pending deadlines unchanged across set and adjust; Analysis: no reference to wall-clock symbols from `kernel/` time paths.
**Trace.** SPEC-003 §10

### KRN-TIM-032  Clock read mechanism
**Statement.** `emb_time_now` shall read a multi-word clock with a sequence lock on architectures whose word is at least 32 bits, and with a critical section on AVR; it shall be O(1), callable from any context, and shall never mask interrupts on the sequence-lock architectures.
**Rationale.** Refines KRN-TIM-016 into the two concrete mechanisms EmbCC's atomic support allows (09 §6).
**Status.** Accepted 2026-10-07
**Verification.** Test: clock read torture test under continuous timer updates on each architecture; Analysis: no masking instruction in the sequence-lock read path.
**Trace.** SPEC-003 §3.1; KRN-TIM-016

### KRN-TIM-033  Bounded timer interrupt
**Statement.** The timer interrupt body shall be bounded to updating the clock, expiring due deadlines, charging the quantum, setting the reschedule flag, and reprogramming the next deadline, and shall emit the trace points `tick`, `timeout_expire`, and `timer_expire` when tracing is enabled.
**Rationale.** KRN-IRQ-031 at the time subsystem's side.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: handler stack usage and cycle budget; Demonstration: timer jitter benchmark.
**Trace.** SPEC-003 §4.1, §5.3; KRN-IRQ-031

### KRN-TIM-034  Port time documentation
**Statement.** Each architecture port's documentation shall state its timer source, counter width, frequency and the exact tick values it supports, maximum interval, tickless capability and the sleep states in which the counter runs, wake latency from each state, and cycle counter source.
**Rationale.** Tickless and power decisions depend on facts only the port knows.
**Status.** Accepted 2026-10-07
**Verification.** Inspection: `docs/ports/<arch>.md` time section against this list.
**Trace.** SPEC-003 §11, §13

### KRN-TIM-035  Checked-build detection for time
**Statement.** Checked builds shall detect and raise a kernel fault for arming an already armed timeout node, a blocking call from an `ISR_CONTEXT` timer callback, destroying a timer with a pending or running callback, and `emb_timer_stop_sync` from the timer's own callback; a `_until` deadline out of the 32-bit profile's range shall return `EMB_EOVERFLOW` in both build kinds.
**Rationale.** These are the time-related misuses that corrupt state silently in release builds.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/conformance/misuse/time/` on the native port in both build kinds.
**Trace.** SPEC-003 §12; API-015
