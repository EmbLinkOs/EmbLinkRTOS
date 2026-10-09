# R-003 - Differentiation: Why Someone Would Choose EmbLinkRTOS

**Status:** Research record, 2026-10-07. Its decisions are ADR-026 to ADR-037 in `docs/architecture/06-decision-records.md`, **accepted 2026-10-07** by the project owner with the answers in §9; this document holds the reasoning and the measurable claims behind them.
**Inputs:** `R-001` (mechanism comparison of eleven kernels), `R-002` (market, performance, certification, regulation), the v0.2 architecture (`docs/architecture/`), and the accepted specifications SPEC-001 to SPEC-003.
**Rule:** Every reason to choose EmbLinkRTOS stated here must be either measurable on a board with the harness in §6 or demonstrable by a conformance test. Claims that cannot be verified are not made.

---

## 1. The thesis

The market is a duopoly (R-002 §1): FreeRTOS, small and simple, with thin semantics and weak correctness in exactly the places that hurt in production (R-001 §5, §7); and Zephyr, broad and well-tooled, whose own community lists bloat, poor real-time performance, configuration complexity, and Devicetree "macrobatics" as the reasons people hesitate (R-002 §1). ThreadX is the certified option but is FIFO-by-default, non-transitive in inheritance, thin on isolation, and its public struct layouts are its ABI (R-001 §5, §7, §9). Everything else is in the low single digits of adoption.

EmbLinkRTOS's reason to exist is the empty space between them, defined by nine claims. Each is numbered so that the roadmap, the harness, and the release notes can refer to it.

| # | Claim | How a user checks it |
|---|---|---|
| C1 | **Semantics you can read and test.** Every behavior the kernel promises is a numbered requirement with a verification method, checked by a conformance suite that runs unchanged on every port, with an executable reference model as the oracle | `docs/requirements/`, the traceability matrix, the conformance results per target in each release |
| C2 | **The fastest small kernel on the same board, shown by a public harness.** The same operations, on the same board, with the same compiler and options, for FreeRTOS, Zephyr, ThreadX, and EmbLinkRTOS, with distributions and worst cases, published every release | §6 harness; raw data in the release |
| C3 | **One kernel from a 2 KB AVR to an isolated multicore SoC.** One API, one source tree, one conformance suite; features compile to nothing where not configured | the tiny and isolated reference configurations (03 §11) both pass the same suite |
| C4 | **Synchronization that is actually correct.** Transitive inheritance with correct multi-mutex restore, disinheritance on timeout, deadlock detection, owner-death handling, no `FromISR` twins, bounded interrupt-masked time | KRN-SYNC and KRN-WAIT tests; the masked-time bound measured by the harness |
| C5 | **Isolation designed in, in C.** Partitions, capabilities with rights, generation-tagged handles, supervisor-driven restart without reboot, partition-local kernel storage | the isolated reference configuration; fault-injection tests in the conformance suite |
| C6 | **Time done right.** 64-bit monotonic time, tickless, drift-free periodic timers, synchronous timer control, thread-context callbacks, overhead-compensated arming | KRN-TIM tests; timer accuracy measured by the harness |
| C7 | **Observability out of the box.** CTF trace, deferred-format logging, retained crash record, versioned debug descriptor, worst-case monitors, all compile-out | OBS tests; a field crash diagnosed from the record alone (01 §6) |
| C8 | **Hardware as data without macrobatics.** A readable YAML schema with SVD and Devicetree importers and a generator that emits tables, linker inputs, and syscall stubs | a new board on a supported SoC is a description file plus a board directory (01 §6) |
| C9 | **Evidence as a by-product.** Requirements, traceability, coverage, coding-rule compliance, and benchmarks are produced by development, not retrofitted, so a certification kit is affordable when the time comes | 05 §4 to §6 artifacts in every release; no certification claimed until achieved |

The honest counter-list is in §5.

## 2. The complaint checklist

R-002 §1 and the weakness sections of R-001 give a list of what users dislike. Each line is answered by a decision that already exists or is proposed here.

