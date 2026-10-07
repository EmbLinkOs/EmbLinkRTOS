# 06 - Architecture Decision Records

Each record: context, decision, alternatives, consequences, status. **Proposed** records become **Accepted** on your review. Records are referenced from documents 02 to 05 by number.

---

## ADR-001 Build the native port in Milestone 1, alongside AVR

**Context.** v0.1 schedules host simulation late. A single architecture port cannot validate a portability contract; the first time a second port is written, assumptions surface. AVR is also the slowest possible test loop.

**Decision.** `arch/native` is a first-class port developed in parallel with AVR from the first context switch. Both must pass the conformance suite at Milestone 1.

**Alternatives.** AVR only first (v0.1); Cortex-M first for faster iteration (loses the constraint-exposing value of AVR).

**Consequences.** Slightly more Milestone 1 work; much faster iteration thereafter; sanitizers and the reference model usable from day one; the port contract is honest from the start.

**Status.** Proposed.

## ADR-002 API conventions: `emb_` namespace, single status enum, typed time

**Context.** Names and conventions were open in v0.1; every header depends on them.

**Decision.** Public `emb_`, port contract `emb_arch_`, kernel-private `embk_`, config `CONFIG_EMB_`. One `emb_status_t` with zero success and negative errors. Distinct `emb_instant_t`, `emb_duration_t`, `emb_timeout_t`. Blocking functions end with a timeout parameter; `_until` variants take an instant.

**Alternatives.** A new distinctive prefix such as `elk_` (avoids generic collisions, but splits brand from the EmbLink family); errno-style globals (hostile to ISRs and SMP); untyped `uint32_t` ticks (invites unit bugs).

**Consequences.** Headers are predictable; static tooling can key on prefixes; `embos`-like spellings are avoided for trademark reasons.

**Status.** Proposed.

## ADR-003 One API with documented context classes, no `_from_isr` duplicates

**Context.** FreeRTOS duplicates APIs for ISR use and returns a "higher priority task woken" flag. Zephyr uses one API and checks context.

**Decision.** One API. Each function is ISR-safe, thread-only, or pre-kernel, stated in its annotation. Misuse is a kernel fault in checked builds and `EMB_EPERM` in release. Reschedule-on-exit is driven by the per-CPU `reschedule_pending` flag and the architecture's interrupt exit path.

**Alternatives.** Duplicated `_isr` variants (smaller runtime check cost on AVR, double the API surface and documentation).

**Consequences.** One context check per call in checked builds (a load and compare); cleaner API; the tiny profile may compile out the check in release.

**Status.** Proposed.

## ADR-004 Hardware description: project-owned YAML schema with SVD and DeviceTree importers

**Context.** v0.1 asked for study of DeviceTree, CMSIS-SVD, and vendor formats. The pipeline determines how boards scale.

**Decision.** A project-owned YAML model (SoC and board files) validated by a published JSON schema, consumed by a Python generator. Concepts borrowed from DeviceTree (`compatible`, buses, references) without its syntax. Importers bootstrap SoC files from CMSIS-SVD and board files from DeviceTree source.

**Alternatives.** Adopt DeviceTree source directly (largest reuse of existing board files and tooling, but notoriously hard to author, poor error messages, and a large dependency); hand-written C board files (does not scale, v0.1 already rejects this).

**Consequences.** Full control of schema and error quality; EmbStudio can edit the model directly; DTS files from other ecosystems remain usable through import; the schema must be versioned from 1.0.

**Status.** Accepted 2026-10-06.

## ADR-005 Kconfig semantics for configuration; CMake as the reference build

**Context.** Exact configuration language and build integration were open.

**Decision.** Kconfig semantics through a Python implementation, emitting a normalized header and a JSON view. CMake with presets and per-compiler toolchain files is the reference build. EmbBuild, when available, drives the same targets.

**Alternatives.** Custom configuration language (no tooling, learning cost); Meson or Bazel (less embedded toolchain support, smaller audience); EmbBuild as the only build (violates toolchain independence).

**Consequences.** Familiar to the industry; dependency validation comes for free; generated board defaults slot in naturally.

**Status.** Proposed.

## ADR-006 Static allocation of opaque objects through generated storage types

**Context.** Opaque handles conflict with static allocation unless a portable mechanism exists.

**Decision.** The build generates `emb_<object>_storage_t` types with exact size and alignment from the configured kernel layout, plus `EMB_<OBJECT>_STORAGE(name)` macros. Init functions take caller storage and return the handle. A static assertion in the kernel verifies generated sizes against real sizes.

**Alternatives.** Expose structures publicly (freezes layout as ABI); oversize fixed buffers (wastes RAM on tiny targets); require an allocator (violates KRN-MEM-001).

