# KRN-IRQ - Interrupts, Exceptions, Critical Sections, Scheduler Lock

Group `KRN-IRQ`. Design: `docs/specs/SPEC-002-interrupts-and-critical-sections.md`. Related groups: KRN-SCH (v0.1 §4, 03 §2), KRN-RT (v0.1 §4.10), API (SPEC-001).

KRN-IRQ-001 to 003 and 010 to 016 originate in the v0.1 specification (§3.7 and §6.6) and are restated here as the authoritative copy; their statements are unchanged. Numbers 004 to 009 were never assigned and stay unassigned. New requirements start at 017.

---

### KRN-IRQ-001  No blocking in interrupt context
**Statement.** Interrupt context shall not perform blocking operations.
**Rationale.** An ISR has no thread to block; blocking there corrupts the scheduler's model of who is running.
**Status.** Accepted (v0.1)
**Verification.** Test: `tests/conformance/misuse/` calls each thread-only function from an ISR in checked and release builds (API-015).
**Trace.** v0.1 §3.7; SPEC-002 §3.1; SPEC-001 §5.2

### KRN-IRQ-002  Interrupt context is distinguishable
**Statement.** Interrupt context shall be distinguishable from thread context.
**Rationale.** Every context-class check and every ISR-safe code path depends on it.
**Status.** Accepted (v0.1)
**Verification.** Test: `emb_context()` returns `EMB_CONTEXT_ISR` inside every handler level and `EMB_CONTEXT_THREAD` outside, on every port.
**Trace.** v0.1 §3.7; SPEC-002 §2; SPEC-001 §5.1

### KRN-IRQ-003  Wakeup from an interrupt permits scheduling before thread resume
**Statement.** An interrupt that makes a higher-priority thread READY shall permit scheduling before returning to thread execution when preemption is enabled.
**Rationale.** This is the definition of preemptive scheduling driven by events rather than ticks.
**Status.** Accepted (v0.1)
**Verification.** Test: ISR wakes a higher-priority thread; that thread runs before the interrupted thread's next instruction (measured by a sequence counter).
**Trace.** v0.1 §3.7; SPEC-002 §6; KRN-SCH-020

### KRN-IRQ-010  ISR-safe operations are explicit
**Statement.** ISR-safe kernel operations shall be explicitly identified.
**Rationale.** Callers cannot guess; the annotation is the contract.
**Status.** Accepted (v0.1)
**Verification.** Analysis: every public function has a `@ctx` tag (API-003); the ISR-safe set is the functions tagged `thread isr`.
**Trace.** v0.1 §6.6; SPEC-001 §9; API-014

### KRN-IRQ-011  ISR-safe operations never block the caller
**Statement.** An ISR-safe operation shall not require the caller to block.
**Rationale.** Follows from KRN-IRQ-001 for the kernel's own implementation paths.
**Status.** Accepted (v0.1)
**Verification.** Analysis: `@ctx thread isr` functions are `@blocks no`, or `@blocks timeout` with a documented ISR-safe `EMB_NO_WAIT` path.
**Trace.** v0.1 §6.6; API-014

### KRN-IRQ-012  Nesting depth is observable
**Statement.** Nested interrupt depth shall be observable to the kernel when nesting is supported.
**Rationale.** The outermost-exit rule, stack sizing, and diagnostics all need the depth.
**Status.** Accepted (v0.1)
**Verification.** Test: `emb_irq_nesting_depth()` equals the actual depth in a nested-interrupt test on Cortex-M, RISC-V with nesting, and native.
**Trace.** v0.1 §6.6; SPEC-002 §7.1

### KRN-IRQ-013  Reschedule during nested handling stays pending
**Statement.** A reschedule requested during nested interrupt handling shall remain pending until a legal thread-resume boundary.
**Rationale.** Switching threads from inside a nested interrupt would leave an interrupted handler's frame on the wrong stack.
**Status.** Accepted (v0.1)
**Verification.** Test: nested ISR wakes a higher-priority thread; the switch occurs only after the outer handler returns.
**Trace.** v0.1 §6.6; SPEC-002 §6.1; KRN-SCH-021

### KRN-IRQ-014  Bounded kernel masking
**Statement.** Interrupt masking used by kernel internals shall have bounded duration.
**Rationale.** Kernel masking is the floor of every interrupt latency figure.
**Status.** Accepted (v0.1)
**Verification.** Demonstration: `CONFIG_EMB_IRQ_LOCK_STATS` under the stress suite on HIL reports the maximum; it is published per release.
**Trace.** v0.1 §6.6; SPEC-002 §4.2, §10; KRN-RT-003