| Complaint (source) | EmbLinkRTOS answer |
|---|---|
| "Bloated and poor real-time performance" (Zephyr roast) | C2 and C3: profiles that compile features out; the harness proves the numbers; tiny profile table scheduler (ADR-036) |
| "I already have a vendor HAL, why more abstraction" (Zephyr roast) | the request/completion driver model (ADR-007) is thin and drivers may wrap a vendor HAL; the hardware description generates register tables, not a mandatory HAL |
| "Devicetree is so complex, macrobatics" (Zephyr roast) | ADR-004: own YAML schema, generator emits plain C tables; Devicetree is an *import* format |
| "What is west and why must I learn Python" (Zephyr roast) | ADR-005: CMake is the reference build; EmbBuild manifests later; no mandatory meta-tool |
| `FromISR` API duplication (FreeRTOS, R-001 §2.3) | ADR-003: one API with context classes checked at compile and run time |
| Non-transitive inheritance, no owner-death handling, timeout disinherit only with one mutex (FreeRTOS), FIFO waiters by default (ThreadX) | ADR-028 and KRN-WAIT-001 |
| Asynchronous timer control, callbacks share one daemon stack (FreeRTOS); timer wheel drift (ThreadX) | SPEC-003: synchronous stop, work-queue context, drift-free re-arm |
| Public struct layout is the ABI, assembly hard-codes offsets (ThreadX, uC/OS-III) | ADR-006 generated storage types; ADR-011 debug descriptor instead of fixed offsets |
| Isolation retrofitted: 7,000 lines of hand-written wrappers (FreeRTOS), unvalidated user pointers (NuttX), supervisor code fully trusted (Zephyr) | 03 §7 and §8, ADR-014, 015, 032, 033; generated syscall stubs |
| Unbounded interrupt-masked list walks (uC/OS-III, NuttX, ChibiOS) | ADR-026 wait protocol with bounded masking; the bound is a requirement verified per release (§4) |
| Configuration explosion with edge cases reachable only in some permutations (Zephyr) | profiles instead of free permutation; generator consistency checks (ADR-035) |
| No test suite or certificate artifacts in the repository (ThreadX) | C9: every artifact is in the tree or produced by it |
| Certifying after the fact: 15 kLOC scoped out of 2.4 MLOC, tooling invented late (Zephyr) | C9: scope defined by profile from day one |

## 3. Decisions: adopt, avoid, beat

Each row names the kernel the idea comes from, what EmbLinkRTOS does with it, and where it lands. "Adopt" means the mechanism is taken largely as is; "avoid" means the comparison showed a failure mode we design against; "beat" means we commit to a measurable improvement.

### 3.1 Scheduler and interrupts

| Kind | Mechanism | Source | EmbLinkRTOS | Lands in |
|---|---|---|---|---|
| adopt | bitmap plus per-priority FIFO, two-level above the word width | RTEMS, ThreadX, uC/OS-III | already KRN-SCH-036 | 03 §2.1 |
| adopt | table scheduler where priority equals the thread index, no ready list | ChibiOS NIL | tiny profile implementation behind the class interface; unique priorities; up to 16 threads | ADR-036 |
| adopt | optional idle thread; idle inside the scheduler when absent | uC/OS-III 3.08, ChibiOS, RIOT | tiny profile option; saves one stack and TCB | ADR-036 |
| adopt | preempted thread goes *ahead* of its equal-priority peers because its quantum is unspent | ChibiOS, uC/OS-III | KRN-SCH-041 (new) | 03 §2.1 |
| adopt | time slicing applies only at or below a configured priority level | Zephyr `TIMESLICE_PRIORITY` | replaces per-level quanta in KRN-SCH-037: one quantum, one threshold level; answers Q8 | 03 §2.1, 08 |
| adopt | defer the switch, keep the ready structure current under the scheduler lock | ThreadX, RTEMS | already SPEC-002 | none |
| adopt | state checker over `isr_depth` and `lock_depth` halting on illegal transitions in checked builds | ChibiOS `SV#1..11` | KRN-IRQ amendment for SPEC-002: the context-class checker is a transition checker, not a set of independent asserts | ADR-035 |
| adopt | separable validation layer removable by name mapping | ThreadX `txe_` | implementation rule for checked builds: checks live in a layer that release builds do not link | ADR-035 |
| avoid | global recursive spinlock as the SMP interrupt lock | Zephyr legacy `irq_lock`, NuttX `g_cpu_irqlock` | SPEC-002 keeps `emb_irq_lock()` per CPU; SMP gets explicit spinlocks | none |
| avoid | `SWAP_NONATOMIC` workarounds from PendSV below IRQs | Zephyr | SPEC-002 preemption points are defined so that no kernel decision is made between "pend" and "switch"; the PendSV handler re-selects | SPEC-002 |
| beat | a tested bound on the longest interrupt-masked section in any kernel path | nobody (uC/OS-III and NuttX measure it, none bounds it) | §4 target T7; measured by the harness on every release; the monitor of ADR-034 makes it self-checking in the field | §4, ADR-034 |
| decide | preemption threshold | ThreadX | rejected for 1.0: it interacts with inheritance and budgets, costs a second bitmap, and the same reduction in switches is available with a threshold-limited time slice and the scheduler lock. Reconsidered only with harness data | ADR-031 |
| measure | PendSV switch path versus ChibiOS fake-frame path | ChibiOS | the Cortex-M port design measures both on the STM32F4 before freezing | §6 |

