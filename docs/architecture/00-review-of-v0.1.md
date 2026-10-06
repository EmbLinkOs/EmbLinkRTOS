# 00 - Review of the v0.1 Specification

**Status:** Review record. Input to v0.2.
**Scope:** What v0.1 gets right, where it is incomplete for a modern production RTOS, and what v0.2 changes.

---

## 1. Verdict

v0.1 is an unusually disciplined starting point. Most RTOS projects begin with a context switch and discover semantics later. v0.1 begins with semantics, invariants, requirement identifiers, and an honest LOCKED / PLANNED / FUTURE split. That foundation is kept intact in v0.2.

The gaps are not in the kernel core. They are in the parts that turn a kernel into a *platform* that a product team would trust in 2026 and beyond:

| Area | v0.1 state | Why it matters now |
|---|---|---|
| Isolation and fault containment | "Userspace" marked FUTURE, defined only as privileged vs unprivileged threads | Mixed-criticality, security, and restartable subsystems all need one isolation concept designed in from the start, even if it compiles to nothing on AVR |
| Handle and permission model | Opaque handles, permissions "optional" | A capability model costs nothing on tiny targets and makes userspace, AMP, and security tractable later |
| Scheduling beyond fixed priority | Fixed priority only; aging rejected | Correct rejection of aging, but modern real-time needs *temporal protection* (budgets, deadline monitoring) and optionally deadline or table-driven classes |
| ISR-to-thread fast path and deferred work | Not defined; timer callback context left open | Notifications and work queues are the two most used primitives in modern RTOS code and define the driver model |
| Hardware description | "Study DeviceTree, SVD, etc." | The hardware description pipeline is the single biggest determinant of how boards scale. It must be decided before the driver framework |
| Observability | Logging and tracing described generically | Deferred-format logging, binary trace in a standard format, retained crash records, and a versioned debug descriptor are what make a kernel debuggable in production |
| Security lifecycle | Good list of topics; no threat model, no image format, no supply-chain story | Production claims in 2026 and later imply regulatory expectations (for example the EU Cyber Resilience Act). SBOM, signed images, disclosure process, and reproducible builds are table stakes |
| Multicore | SMP only | Most multicore MCUs shipped today are heterogeneous or asymmetric (Cortex-M7 + M4, Arm + RISC-V). AMP must be a peer of SMP |
| Host simulation | "Eventually" | A native port is the fastest path to correctness and should be built in parallel with the first hardware port, not after it |
| Verification method | Good test taxonomy | Missing the thing that makes the taxonomy pay off: an executable reference model of scheduler and wait semantics that tests are checked against |
| API conventions | Prefix and names open | Status codes, timeout types, ISR-safety convention, naming, and the C standard level shape every header. They must be fixed before the first API |
| Concurrency and memory model | Not stated | The kernel must state what memory ordering its synchronization points guarantee, or SMP and compiler optimizations will break code that "worked on AVR" |

---

## 2. What v0.1 gets right and v0.2 keeps unchanged

These remain **LOCKED**. v0.2 does not reopen them.

1. Specification before mechanism; requirement identifiers; traceability chain.
2. Thread states READY / RUNNING / BLOCKED / TERMINATED with wait *reasons* rather than extra states.
3. Fixed-priority preemptive scheduling as the mandatory baseline, FIFO among equals, optional round-robin, no automatic aging.
4. Base priority plus effective priority; larger number is higher priority; priority 0 is the idle class.
5. Yield never runs a lower-priority thread.
6. Preemption preserves FIFO position of the preempted thread.
7. Scheduler locking is nestable, does not mask interrupts, and is not a synchronization primitive.
8. Scheduling is event driven; the tick is one event source; tickless is a first-class mode.
9. One unified wait mechanism for every blocking primitive.
10. ISRs never block; ISR-safe operations are explicitly identified.
11. RUNNING thread is tracked per CPU, not in the ready queue; per-CPU state exists conceptually even on uniprocessor builds.
12. Intrusive, statically allocated kernel data structures; no dynamic allocation on real-time paths.
13. Opaque public handles; internal layout is not ABI.
14. Architecture -> SoC -> board -> device instance layering; no per-board kernel copies.
15. EmbCC first-class, never mandatory; GCC and Clang are architectural requirements.
16. C-compatible public ABI; C++ wraps it.
17. Real-time claims require published evidence with hardware, toolchain, and configuration metadata.
18. AVR / ATmega328P as the constraint-exposing first hardware target, then Cortex-M, then RISC-V, then multicore.
19. Vertical milestones; do not build everything before the scheduler runs.
20. Never advertise safety certification that has not been achieved.

---

## 3. Specific issues found in v0.1

### 3.1 Semantic gaps in the kernel sections