**Consequences.** Works on all three compilers without extensions; objects can live in flash-initialized tables where configuration permits; a configuration change requires a rebuild of all users, which is already true.

**Status.** Proposed.

## ADR-007 Drivers as request/completion state machines

**Context.** Blocking-only driver APIs cannot serve DMA, isolated partitions, or cross-core forwarding without rewrites.

**Decision.** Every driver class defines request submission with asynchronous completion to a notification, semaphore, work item, or bounded ISR callback. Blocking APIs are wrappers.

**Alternatives.** Blocking APIs with per-driver interrupt handling (simple, does not compose); callback-only APIs (hard to use, callback context confusion).

**Consequences.** Slightly more structure per driver; one driver source for all transfer modes and placements; cancellation and timeouts implemented once.

**Status.** Proposed.

## ADR-008 MCUboot-compatible image format and slot semantics

**Context.** A production update story needs an image format; inventing one isolates the project from existing tooling and bootloaders.

**Decision.** Image header and TLV trailer compatible with MCUboot; slot semantics (swap, overwrite, direct-XIP) as defined there. EmbLinkRTOS does not require the MCUboot code base; a project bootloader may implement the same format later.

**Alternatives.** Project-specific format (full control, zero ecosystem); vendor bootloader formats (lock-in).

**Consequences.** Existing signing tools and bootloaders work immediately; multi-image manifests follow the same conventions; the project's own bootloader is optional, not blocking.

**Status.** Proposed.

## ADR-009 Deferred-format logging with string interning

**Context.** Text formatting on target costs flash, cycles, and bandwidth, so production builds turn logging off and lose diagnosability.

**Decision.** Format strings and metadata live in a non-loaded ELF section; the target emits an identifier plus raw arguments; the host decodes from the ELF. A plain-text backend remains for targets without a decoder.

**Alternatives.** printf-style on target (status quo); fixed binary event codes only (loses expressiveness).

**Consequences.** Logging cheap enough to leave on in production; EmbDebug and a standalone decoder both work; log identifiers are per build and need the matching ELF.

**Status.** Proposed.

## ADR-010 Timeout structure: sorted intrusive deadline list as default, wheel behind the same interface

**Context.** Open in v0.1.

**Decision.** Default is a doubly linked intrusive list ordered by absolute deadline (O(n) insert, O(1) removal and expiry check, no extra memory); the 32-bit tick profile compares deadlines wrap-safely instead of storing deltas. The timeout interface allows a hierarchical timing wheel per profile when measurements on a target with many timers justify it.

**Alternatives.** Wheel everywhere (memory on tiny targets); heap (allocation or fixed array, less cache friendly for small n).

**Consequences.** Simplest correct implementation first; measurable upgrade path; bound is "O(number of armed timeouts)" and is stated as such in the timing class.

**Status.** Proposed.

## ADR-011 Versioned kernel debug descriptor exported by every image

**Context.** RTOS-aware debuggers break on every internal layout change when they hard-code offsets.

**Decision.** Each image exports a read-only, versioned descriptor with struct offsets and locations needed to enumerate threads, states, stacks, locks, partitions, devices, and trace rings. EmbDebug and third-party tools read it.

**Alternatives.** Per-version debugger plugins (maintenance burden, excludes third-party probes).

**Consequences.** Internal layout stays free to change; debugger support is one plugin reading the descriptor.

**Status.** Proposed.

## ADR-012 SMP ready structure: global under one scheduler lock first, per-CPU only if measured

**Context.** Per-CPU run queues with migration are complex and easy to get wrong; most target core counts are 2 to 4.

**Decision.** First SMP implementation uses a single global ready structure protected by a scheduler spinlock, with affinity masks. Per-CPU structures are adopted only when benchmarks on real multicore targets show the lock as a bottleneck.

**Alternatives.** Per-CPU from the start (more code, more races, little benefit at 2 to 4 cores).

**Consequences.** Faster to correctness; the class interface (KRN-SCH-038) keeps the upgrade path open.

**Status.** Proposed.

## ADR-013 Executable reference model as the semantic oracle

**Context.** Requirements in prose cannot be executed; tests written from prose encode the test author's reading.

**Decision.** A small model of scheduler, wait protocol, inheritance, and timeouts implements the requirements directly. Differential tests compare the model and the kernel on randomized sequences. The wake-race protocol is explored exhaustively for small cases.

**Alternatives.** Prose plus hand-written tests only (status quo); full formal verification (not proportionate before 1.0).

**Consequences.** Semantic changes are tried in the model first; a readable artifact for reviewers; modest maintenance cost that pays back on every refactor.

**Status.** Proposed.

## ADR-014 Public handles are capabilities