### 3.2 Time

| Kind | Mechanism | Source | EmbLinkRTOS | Lands in |
|---|---|---|---|---|
| adopt | 64-bit tick counter, no wrap code | Zephyr, RTEMS, Hubris, Embassy | already SPEC-003 | none |
| adopt | wrap-safe absolute compare for the 32-bit tiny profile | NuttX `clock_compare` | already SPEC-003 | none |
| adopt | superseded bit on the in-flight timeout so cancel racing a running handler is safe | Zephyr | part of the wait protocol | ADR-026 |
| adopt | round-robin and budget deadlines as ordinary timeout entries so slicing is tickless-safe | NuttX | SPEC-003 time-slice hook already uses the timeout list; restated | none |
| adopt | arming-overhead compensation calibrated at boot | RIOT `adjust_set`, `adjust_sleep` | KRN-TIM-036 (new): the arch timer reports its set latency; the kernel subtracts it; calibration at init on tickless targets | 03 §4, SPEC-003 amendment |
| adopt | work queue woken at the next expiry rather than polling | uC/OS-III condvar timer task | implementation of ADR-019 | SPEC-006 |
| avoid | 32-slot timer wheel with re-queue and relative reload | ThreadX | ADR-010 absolute list | none |
| avoid | asynchronous timer control through a command queue | FreeRTOS | SPEC-003 synchronous control | none |
| beat | timer accuracy and sleep accuracy published as a distribution | nobody publishes | §4 target | §6 |

### 3.3 Waiting, synchronization, IPC

| Kind | Mechanism | Source | EmbLinkRTOS | Lands in |
|---|---|---|---|---|
| adopt | three-state wait flag READY / INTEND_TO_BLOCK / BLOCKED changed by CAS, so no lock spans the block window | RTEMS | the SPEC-004 protocol on cores with CAS; a critical-section equivalent on AVR | ADR-026 |
| adopt | suspension sequence number to make late wakes idempotent | ThreadX | the wait generation in the TCB, used by timeout and cancel paths | ADR-026 |
| adopt | wake result set before READY; flush filters that encode the reason | RTEMS, Zephyr `swap_retval` | already KRN-WAIT-005; `emb_wait_result` | none |
| adopt | per-thread notification bits set by objects, so one wait covers several sources | ChibiOS events, RIOT thread_flags, Hubris notifications, FreeRTOS stream buffers | object-to-notification binding: any waitable object may be bound to (thread, bit); state changes set the bit | ADR-027 |
| adopt | owned-mutex list per thread with recomputation over all held mutexes | ThreadX, uC/OS-III, Zephyr | mandatory part of the inheritance algorithm | ADR-028 |
| adopt | transitive chain walk that re-sorts the boosted thread wherever it waits | ChibiOS, uC/OS-III | with a configurable depth bound (KRN-SYNC-009) | ADR-028 |
| adopt | deadlock detection during the chain walk | RTEMS, Zephyr | `EMB_EDEADLK` (kernel-reserved range) in release, fault in checked builds | ADR-028 |
| adopt | `mutex_cancel` as the generic abort for any blocking lock | RIOT | `emb_thread_cancel_wait()` already in the wake-result set (`CANCELED`) | SPEC-004 |
| adopt | lock-free single-producer single-consumer byte stream woken by notifications | FreeRTOS stream buffers | the design of 03 §6.4 pipes | SPEC for IPC |
| adopt | broadcast with lagged-consumer detection | Embassy `PubSubChannel` | an option on event broadcast and streams | SPEC for IPC |
| avoid | LIFO unlock order requirement | ChibiOS | KRN-SYNC-016 (new): any unlock order | ADR-028 |
| avoid | queue sets as queues of handles; global poll lock | FreeRTOS, Zephyr | ADR-027 instead | ADR-027 |
| avoid | five unrelated wait mechanisms | FreeRTOS | one wait protocol under every primitive (03 §3) | SPEC-004 |
| beat | every column of R-001 §5 correct in a small kernel with bounded masking | nobody | ADR-028 plus §4 masked-time target | ADR-028 |

