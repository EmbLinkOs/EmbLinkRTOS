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