**Context.** v0.1 wanted opaque handles, optional permissions, stale-handle detection, and future userspace. These are one mechanism.

**Decision.** A handle is a capability: object reference plus rights. Representation per profile: pointer (tiny), tagged pointer with generation (base), validated table index (isolated). Rights reduce on derivation; revocation is by generation bump.

**Alternatives.** Raw pointers forever (no isolation path); separate permission objects (two mechanisms to validate).

**Consequences.** Zero cost on tiny targets; isolation and AMP reuse the same API; capability tables are static per partition in 1.0.

**Status.** Proposed.

## ADR-015 Partition as the single isolation and fault-containment unit

**Context.** v0.1's userspace was defined only as unprivileged threads. Isolation, time partitioning, and restartability need one owner.

**Decision.** The partition owns threads, regions, capabilities, optional budget or schedule window, fault policy, and devices. One partition exists on unprotected targets and compiles to nothing. A supervisor partition applies fault policy.

**Alternatives.** Zephyr-style memory domains plus separate userspace flag (two concepts); process model with dynamic loading (not an RTOS goal).

**Consequences.** Mixed-criticality freedom-from-interference primitives exist from the architecture up; drivers can be placed in or out of the kernel partition without source changes.

**Status.** Proposed.

## ADR-016 Scheduling classes with fixed priority mandatory; budgets instead of aging

**Context.** Fixed priority is LOCKED. Starvation handling by aging was rejected. Modern systems still need to contain runaway threads and optionally schedule by deadline or by table.

**Decision.** A class interface over the ready structure: FIXED_PRIORITY (mandatory, only class in 1.0), DEADLINE and TIME_TABLE (FUTURE), IDLE. Temporal protection by per-thread or per-partition budgets with NOTIFY, DEMOTE, SUSPEND, FAULT policies. Deadline-miss monitoring as a lighter option.

**Alternatives.** Fixed priority forever (limits mixed-criticality use); aging (rejected, weakens determinism).

**Consequences.** Zero overhead when only fixed priority is configured (KRN-SCH-040); CPU accounting becomes a kernel option; certifiable time partitioning has a home.

**Status.** Proposed.

## ADR-017 64-bit monotonic time; 32-bit profile for the smallest targets

**Context.** Width and unit were open.

**Decision.** 64-bit tick counter for base and above with a configurable tick unit; 32-bit option for tiny with wrap-safe comparison; typed time API with absolute variants.

**Alternatives.** 32-bit everywhere (wrap handling complexity on every target, 49-day class bugs); 64-bit everywhere (cost on AVR).

**Consequences.** Simple arithmetic on 32-bit and larger cores; tiny profile keeps the wrap-safe discipline v0.1 already required.

**Status.** Proposed.

## ADR-018 Acquire/release guarantees at every kernel synchronization point

**Context.** Not stated in v0.1.

**Decision.** Acquisition operations are at least acquire; release operations are at least release; context switch is a full barrier; identical on UP and SMP.

**Alternatives.** Rely on interrupt masking on UP and fix later (guaranteed breakage at SMP).

**Consequences.** Trivial cost on UP; code written for AVR remains correct on SMP.

**Status.** Proposed.

## ADR-019 Software timer callbacks run on a work queue

**Context.** Timer callback context was open; ISR-context callbacks are a classic source of bugs.

**Decision.** Default is the system work queue (thread context). `ISR_CONTEXT` is opt-in for callbacks declared ISR-safe.

**Alternatives.** Always ISR context (small, dangerous); dedicated timer thread (that is the work queue).

**Consequences.** Callback latency is bounded by the work queue's priority and load, which is documented; the tiny profile may use ISR-context timers when no work queue is configured.

**Status.** Proposed.

## ADR-020 Destroying an object with waiters is a fault unless `ABORT_WAITERS` was requested

**Context.** Undefined in v0.1.

**Decision.** Checked builds fault; release builds return `EMB_EBUSY`; objects created with `ABORT_WAITERS` wake waiters with `DESTROYED` and bump the generation.

**Alternatives.** Always abort waiters (hides lifecycle bugs); always refuse (prevents legitimate teardown patterns).

**Consequences.** Lifecycle bugs are caught; deliberate teardown is explicit.

**Status.** Proposed.

## ADR-021 Common Trace Format for kernel trace

**Context.** Trace needs a viewer; proprietary formats tie the project to one tool.

**Decision.** CTF with generated metadata. Trace Compass and Babeltrace read it directly; Perfetto through conversion; EmbDebug reads the same stream.

**Alternatives.** SystemView or Tracealyzer formats (vendor-specific); custom format with custom viewer (tooling burden).

**Consequences.** Zero viewer development to be useful; the metadata is generated from the trace point definitions.