### 3.4 Isolation and partitions

| Kind | Mechanism | Source | EmbLinkRTOS | Lands in |
|---|---|---|---|---|
| adopt | generation-tagged ids with kernel-delivered dead codes after a restart | Hubris | thread and partition handles carry a generation in all profiles that check handles; a wait on a restarted peer returns `EMB_ESTALE` | ADR-032 |
| adopt | leases: borrowed buffers validated on each access and revoked by the blocking state machine | Hubris | the zero-copy mechanism for Ports across partitions (03 §6.6) | ADR-032 |
| adopt | per-partition kernel storage carved from the partition's own memory | Tock grants | isolated profile: object storage, wait nodes, and capability tables of a partition live in its region; restart frees them; user code cannot exhaust kernel memory | ADR-033 |
| adopt | restart rate limiting and fault "mailing list" in the supervisor | Hubris jefe | supervisor partition reference implementation | 03 §8 |
| adopt | generated syscall proxies and stubs | NuttX CSV, Zephyr markers | emitted by the generator from the API annotation tags of SPEC-001 | 04 §1, SPEC-009 |
| adopt | fault policy object outside the kernel | Tock `ProcessFaultPolicy`, Hubris supervisor | already KRN-PART-005 | none |
| adopt | start freeze after which create and destroy are refused | uC/OS-III `OSSafetyCriticalStart` | KRN-OBJ-004 (new): `emb_system_freeze()`; required in the isolated reference configuration | 03 §7.4 |
| avoid | hand-written wrapper layer, O(N) handle scan, stack checks off on MPU ports | FreeRTOS MPU v2 | capability table lookup is O(1); stack limits are part of the partition context | 03 §7.2 |
| avoid | unvalidated user pointers in protected mode | NuttX | KRN-CAP-002 and 03 §8.1 | none |
| beat | capabilities with rights, derivation, grant, and generation revocation on MCU hardware in C | nobody in C | already 03 §7.2; ADR-032 and 033 make it implementable | ADR-032, 033 |

### 3.5 Temporal protection

| Kind | Mechanism | Source | EmbLinkRTOS | Lands in |
|---|---|---|---|---|
| adopt | per-thread sporadic budget with replenishment | NuttX `SCHED_SPORADIC`, seL4 MCS | the `SUSPEND` and `DEMOTE` policies of 03 §2.3 use sporadic-server replenishment, not a hard period reset, with a bounded refill list | ADR-029 |
| adopt | partition budgets as a share over a sliding window, with unused time given to partitions that want it | QNX adaptive partitioning | partition-level temporal protection: under load each partition gets at least its share; without load it behaves as plain fixed priority | ADR-029 |
| adopt | critical budget for interrupt-driven threads | QNX | a thread marked `CRITICAL` may overrun its partition's share up to a separate critical budget; exhausting it is a partition fault | ADR-029 |
| adopt | budget donation: a server thread runs on its client's budget | QNX partition inheritance, seL4 passive servers | FUTURE with Ports: a request carries the client's budget to the serving partition | ADR-029 (FUTURE) |
| adopt | timeout fault with consumed time to a handler | seL4 MCS | the `FAULT` policy delivers consumed time in the fault record | ADR-029 |
| adopt | compile-time priority ceilings from the static thread and resource description | RTIC | KRN-SYNC-012 ceiling mutexes get their ceiling from the generator; locking raises the effective priority, O(1), no waiters by construction when all users are declared | ADR-030 |
| beat | sliding-window partition budgets with idle sharing on an MCU | nobody on MCUs | ADR-029 | ADR-029 |

### 3.6 Observability, configuration, evidence