### KRN-IRQ-015  No controller assumption in the generic kernel
**Statement.** The generic kernel shall not assume a specific interrupt-controller architecture.
**Rationale.** NVIC, PLIC, CLIC, AVR's fixed vectors, and the simulator are all targets.
**Status.** Accepted (v0.1)
**Verification.** Analysis: `kernel/` includes no architecture header and references no controller register (02 §5 dependency check).
**Trace.** v0.1 §6.6; SPEC-002 §8

### KRN-IRQ-016  Documented entry, exit, save, and reschedule behavior
**Statement.** Architecture documentation shall define interrupt-entry, interrupt-exit, context-save, and rescheduling behavior.
**Rationale.** A port's correctness is reviewable only against a written description.
**Status.** Accepted (v0.1)
**Verification.** Inspection: each `docs/ports/<arch>.md` has the sections named in 05 §9.
**Trace.** v0.1 §6.6; SPEC-002 §13

### KRN-IRQ-017  Two interrupt classes
**Statement.** The kernel shall distinguish kernel-aware interrupts, which it may mask and whose handlers may call ISR-safe API, from kernel-independent interrupts, which it shall never mask and whose handlers shall call no kernel function. The boundary shall be the configured level `CONFIG_EMB_IRQ_KERNEL_LEVEL`. The kernel-independent class shall be available only on architectures with interrupt priority masking and shall be rejected at configuration time elsewhere.
**Rationale.** Zero-latency interrupts are a real-time requirement the kernel can only meet by staying out of their way (v0.1 §6.3).
**Status.** Accepted 2026-10-07
**Verification.** Test: with the class enabled on Cortex-M, a kernel-independent interrupt fires inside a kernel critical section and its handler runs within hardware latency; Analysis: Kconfig rejects `CONFIG_EMB_IRQ_ZERO_LATENCY` on Armv6-M, AVR, and native.
**Trace.** SPEC-002 §3

### KRN-IRQ-018  Key-based, nestable critical sections
**Statement.** The kernel shall provide `emb_irq_lock()` returning the previous mask state and `emb_irq_unlock(key)` restoring exactly that state, callable from thread, interrupt, and pre-kernel context, nestable, with unlocks in reverse order of locks. Both shall be O(1), shall never block or yield, and shall act as compiler barriers.
**Rationale.** Key-based masking nests for free, works identically from ISRs, and maps to one or two instructions on every architecture (SPEC-002 §4.1).
**Status.** Accepted 2026-10-07
**Verification.** Test: nesting test restores the outer state exactly; Analysis: the inline wrappers carry a `"memory"` clobber on all three compilers.
**Trace.** SPEC-002 §4.1; KRN-RT-003

### KRN-IRQ-019  Critical sections are per CPU
**Statement.** `emb_irq_lock()` shall protect against interrupts and preemption on the current CPU only. Data shared across CPUs shall be protected by a spinlock acquired with interrupts masked, which on uniprocessor builds shall compile to the critical section alone.
**Rationale.** Masking is a local operation; pretending otherwise is the classic SMP porting bug (KRN-SMP-004).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: SMP specification lock-ordering rules; Test: the spinlock reduces to `emb_irq_lock()` on UP (symbol inspection).
**Trace.** SPEC-002 §4.3; KRN-SMP-002, KRN-SMP-004

### KRN-IRQ-020  Measured critical-section budgets
**Statement.** Each architecture port shall declare a cycle budget for the kernel's own critical sections and scheduler locks, and a configuration option shall measure the longest observed durations with the cycle counter. The measured maxima shall be published per reference board with full metadata.
**Rationale.** Evidence, not assertion (principle 6, KRN-RT-007).
**Status.** Accepted 2026-10-07
**Verification.** Demonstration: benchmark pipeline output per release; Test: checked builds fault when a declared budget is exceeded under the stress suite.
**Trace.** SPEC-002 §10