**Status.** Proposed.

## ADR-022 AMP through ports with a shared-memory transport

**Context.** Heterogeneous and asymmetric multicore MCUs are common; SMP alone does not cover them.

**Decision.** Per-core images share the hardware description. Inter-core messaging uses ports with queue semantics over shared-memory rings and doorbell or mailbox peripherals described in the hardware model. rpmsg compatibility is an optional transport.

**Alternatives.** OpenAMP as the native model (Linux-centric, heavier); ad hoc mailbox code per SoC.

**Consequences.** Intra-partition, inter-partition, and inter-core messaging share one API; boot manifests describe multiple images.

**Status.** Proposed.

## ADR-023 C11 minimum, freestanding kernel, C++17 optional wrappers

**Context.** Language level affects EmbCC feature requirements and portability.

**Decision.** C11 minimum, C17 preferred, freestanding kernel; atomics wrapped by the compiler portability layer; C++17 wrappers with exceptions and RTTI off by default.

**Alternatives.** C99 (no static assertions, no alignment specifiers, no atomics); C23 (toolchain availability on all three compilers uncertain for AVR).

**Consequences.** EmbCC must support C11 features used by the kernel; a defined list of used features is maintained.

**Status.** Proposed.

## ADR-024 Licence and contribution model: Apache-2.0 with the Developer Certificate of Origin

**Context.** EmbLinkRTOS is linked statically into commercial firmware, is one component of the larger EmbLink platform, and will accept code from outside contributors.

**Decision.** Apache-2.0 for kernel, ports, drivers, tools, hardware descriptions, and generated code. Contributions are accepted under the Developer Certificate of Origin (DCO 1.1) with a `Signed-off-by` line on every commit; inbound licence equals outbound licence; no contributor licence agreement. SPDX identifiers in every source file. A `NOTICE` file names the copyright holder and reserves the EmbLink names. `CODEOWNERS` per layer, starting with the project owner for everything. The core kernel remains written and understood by the project owner (v0.1 §1.2); outside contributions are expected first in ports, SoC and board descriptions, drivers, tooling, tests, and documentation.

**Alternatives.** MIT (simpler, no patent grant; chosen by FreeRTOS and ThreadX). A contributor licence agreement (keeps the option of relicensing or dual licensing, but deters individual contributors and adds administration). Copyleft licences (block product adoption; LGPL is impractical for static linking).

**Consequences.** The explicit patent grant and the trademark exclusion protect adopters and the EmbLink names. Middleware under Apache-2.0, BSD, and MIT combines cleanly. Because contributions arrive under the DCO and not a CLA, contributed code cannot later be relicensed or dual-licensed without each contributor's consent; this is a deliberate choice for an open project and is only reversible before the first external contribution is merged. Hardware importers must take data only from permissively licensed sources.

**Status.** Accepted 2026-10-06.

## ADR-025 First Cortex-M targets: STM32F4 for the port, RP2350 for Armv8-M, RISC-V, and multicore

**Context.** Milestone 3 needs a board on which to develop the Cortex-M port, and a board with Armv8-M protection on which to prove partitions. Later milestones need RISC-V and multicore hardware.

**Decision.** Develop the Cortex-M port on the STM32F4 family (STM32F407 Discovery or NUCLEO-F446RE): the canonical Armv7E-M implementation with FPU, DMA, and the classic MPU, with Renode platform support and the largest community and SVD corpus. Acquire the Raspberry Pi Pico 2 (RP2350) at the same time: dual Cortex-M33 with FPU, MPU, and TrustZone, each core alternatively bootable as a Hazard3 RISC-V core. The RP2350 is the Armv8-M isolation board in M3, the first RISC-V board in M5, and the SMP and AMP board in M6. An industrial Armv8-M family (STM32U5 or STM32H5 class, with CAN-FD and Ethernet) is the planned second SoC family in M4.

**Alternatives.** RP2350 alone (cheapest and covers three ports, but unconventional peripherals, a bootrom-centric boot flow, and weaker industrial representativeness). STM32U5 or H5 first (closest to shipping products, but weaker emulator support and more complex flash and option-byte handling).

**Consequences.** The port is learned on the simplest and best-documented Cortex-M. Memory protection is implemented first on the Armv8-M model, whose regions use base and limit addresses, then on the Armv7-M model, whose regions must be power-of-two sized and aligned. One inexpensive board de-risks three later milestones and gives EmbCC both of its existing architectures on one target.

**Status.** Accepted 2026-10-06.

## ADR-026 Wait protocol: three-state wait flag with a wait generation and a superseded in-flight timeout