| Kind | Mechanism | Source | EmbLinkRTOS | Lands in |
|---|---|---|---|---|
| adopt | per-thread worst-case masked time, scheduler-locked time, and run time, with the caller address, and optional panic thresholds | NuttX critmonitor, uC/OS-III | OBS-009 to OBS-011 (new) | ADR-034 |
| adopt | debugger-readable size and configuration constants kept alive | uC/OS-III `OSDbg_*`, ChibiOS `ch_debug` | already ADR-011 (versioned) | none |
| adopt | dictionary logging with strings in a non-loaded section | Zephyr | already ADR-009 | none |
| adopt | inline trace with class filters and object registry | ThreadX TraceX | CTF event set design (work item 14) | SPEC for observability |
| adopt | configuration checked for every symbol and for version | ChibiOS `chchecks.h` | generator emits a configuration consistency check; unsafe combinations rejected in the certified profile | ADR-035 |
| adopt | safety gate rejecting unsafe option combinations | ThreadX `TX_SAFETY_CRITICAL` | same | ADR-035 |
| adopt | real-hardware test logs required, zero-trust review of test claims | NuttX `CONTRIBUTING.md` | CONTRIBUTING rule for ports and drivers | CONTRIBUTING.md |
| adopt | optional API adapters for the two APIs middleware is written against | ESP-IDF (FreeRTOS API on another kernel), CMSIS-RTOS2 vendors | FreeRTOS-API and CMSIS-RTOS2 adapters as optional layers in M4, never the native API | ADR-037 |
| avoid | iterable linker sections for static object registration | Zephyr | EmbCC supports linker scripts only on ARM and RISC-V (09 §6); the generator emits tables instead | none |
| avoid | Kconfig free permutation as the only structure | Zephyr | profiles are the first-class structure; Kconfig options live inside a profile | ADR-005, ADR-035 |
| beat | a vendor-neutral, same-board, same-compiler benchmark harness published with raw data | nobody (R-002 §2) | §6 | TEST-012 (new) |

## 4. Measurable targets

These are design targets, not measurements. Nothing here is claimed until the harness (§6) reports it, and the first measured values become the baseline that later releases may not regress. Reference numbers come from R-002 §2 and are not comparable with each other; the promise is always "measured by our harness under identical conditions".

| Id | Metric | Conditions | Reference point | Target | Claim |
|---|---|---|---|---|---|
| T1 | Kernel text, tiny profile | ATmega328P, EmbCC or avr-gcc `-Os`, scheduler plus notifications plus sleep and timeouts plus one semaphore type, no logging | ThreadX advertises 2 KB minimal on Cortex-M; FreeRTOS minimal builds on AVR are typically 5 to 7 KB | <= 4 KB on AVR; <= 3 KB on Cortex-M0+ | C3 |
| T2 | Kernel static RAM, tiny profile | as T1 | ThreadX 1 KB RAM claim | <= 64 bytes plus <= 32 bytes per thread | C3 |
| T3 | Kernel text, base profile | STM32F407, GCC `-Os`, threads, mutex, semaphore, events, notifications, timers, work queue, message queue, asserts off | Zephyr minimal 7 to 8 KB with multithreading; FreeRTOS comparable builds 6 to 9 KB | <= 8 KB; <= every competitor on the same board with the equivalent feature set | C2, C3 |
| T4 | TCB size, base profile | as T3, 64-bit time, notifications 32 bits | FreeRTOS `TCB_t` about 100 bytes; ThreadX `TX_THREAD` about 200 bytes | <= 96 bytes | C3 |
| T5 | Cooperative context switch | STM32F407 at 168 MHz, GCC `-O2`, asserts off, yield between two threads, DWT cycle counter | FreeRTOS 223 cycles, Zephyr 468 to 524 cycles on an F429 (UL) | <= 200 cycles; <= FreeRTOS measured by the harness | C2 |
| T6 | Interrupt to thread latency | as T5, from the first instruction of the ISR to the first instruction of the woken higher-priority thread, via a notification | not published in comparable form | <= 350 cycles; <= every competitor measured by the harness | C2 |
| T7 | Longest interrupt-masked section in any kernel path | as T5, every kernel path exercised by the conformance suite, measured by the monitor of ADR-034 | nobody bounds it | <= 150 cycles for every O(1) path; the two O(n) paths (priority-ordered wait-queue insert, n = waiters on that object; timeout-list insert, n = armed timeouts) are masked for the walk only, never across the block window (ADR-026), with both n stated per configuration and the wheel of ADR-010 as the escape for the second | C4 |
| T8 | Notification set from ISR to wake | as T5 | FreeRTOS direct-to-task notification is its cheapest wake | O(1), <= 120 cycles for the set, measured | C2 |
| T9 | Mutex lock and unlock, uncontended | as T5 | Zephyr sample 83 cycles lock; ThreadX 0.2 to 0.3 µs class | <= 80 cycles each; inheritance walk of depth k costs O(k) with <= T7 masked per hop | C4 |
| T10 | Sleep accuracy, tickless | as T5, `emb_sleep(EMB_MS(10))`, 10,000 samples | not published | wake within [10 ms, 10 ms + 1 tick + 5 µs]; distribution published | C6 |
| T11 | Periodic timer drift | as T5, 1 ms period, 1 hour | ThreadX drifts by design | zero accumulated drift; overruns counted | C6 |
| T12 | Isolated partition restart | STM32F4 or RP2350 with MPU, fault injected in a partition | Hubris: restart with 5 ms minimum delay by policy | the system keeps meeting T6 for other partitions during the restart; restart completes without reboot; measured time published | C5 |
| T13 | Conformance | every supported target | nobody runs one suite across a 2 KB AVR and an MPU SoC | identical suite passes on tiny and isolated reference configurations | C1, C3 |
| T14 | Evidence | every release | Zephyr: 15 kLOC scope certified after the fact | 100% requirement-to-test traceability for the profile under release; statement and branch coverage reported; MISRA-subset compliance reported | C9 |