- **Created-but-not-started and suspended threads are not placed in the state model.** v0.2 models both as BLOCKED with the wait reasons `START` and `SUSPEND`, keeping the four-state model intact. Join, cancel, and destroy semantics are then defined against the same wait model.
- **Wait ordering is left per object.** v0.2 fixes a default: wake the highest effective priority waiter, FIFO among equals, with an optional per-object pure-FIFO mode. This matters for priority inheritance correctness and for determinism claims.
- **Priority inheritance algorithm is open.** v0.2 specifies transitive inheritance with bounded depth, recomputation on unlock from the set of still-owned mutexes, and defines behavior under priority change while blocked. It also records the decision that priority ceiling is an optional protocol on the same mutex object.
- **Object destruction with waiters is undefined.** v0.2 defines the rule: destruction while waiters exist is a kernel fault in checked builds unless the object was created with the `ABORT_WAITERS` policy, in which case waiters wake with `EMB_EDESTROYED`.
- **Time width and unit are open.** v0.2 fixes 64-bit monotonic ticks internally with a configurable tick unit, a 32-bit profile for the smallest targets using wrap-safe arithmetic, and adds absolute-deadline APIs so periodic threads do not drift.
- **Timer callback context is open.** v0.2 decides: software timer callbacks run on a kernel work queue by default; an explicit `ISR_CONTEXT` flag is allowed only for callbacks that are documented ISR-safe.
- **Memory ordering is unspecified.** v0.2 states that every kernel synchronization operation is at least acquire on acquisition and release on release, and that this holds on uniprocessor builds too so that code does not silently depend on interrupt masking.

### 3.2 Structural gaps

- **No single isolation concept.** v0.2 introduces the **Partition** as the unit of space isolation (memory regions plus capability table), optional time isolation (CPU budget or schedule window), and fault containment (restart policy). On targets without an MPU there is exactly one partition and the concept compiles to nothing.
- **No fast-path ISR-to-thread primitive.** v0.2 adds **Notifications**: a per-thread set of event bits settable from any context and waited on by the owner. No object allocation, no queue, bounded.
- **No deferred execution service.** v0.2 adds **Work queues** as a kernel service, used by software timers, driver bottom halves, and applications.
- **Driver contracts are blocking-centric.** v0.2 defines drivers as request/completion state machines with synchronous wrappers, so the same driver works with DMA, from ISR completion, inside an isolated partition, or across cores.
- **Hardware description is undecided.** v0.2 decides on a project-owned, schema-validated YAML hardware description with a generator that emits C tables, linker fragments, and configuration defaults, and that can *import* from CMSIS-SVD and DeviceTree rather than requiring authors to write them.
- **Observability has no concrete design.** v0.2 specifies deferred-format logging, a binary trace stream in Common Trace Format, a retained-RAM crash record, a flight recorder, and a versioned kernel debug descriptor for RTOS-aware debuggers.
- **Security has no threat model and no image format decision.** v0.2 adds a threat model section, decides on an MCUboot-compatible image format and manifest as the default, specifies a PSA-shaped crypto and attestation API surface, and adds supply-chain requirements (SBOM, pinned toolchains, signed releases, disclosure policy).
- **Multicore considers SMP only.** v0.2 adds an AMP model where each core runs its own image and cores communicate through **Ports**, which use the same handle and IPC semantics as intra-image IPC.
- **Host simulation is late.** v0.2 moves the native port into Milestone 1 alongside AVR. Two architecture ports from day one is the only reliable way to keep the port contract honest.
- **No reference model.** v0.2 requires an executable reference model of scheduler and wait semantics, used as a test oracle for differential and property-based testing.

### 3.3 Program and risk observations

- **35 numbered program items with no 1.0 boundary.** v0.2 defines exactly what 1.0 contains and what is explicitly excluded.
- **AVR-first can bias widths and layouts toward 8-bit.** Mitigation: the native port and the Cortex-M port exist early enough to catch it, and all widths are configuration, not law.
- **Naming.** The public prefix is still open. v0.2 recommends keeping `emb_` for the public API, `emb_arch_` for the port contract, and `embk_` for kernel-private symbols. Avoid any `embos` spelling: embOS is an existing commercial RTOS trademark.
- **No licence, governance, or contribution model.** v0.2 lists this as an open decision with a recommendation.

---

## 4. Reading guide for v0.2

| Document | Purpose |
|---|---|
| `01-vision-and-principles.md` | Mission, principles, non-goals, positioning against existing RTOSes, what "modern" means here |
| `02-system-architecture.md` | The complete layered architecture, concept vocabulary, and dependency rules |
| `03-kernel-architecture.md` | Execution model, scheduling classes, wait model, time, synchronization, IPC, objects and capabilities, partitions, memory, faults |
| `04-platform-architecture.md` | Hardware description, device model, drivers, power, boot and update, security, observability, multicore, native simulation |
| `05-engineering-system.md` | API conventions, configuration and build, verification, quality gates, release engineering, support levels, repository layout, documentation |
| `06-decision-records.md` | Architecture Decision Records: each major v0.2 choice with alternatives and consequences |
| `07-roadmap.md` | Revised milestones, 1.0 definition, and specification work order |
| `08-open-questions.md` | Decisions that need your call, each with a recommendation |

Status markers used throughout v0.2:

- **LOCKED**: established in v0.1 or confirmed here; not reopened without an ADR.
- **PROPOSED**: new in v0.2; recommended and ready to lock after your review.
- **PLANNED**: required capability, detailed design still to come.
- **FUTURE**: must not be precluded; not needed for 1.0.