**Context.** KRN-WAIT-004 to 007 require that exactly one of wake, timeout, cancel, and destroy wins for a blocked thread. R-001 §4.2 found three ways kernels achieve this: one lock held across everything (uC/OS-III, NuttX, ChibiOS, Zephyr's scheduler spinlock), which makes the longest masked section proportional to list length; replay under a side lock (FreeRTOS), which doubles the code paths; or an idempotent state machine (RTEMS's READY / INTEND_TO_BLOCK / BLOCKED wait flags changed by compare-and-swap, ThreadX's suspension sequence number), which needs no lock across the block window. Zephyr adds a superseded bit on an in-flight timeout so that cancelling a timeout whose handler runs on another CPU is safe.

**Decision.** Every thread carries `wait_state` in {READY, INTEND_TO_BLOCK, BLOCKED} and a `wait_generation`. Blocking: under the object's lock (a critical section on uniprocessor, the object's spinlock on SMP) check the condition, enqueue the wait node by policy, set INTEND_TO_BLOCK and capture the generation, release the object lock; arm the timeout under the timeout lock with that generation; then compare-and-swap INTEND_TO_BLOCK to BLOCKED and switch. If the swap fails a waker already won: disarm the timeout and return the recorded result without switching. Waking: under the object lock dequeue the node, write the wake result, then swap INTEND_TO_BLOCK to READY, or, if BLOCKED, set READY and make the thread ready; a second wake source finds neither state and does nothing. The timeout handler compares the generation it was armed with against the thread's current one and does nothing on mismatch. On SMP, cancelling a timeout whose handler is running marks it superseded and retries; the handler checks the bit before acting. On cores without compare-and-swap (AVR) the swap is a short critical section.

**Alternatives.** One lock across everything: simplest, unbounded masked time, a global lock on SMP. FreeRTOS-style replay lists: two code paths and tick-delayed wakes.

**Consequences.** No masked section spans the block window; the remaining masked sections are single list operations whose bound is stated (priority-ordered insert, bounded by the waiters of one object; timeout insert, bounded by armed timeouts, with the wheel of ADR-010 as the escape). SMP needs no global lock for blocking. The thread control block gains one byte of state and one or two bytes of generation. The reference model expresses exactly these states, which is what KRN-WAIT-007 asks for. KRN-WAIT-008 and 009 record this.

**Status.** Proposed (R-003).

## ADR-027 Multi-object wait by binding objects to notification bits

**Context.** R-001 §4.3: FreeRTOS queue sets copy a handle into a container queue on every post; Zephyr's `k_poll` keeps per-object poller lists under one global lock and supports only one mode; uC/OS-III removed pend-multi in favour of per-task primitives. ChibiOS events, RIOT thread flags, Hubris notifications, and FreeRTOS stream buffers all converge on per-thread bits set by other mechanisms.

**Decision.** Any waitable object may be bound to one `(thread, notification bit)` pair with `emb_<object>_bind_notify()`. Whenever the object becomes ready for its bound operation (a queue gains a message, a semaphore becomes available, an event condition becomes true, a stream reaches its trigger level), the kernel sets the bit with the ordinary O(1), ISR-safe notification set. A thread that waits on several objects waits once on its notification mask, then performs the non-blocking operation on each signalled object and loops on `EMB_EBUSY` when another consumer was faster. One binding per object in 1.0; rebinding requires the object to have no bound waiter in flight. Threads blocked directly on the object are unaffected.

**Alternatives.** Queue sets (extra copy, sets are queues of handles); a poll object with per-object poller lists (global lock, memory per poller); pend-multi arrays (O(objects) per wait, removed by its own authors).

**Consequences.** Multi-object wait costs nothing per object beyond a thread pointer and a bit index; no new kernel object; works from the tiny profile upward; the notification width must leave bits for the application (KRN-NOTIF-006, answers Q7). Edge semantics (set on every transition to ready, thread re-checks) mean no lost wakeups and at most one spurious pass.

**Status.** Proposed (R-003).

## ADR-028 Priority inheritance algorithm

**Context.** R-001 §5: FreeRTOS raises only the direct holder, disinherits only on the last mutex, and handles timeout disinheritance only when one mutex is held; ThreadX is non-transitive; Zephyr documents deferred priority drop and non-propagated priority down caused by its lock ordering; NuttX is single-level and panics when its holder pool is exhausted. Only RTEMS is correct in every column, at the price of 64-bit priorities and red-black trees. uC/OS-III and ChibiOS show that a small kernel can be transitive and restore correctly with an owned-mutex list, but ChibiOS forces LIFO unlock order and uC/OS-III has an owner-death hand-off bug.