## 5. Why someone would still not choose us, and what we do about it

| Reason | Mitigation |
|---|---|
| New and unproven; no production deployments | C1 and C2 replace reputation with evidence; the native port and the reference model let users run the conformance suite themselves on day one |
| No board catalog | ADR-004 importers make a vendor SVD a half-day job; the first targets (ADR-025) are the cheapest and most common boards |
| No middleware (network stacks, file systems, USB) | ADR-037: CMSIS-RTOS2 and FreeRTOS-API adapters let existing middleware run unchanged; native middleware comes later and is not the kernel's job |
| No certification | C9 builds the kit incrementally; no claim before achievement; the certified profile is defined from day one so scope never has to be cut out of a larger code base |
| Migration cost from FreeRTOS | ADR-037 adapter plus a migration guide mapping each FreeRTOS primitive to its EmbLinkRTOS equivalent, including the semantic differences (R-001 §5) |
| Single-maintainer kernel | 05 §10 contribution model; ADR process; the reference model makes kernel proposals reviewable without reading assembly |

## 6. The benchmark harness

A deliverable of M2 (first measurements) and of every release after it. It implements claim C2 and the targets of §4.

**Boards.** NUCLEO-F446RE and STM32F407 Discovery first (ADR-025); RP2350 when its port exists; the native port for functional checks only, never for numbers.

**Kernels.** EmbLinkRTOS, FreeRTOS kernel (latest LTS), Zephyr (latest LTS), ThreadX (latest release); ChibiOS RT added when time permits because its switch path is the fastest alternative design (R-001 §2.1). Each is built from a pinned commit recorded in the metadata.

**Conditions.** Same board, same clock, same compiler and version (GCC first, EmbCC when it targets Cortex-M), same optimisation level, asserts and statistics off, instrumentation by DWT cycle counter read inline, interrupts from a hardware timer, no other load. Each kernel is configured for the feature set under test and no more; configurations are published.

**Operations.** T5 yield switch; T6 ISR-to-thread latency through the kernel's cheapest wake; T8 notification or equivalent set and wait; T9 mutex lock and unlock uncontended, then with inheritance chains of depth 1, 2, 4; semaphore give and take uncontended; message queue send and receive of four words with and without a switch; timer arm and cancel; T10 sleep accuracy; T11 periodic drift; T7 longest masked section (EmbLinkRTOS only, through its monitor; for other kernels by GPIO toggling around their critical-section macros where feasible); T1 to T4 footprint from the map file per feature set.

**Method.** At least 10,000 samples per operation; report minimum, median, 99th percentile, maximum, and the full distribution; every result carries the metadata record of 05 §4.4; the harness itself is open source in `tests/benchmarks/` with one adapter per kernel so that anyone can rerun it and so that a vendor can contest a number by submitting a configuration change.

**Rules.** No result is published without the metadata; competitor configurations are reviewed for fairness and the review is public; a regression against the previous release's baseline fails the release.