### KRN-IRQ-021  No blocking inside a critical section
**Statement.** A blocking call with a nonzero timeout, a thread-only operation, or a scheduler-lock release that would reschedule, made while a critical section opened by the caller is active, shall be misuse: a kernel fault in checked builds and `EMB_EPERM` in release builds.
**Rationale.** A critical section that spans a preemption point leaves interrupts masked for an unbounded time in another thread.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/conformance/misuse/` in both build kinds.
**Trace.** SPEC-002 §4.2; SPEC-001 §5.3; KRN-RT-002

### KRN-IRQ-022  Scheduler lock semantics
**Statement.** `emb_sched_lock()` and `emb_sched_unlock()` shall be thread-only, nestable by depth, shall not mask interrupts, and the unlock that reaches depth zero shall perform any pending reschedule immediately. A scheduler-lock operation from interrupt context shall be misuse. Yield while locked shall keep the reschedule pending and shall not switch.
**Rationale.** Restates KRN-SCH-016 to 019 at the API level and fixes the two edge cases v0.1 left open.
**Status.** Accepted 2026-10-07
**Verification.** Test: conformance tests for nesting, ISR wakeup while locked, yield while locked, and the ISR misuse case.
**Trace.** SPEC-002 §5; KRN-SCH-016, KRN-SCH-017, KRN-SCH-018, KRN-SCH-019

### KRN-IRQ-023  Enumerated preemption points
**Statement.** A thread switch shall occur only at: the outermost exit of a kernel-aware interrupt when a reschedule is pending and the scheduler is not locked; inside a thread-context kernel operation that changes schedulability when the caller is not in a critical section and the scheduler is not locked; and kernel start. A thread switch shall never occur inside a nested interrupt or inside a critical section.
**Rationale.** A closed list of preemption points is what makes the kernel's behavior analyzable and the reference model finite (ADR-013).
**Status.** Accepted 2026-10-07
**Verification.** Test: differential tests against the reference model's switch oracle on the native port; Analysis: the kernel has one switch call site per architecture path.
**Trace.** SPEC-002 §6.1; KRN-SCH-021; KRN-IRQ-013

### KRN-IRQ-024  Reschedule on outermost exit
**Statement.** When an ISR-safe operation changes the scheduling decision, the kernel shall set the per-CPU reschedule-pending flag and request the architecture's deferred switch; the architecture's outermost interrupt exit shall perform the switch when the flag is set and the scheduler is not locked, and shall clear the flag.
**Rationale.** This is the single mechanism by which interrupts drive scheduling (KRN-SCH-020, KRN-IRQ-003).
**Status.** Accepted 2026-10-07
**Verification.** Test: ISR wakeup latency test on each port; the switch happens exactly once per outermost exit.
**Trace.** SPEC-002 §6.2; KRN-SCH-020

### KRN-IRQ-025  Portable handler declaration
**Statement.** The kernel shall provide `EMB_ISR(name)` as the only portable declaration of a kernel-aware handler, expanding per architecture to the form that provides the kernel's entry and exit behavior, and `EMB_ISR_RAW(name)` for handlers that call no kernel function.
**Rationale.** Cortex-M handlers are plain functions, AVR needs a full-context `naked` vector, RISC-V needs a dispatcher entry; the application should not know (09 §8).
**Status.** Accepted 2026-10-07
**Verification.** Test: the same handler source builds and passes the ISR conformance tests on every port.
**Trace.** SPEC-002 §6.2, §8.3

### KRN-IRQ-026  Bounded nesting
**Statement.** Kernel-aware interrupt nesting shall be enabled only where the architecture permits and the configuration selects it. The maximum depth shall be a configuration constant used to size the interrupt stack, and exceeding it shall be a kernel fault in checked builds.
**Rationale.** Unbounded nesting is unbounded stack use.
**Status.** Accepted 2026-10-07
**Verification.** Test: nesting-depth test on the native port with fault injection; Analysis: Kconfig gating per architecture.
**Trace.** SPEC-002 §7.1

### KRN-IRQ-027  Interrupt stacks
**Statement.** Where the architecture permits, kernel-aware interrupts shall execute on a dedicated per-CPU interrupt stack sized from the maximum nesting depth and the handlers' measured frames. Where it does not (AVR), every thread's stack budget shall include a configured interrupt reserve, and the architecture documentation shall state its size derivation.
**Rationale.** Thread stacks must be sized for thread work only wherever the hardware allows (KRN-MEM-011).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `-fstack-usage` sums in the stack report; Test: interrupt-stack overflow detection fires in checked builds under the nesting stress test.
**Trace.** SPEC-002 §7.2; KRN-MEM-011

### KRN-IRQ-028  Generic interrupt priority numbering
**Statement.** Generic interrupt priorities shall use the thread convention (a larger number is more urgent), with level 0 the least urgent kernel-aware level and levels above `CONFIG_EMB_IRQ_KERNEL_LEVEL` kernel-independent. The architecture port shall map generic levels to hardware encodings, and the hardware description shall store generic levels.
**Rationale.** One priority direction across threads and interrupts removes a documented source of configuration mistakes, and the mapping belongs in one place per architecture.
**Status.** Accepted 2026-10-07
**Verification.** Test: `emb_irq_set_level()` followed by observed preemption order on each port; Analysis: generator emits encoded values from generic input.
**Trace.** SPEC-002 §8.1

### KRN-IRQ-029  Interrupt management API
**Statement.** The kernel shall provide O(1), non-blocking `emb_irq_enable`, `emb_irq_disable`, `emb_irq_is_enabled`, `emb_irq_set_level`, `emb_irq_pend`, and `emb_irq_clear_pending`, and, only when `CONFIG_EMB_IRQ_DYNAMIC` is selected, `emb_irq_connect`. By default the vector table shall be generated from the hardware description and static.
**Rationale.** A static table costs no RAM and no init code; dynamic connection is an option for the applications that need it.
**Status.** Accepted 2026-10-07
**Verification.** Test: API conformance per port; Analysis: no RAM vector table in the tiny profile image.
**Trace.** SPEC-002 §8.2; HW-001

### KRN-IRQ-030  Exceptions route to the fault manager
**Statement.** Synchronous faults shall enter through the architecture fault entry, which shall capture the available context and dispatch to the fault manager, classifying a fault from an unprivileged partition as a partition fault and any other as a kernel fault. System-call traps shall not be treated as faults. The crash record shall be written before any recovery action.
**Rationale.** Fault handling that is designed, not improvised (01 §6); isolation requires the partition classification.
**Status.** Accepted 2026-10-07
**Verification.** Test: provoked faults of each class on native and Cortex-M (emulated) produce decodable crash records and the right classification.
**Trace.** SPEC-002 §9; 03 §10; FLT-001, FLT-002

### KRN-IRQ-031  Timer interrupt level and bound
**Statement.** The system timer interrupt shall be a kernel-aware interrupt whose level defaults to the highest kernel-aware level and is configurable, and whose handler shall be bounded to moving expired timeouts to READY, setting the reschedule flag, and programming the next event.
**Rationale.** Timeout jitter should depend on critical sections and zero-latency interrupts only, and the handler must not become a place where work accumulates.
**Status.** Accepted 2026-10-07
**Verification.** Demonstration: timer jitter benchmark; Analysis: handler `-fstack-usage` and cycle budget.
**Trace.** SPEC-002 §11; KRN-TIM-006

### KRN-IRQ-032  Latency metrics are defined and published
**Statement.** The project shall define interrupt latency, kernel masking latency, ISR overhead, ISR-to-thread latency, and scheduler-lock latency as in SPEC-002 §10.1, shall measure them on every reference board with the benchmark pipeline, and shall publish worst observed values with full metadata.
**Rationale.** KRN-RT-004 to 007 require measurability; this fixes what is measured.
**Status.** Accepted 2026-10-07
**Verification.** Demonstration: published benchmark data per release.
**Trace.** SPEC-002 §10; KRN-RT-004, KRN-RT-005, KRN-RT-006, KRN-RT-007; TEST-006

### KRN-IRQ-033  The kernel never unmasks behind a caller
**Statement.** An ISR-safe kernel operation called while the caller holds a critical section shall return with the caller's mask state unchanged; kernel internals shall save and restore mask state with the same key-based primitive and shall never unconditionally enable interrupts.
**Rationale.** A kernel that enables interrupts inside a caller's critical section breaks the caller's invariant silently.
**Status.** Accepted 2026-10-07
**Verification.** Test: every ISR-safe function called under `emb_irq_lock()`; the key read after the call equals the key before; Analysis: no unconditional enable instruction in `kernel/` or in the ports' kernel paths.
**Trace.** SPEC-002 §4.2

### KRN-IRQ-034  Context query is O(1) and exact
**Statement.** `emb_context()` shall be O(1), callable from any context including nested interrupts and pre-kernel, and shall agree with the hardware's notion of exception activity on architectures that expose one.
**Rationale.** It is executed on every checked API entry and must be cheap and right.
**Status.** Accepted 2026-10-07
**Verification.** Test: context query inside nested interrupts and before kernel start on every port.
**Trace.** SPEC-002 §2.1; KRN-IRQ-002

### KRN-IRQ-035  Checked-build detection
**Statement.** Checked builds shall detect and raise a kernel fault for: out-of-order `emb_irq_unlock()`, scheduler-lock operations from interrupt context, nesting beyond the configured maximum, interrupt stack overflow, and kernel calls from `EMB_ISR_RAW` handlers on architectures where context can be distinguished.
**Rationale.** These bugs are invisible in release builds until they corrupt state.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/conformance/misuse/irq/` provokes each condition on the native port and on an emulated Cortex-M.
**Trace.** SPEC-002 §12; API-015

### KRN-IRQ-036  Handoff from kernel-independent interrupts
**Statement.** The architecture port shall provide `emb_arch_irq_pend_soft()`, callable from a kernel-independent handler, that pends a kernel-aware software interrupt whose handler may perform kernel work on behalf of the kernel-independent handler. Documentation shall present this as the only sanctioned path from a kernel-independent handler to the kernel.
**Rationale.** Zero-latency handlers need a way to wake threads without touching kernel state.
**Status.** Accepted 2026-10-07
**Verification.** Test: on Cortex-M with the class enabled, a kernel-independent handler pends the software interrupt and a thread is woken with measured latency.
**Trace.** SPEC-002 §3.2