**Decision.** Each thread keeps an intrusive list of the mutexes it owns. Lock, slow path: enqueue priority-ordered; walk the owner chain: for each owner whose effective priority is below the waiter's, raise it and reposition it in its ready queue or wait queue (KRN-WAIT-003), then continue with the mutex that owner is waiting on, up to `CONFIG_EMB_PI_MAX_DEPTH` (default 8); beyond the depth the walk stops and records a trace event (checked builds fault, KRN-SYNC-009); if the walk reaches the caller the result is `EMB_EDEADLK` (checked builds fault). Unlock: remove the mutex from the owned list, recompute the effective priority as the maximum of the base priority and the highest waiter over all still-owned mutexes, and hand the mutex directly to its highest-priority waiter. A waiter's timeout, cancel, or priority change recomputes the owner's effective priority immediately, in the same operation, not deferred to the waiter's resumption. Owner termination follows KRN-SYNC-011 (fault by default, or `EMB_EOWNERDEAD` delivered to the next owner). Any unlock order is allowed. Ceiling mutexes use the same effective-priority recomputation (ADR-030).

**Alternatives.** Priority aggregation trees (RTEMS): correct and general, too heavy for the tiny and base profiles. Non-transitive inheritance (FreeRTOS, ThreadX): fails the nested-mutex cases the conformance suite will contain.

**Consequences.** Per mutex: an owner pointer, two list links, and the wait queue. Per thread: an owned-list head, base and effective priority. The chain walk is O(depth) with each hop a bounded masked section. KRN-SYNC-014 to 016 record the algorithm; R-003 target T9 bounds its cost.

**Status.** Proposed (R-003).

## ADR-029 Temporal protection: sporadic thread budgets, sliding-window partition shares, critical budget; donation FUTURE

**Context.** 03 §2.3 defined budgets as `(capacity, period, policy)`. R-002 §5 and R-001 §1 showed two proven refinements: the sporadic server (POSIX `SCHED_SPORADIC` in NuttX, seL4 MCS scheduling contexts) replenishes consumed time one period after it was consumed, with a bounded refill list, which gives smoother service than a hard reset per period; QNX adaptive partitioning gives each partition a share of CPU over a sliding window and hands unused time to partitions that want it, so the scheduler is plain priority when not overloaded and a partition scheduler when it is, with a separate critical budget for interrupt-driven threads.

**Decision.** Thread budgets are sporadic servers: `(capacity, period, overrun_policy)`; consumed slices are replenished one period after their start; the replenishment list is bounded (`CONFIG_EMB_BUDGET_REFILLS`, default 2) and overflow merges entries. Partition shares are `(share_percent, window, critical_capacity)` over a sliding window (default 100 ms, implemented as a ring of sub-windows); when total demand exceeds the CPU, a partition over its share is eligible only when no partition under its share has runnable threads, implemented as an eligibility filter in the class order next to TIME_TABLE; when the system is not overloaded, ordering is pure fixed priority (KRN-TP-005 holds). A thread marked `EMB_THREAD_CRITICAL` may exceed its partition's share up to `critical_capacity` per window; exhausting that is a partition fault with the usual fault policy. The `FAULT` overrun policy delivers the consumed time in the fault record. Budget donation (a server runs on its client's budget across a Port) is reserved as FUTURE with Ports. All of it compiles out when not configured (KRN-TP-002).

**Alternatives.** Hard periodic budgets only (simplest, bursty service, no idle sharing); time-table scheduling only (ARINC 653, static); CBS under EDF (DEADLINE class, FUTURE).

**Consequences.** Mixed-criticality on an MCU gets the QNX model that no MCU kernel offers; the accounting cost is one timestamp per switch when enabled; the sliding window needs `windows / sub-windows` counters per partition. KRN-TP-006 to 009 record the refinements.

**Status.** Proposed (R-003).

## ADR-030 Compile-time priority ceilings from the static system description

**Context.** KRN-SYNC-012 asks for a ceiling protocol as a per-mutex option. RTIC computes each resource's ceiling at compile time from the declared users and makes locking a priority raise with no waiters and no blocking (R-002 §5). RTEMS implements ceilings as priority nodes at run time.