## 7. Candidate decisions

Recorded in `06-decision-records.md` as ADR-026 to ADR-037, all Proposed. Summary:

| ADR | Title | Changes |
|---|---|---|
| 026 | Wait protocol: three-state wait flag with a wait generation and a superseded in-flight timeout | 03 §3; KRN-WAIT-008, 009; SPEC-004 |
| 027 | Multi-object wait by binding objects to notification bits | 03 §6.1; KRN-NOTIF-004 to 006 |
| 028 | Priority inheritance algorithm: owned-mutex list, bounded transitive walk, disinheritance on timeout, deadlock detection, any unlock order | 03 §5.2; KRN-SYNC-014 to 016 |
| 029 | Temporal protection: sporadic budgets per thread, sliding-window shares per partition with idle sharing, critical budget, budget donation FUTURE | 03 §2.3; KRN-TP-006 to 009 |
| 030 | Compile-time priority ceilings from the static system description | 03 §5.2; KRN-SYNC-017 |
| 031 | Preemption threshold rejected for 1.0 | none |
| 032 | Generation-tagged handles with dead codes, and leases for cross-partition buffers | 03 §7, §6.6; KRN-OBJ-005, KRN-IPC-009 |
| 033 | Partition-local kernel storage | 03 §8; KRN-PART-007 |
| 034 | Worst-case monitors with caller address as part of the observability baseline | 04 §7.5; OBS-009 to 011 |
| 035 | Checked-build architecture: transition checker, separable validation layer, generator consistency checks, safety gate | 05 §1, §2; BLD-007, BLD-008 |
| 036 | Tiny profile scheduler: priority-indexed thread table and optional idle thread | 03 §2.1, §11; KRN-SCH-042, 043 |
| 037 | CMSIS-RTOS2 and FreeRTOS-API adapters as optional layers | 07 M4 |

Requirement additions outside those ADRs: KRN-SCH-041 (preempted thread ahead of peers), KRN-SCH-037 revised (time slicing at or below a threshold level), KRN-TIM-036 (arming-overhead compensation), KRN-OBJ-004 (system freeze), TEST-012 (the harness), OBS-012 (benchmark baseline regression). Each is written into the architecture document that owns the group; the requirement files are produced by the corresponding specification work item as before.

## 8. What was changed in the architecture documents

- `01-vision-and-principles.md` §5: positioning table extended with ChibiOS, uC/OS-III, RIOT, and QNX; a paragraph naming the nine claims.
- `03-kernel-architecture.md`: §2.1 (KRN-SCH-037 revised, 041 to 043 added), §2.3 (temporal protection revised per ADR-029), §3 (wait protocol references ADR-026), §4 (KRN-TIM-036), §5.2 (KRN-SYNC-014 to 017), §6.1 (KRN-NOTIF-004 to 006), §6.6 (leases), §7.4 (KRN-OBJ-004, 005), §8 (KRN-PART-007), §11 (tiny profile scheduler).
- `04-platform-architecture.md` §7.5: OBS-009 to 012.
- `05-engineering-system.md` §4.4: the harness; BLD-007, BLD-008; TEST-012.
- `06-decision-records.md`: ADR-026 to ADR-037.
- `07-roadmap.md`: research records inserted before work item 4; harness in M2; adapters in M4.
- `08-open-questions.md`: Q4, Q5, Q7, Q8 recommendations confirmed or revised by R-001.

## 9. Questions for the owner and answers (2026-10-07)

1. *Accept ADR-026 to ADR-037?* **All twelve accepted.** The specification work items refine them; a refinement that changes a decision goes through a new ADR.
2. *Are T5 (<= 200 cycles yield) and T7 (<= 150 cycles masked) the right ambition?* **Kept as stated.** They are design targets; the first harness measurement becomes the baseline that later releases may not regress (OBS-012), and the promise made to users is always relative ("not slower than the best competitor measured on the same board"), never the absolute number until measured.
3. *API adapters (ADR-037)?* **Wanted.** CMSIS-RTOS2 first because vendor middleware targets it, the FreeRTOS API second for migration; optional layers in M4, never part of the certified profile, never influencing the native API.
4. *Preemption threshold (ADR-031)?* **Rejected for 1.0 and carried as FUTURE**, to be reopened only with harness evidence of a switch-count problem on a real workload.