**Decision.** A ceiling mutex declared in the system description lists the threads (or partitions) that may lock it; the generator computes the ceiling as the highest base priority among them and emits it into the mutex's storage initializer. Locking raises the caller's effective priority to the ceiling immediately (immediate ceiling, as in the Stack Resource Policy); unlocking restores it through the recomputation of ADR-028. In checked builds a locker that is not a declared user, or whose base priority exceeds the ceiling, faults; in release builds the lock fails with `EMB_EPERM`. When every user is declared, the mutex can never have a waiter, so it needs no wait queue; the tiny profile may offer only this mutex form. Runtime-created mutexes may still set a ceiling explicitly. Interrupt-level ceilings (RTIC's `BASEPRI`) are out of scope: thread-level only; sharing with interrupts uses `emb_irq_lock()` or the zero-latency class.

**Alternatives.** Runtime-only ceilings (no generator involvement, no static guarantee); no ceiling protocol (inheritance only).

**Consequences.** Deadlock-free, bounded-inversion locking with O(1) cost for statically described systems; the generator becomes part of the synchronization story; KRN-SYNC-017 records it.

**Status.** Proposed (R-003).

## ADR-031 Preemption threshold: rejected for 1.0

**Context.** ThreadX's preemption threshold lets a thread forbid preemption by priorities between its own and a threshold, reducing context switches (R-001 §1.1, R-002 §5). It costs a second bitmap of preempted priorities and a re-selection path on suspend, and it interacts with priority inheritance (which priority does the threshold follow when the thread is boosted) and with budgets.

**Decision.** Not in 1.0. The two needs it serves are met otherwise: fewer switches among equal-priority threads by time slicing only at or below a threshold level (KRN-SCH-037 revised), and short non-preemptible regions by the scheduler lock. Reconsidered only if the benchmark harness (R-003 §6) shows a switch-count problem on a real workload.

**Alternatives.** Adopt it (ThreadX); adopt meta-IRQ bands instead (Zephyr).

**Consequences.** The scheduler stays a single bitmap plus FIFO queues; inheritance and budgets have one priority to reason about.

**Status.** Proposed (R-003). Rejected for 1.0.

## ADR-032 Generation-tagged thread and partition handles with dead codes; leases for cross-partition buffers

**Context.** 03 §7 already gives objects a generation for stale-handle detection. Hubris extends the idea to tasks: a task id carries a generation, a restart bumps it, and every peer blocked on the old id is woken with a dead code so restarts are visible without polling (R-001 §7). Hubris also replaces large message copies with leases: borrowed buffers validated on every access and revoked automatically when the lender resumes.

**Decision.** In every profile that checks generations (tagged pointers in `base`, capability tables in `isolated`), thread and partition handles carry a generation; a partition restart bumps the generation of the partition and of its threads; any thread blocked on such a handle (join, port request, notification binding) completes with `EMB_ESTALE`. For Ports (03 §6.6, FUTURE for 1.0), a message may carry up to `CONFIG_EMB_PORT_MAX_LEASES` lease descriptors `(base, length, rights)` over the client's memory; the server accesses leased memory only through kernel calls that validate against the client's regions; a lease is valid only while the client is blocked in that request and is revoked implicitly when the client resumes for any reason. Messages up to the copy threshold (default 64 bytes) are copied; larger buffers are leased.

**Alternatives.** Kernel copies for everything (bounded message size or large kernel buffers); shared-memory regions set up by the application (no revocation, no validation).

**Consequences.** Restart semantics are visible and testable; cross-partition zero-copy without a kernel buffer; the validation cost is per access, which is why small messages are still copied. KRN-OBJ-005 and KRN-IPC-009 record it.

**Status.** Proposed (R-003).

## ADR-033 Partition-local kernel storage

**Context.** Every isolating kernel must keep state about each partition (thread control blocks, wait nodes, capability tables, object headers). Tock carves that state ("grants") out of the process's own memory so that it is freed with the process and so that a process cannot exhaust kernel memory (R-001 §7). No C kernel does this.

**Decision.** In isolated profiles each partition's region set includes one kernel-owned sub-region, sized by the generator from the partition's declared threads and objects. The kernel state describing the partition, and the storage of the kernel objects the partition declares (ADR-006 storage types), live in that sub-region. It is accessible only in privileged mode, is re-initialized on partition restart, and is never grown at run time (KRN-PART-006). The kernel partition's own state lives in kernel RAM as before.

**Alternatives.** All kernel state in kernel RAM (restart must find and release it; a partition's demand is invisible in its own budget); dynamic grants (Tock; conflicts with the static-partition rule).

**Consequences.** Per-partition kernel memory is a visible number in the generated layout; restart is a re-initialization of one region; no kernel allocator is needed for partitions; the MPU region count per partition rises by one, which the generator checks against the hardware. KRN-PART-007 records it.

**Status.** Proposed (R-003).

## ADR-034 Worst-case monitors with caller address as part of the observability baseline

**Context.** SPEC-002 promises latency bounds, and R-003 target T7 bounds the longest interrupt-masked section. R-001 §8 found that only NuttX's critmonitor and uC/OS-III's interrupt-disable measurement record worst cases at run time, and only NuttX keeps the address of the code responsible.

**Decision.** As compile-time options, the kernel records the longest interrupt-masked interval, the longest scheduler-locked interval, the longest ISR per vector, and the longest uninterrupted run per thread, each with the program counter of the code that opened the interval, per CPU and per thread where applicable. A configurable threshold per category raises a trace event, a notification to a supervisor, or a fault. The state is exported through the debug descriptor (ADR-011) and the statistics API.

**Alternatives.** Averages only (hide the outliers that break deadlines); external measurement only (not available in the field).

**Consequences.** The bounds the project promises are self-checking on every target in every build that enables them; the cost is one timestamp read at each interval boundary when enabled and nothing otherwise. OBS-009 to 011 record it.

**Status.** Proposed (R-003).

## ADR-035 Checked-build architecture: transition checker, separable validation layer, generator consistency checks, safety gate

**Context.** SPEC-001 rules that misuse is a fault in checked builds and a status in release builds. R-001 §2.3 and §9 found the two strongest designs for this: ChibiOS's runtime state machine over `isr_cnt` and `lock_cnt` that halts on any illegal transition, and ThreadX's `txe_` layer whose removal is a name mapping; and the two strongest configuration guards: ChibiOS's `chchecks.h` (errors on any missing symbol or version mismatch) and ThreadX's `TX_SAFETY_CRITICAL` (rejects unsafe option combinations).

**Decision.** (1) Context-class checking is a transition checker over the per-CPU `irq_nesting_depth`, `irq_lock_depth`, and `sched_lock_depth` of SPEC-002 with an explicit table of legal transitions; an illegal transition is a kernel fault in checked builds. (2) All argument, context, and state validation lives in a separable layer that the kernel core never calls and that release builds do not compile; the public names are mapped to the checked or the direct entry by macro. (3) The generator emits a configuration consistency check that fails the build on a missing required symbol, a configuration-version mismatch, or a violated profile constraint. (4) A profile marked `safety` rejects option combinations that remove checks, run timer callbacks in interrupt context, permit creation after `emb_system_freeze()`, or disable stack protection.

**Alternatives.** Independent asserts scattered through the core (hard to compile out completely, no transition semantics); configuration defaults with `#ifndef` (FreeRTOS: silent on omissions).

**Consequences.** Release builds carry no check code by construction; checked builds catch context misuse at the transition, not at the next symptom; configurations cannot be silently incomplete. BLD-007 and 008 record the build side.

**Status.** Proposed (R-003).

## ADR-036 Tiny profile scheduler: priority-indexed thread table and optional idle thread

**Context.** ChibiOS NIL shows that a kernel for a handful of threads needs no ready list at all: the thread array is indexed by priority and the scheduler scans for the first ready entry (R-001 §1.1). uC/OS-III 3.08, ChibiOS, and RIOT make the idle thread optional and idle inside the scheduler. Both save RAM that matters on a 2 KB AVR. ChibiOS pays for this with two kernels; the class interface (KRN-SCH-038) lets EmbLinkRTOS do it with one.

**Decision.** The tiny profile may select `CONFIG_EMB_SCHED_TABLE`: threads are a fixed array indexed by priority, priorities are unique, the ready set is one bitmap word, selection is count-leading-zeros or a scan, and a wait queue is a bitmap of threads (so priority order is free and wake-highest is O(1)). Up to 16 threads including the idle slot. `CONFIG_EMB_IDLE_THREAD=n` removes the idle thread; the scheduler idles inline with interrupts enabled at the SPEC-002 preemption points. The public API and the conformance suite are unchanged within the tiny profile's documented restrictions (unique priorities, no round robin).

**Alternatives.** One bitmap scheduler for every profile (simplest, costs a list per priority on the tiny targets); a second kernel (ChibiOS).

**Consequences.** TCB target of 32 bytes on AVR (R-003 T2); wait queues of one or two bytes; one more implementation of the class interface to test, which the reference model already covers. KRN-SCH-042 and 043 record it.

**Status.** Proposed (R-003).

## ADR-037 CMSIS-RTOS2 and FreeRTOS-API adapters as optional layers

**Context.** R-003 §5: the two reasons a team would still not switch are middleware written against another kernel API and the migration cost of existing code. Most vendor middleware targets CMSIS-RTOS2 or the FreeRTOS API; ESP-IDF demonstrates a FreeRTOS API served by another kernel.

**Decision.** In M4, optional `compat/cmsis_rtos2/` and `compat/freertos/` layers over the native API, each documenting where semantics differ (inheritance, timer control, `FromISR` mapping to context classes). Adapters never influence native API design and are validated with the upstream validation suites where they exist. They are not part of the certified profile.

**Alternatives.** Native API only (purer, slower adoption); make one of these the native API (inherits their semantic flaws, R-001 §5).

**Consequences.** Existing middleware and applications run while the native API stays clean; two more test targets; a migration guide becomes a deliverable.

**Status.** Proposed (R-003).
