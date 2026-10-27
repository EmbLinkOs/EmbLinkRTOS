# Timing bounds and verification evidence

**Status:** Documentation baseline, 2026-10-14 (ADR-039). This defines the evidence needed to publish timing, correctness, footprint or support claims. It does not assert that the planned evidence tooling exists.

## Timing claim categories

| Category | What it establishes | Required description |
|---|---|---|
| Design target | A desired result | Metric, hardware/configuration and target; explicitly unmeasured |
| Maximum observed | Largest sample in a particular campaign | Raw samples, sample count, workload, instrumentation/overhead and exact build metadata |
| Established bound | A limit supported under declared assumptions | Path/cost analysis plus tests, input/resource limits, interference assumptions, hardware/toolchain scope and margin |

The largest observed sample is not a mathematical worst-case bound. A monitor observes completed intervals and cannot prevent an interval already exceeding its threshold; a closing-point monitor cannot detect a thread that never reaches a closing point. Budget enforcement and watchdogs need independent progress mechanisms.

## Complete resource-bound inventory

Name limits in the generated build report when that tool exists and in benchmark metadata now. Do not label a path O(1) solely because its final bit-set or dequeue is constant-time.

| Path | Inputs that determine cost | Current implementation or planned work |
|---|---|---|
| Priority wait insertion/requeue | Waiters on the object | Base list walk inside the object section |
| Timeout insert | Armed nodes | Ordered list walk; count includes future timers |
| Timeout removal | Armed nodes in tiny | Base O(1), tiny singly linked removal O(armed nodes) |
| Expiry batch | Nodes due together, wake-hook costs | Processes the due set while protected; includes priority propagation triggered by timeout |
| Mutex acquisition/release | Mutexes already owned by each thread | M1 singly linked append/remove and recomputation scan under the object section |
| Inheritance change | Chain depth, owned locks per hop, waiter requeue | Whole M1 walk remains inside the caller's critical section; no per-hop unlock option |
| Deadlock detection | Configured chain depth | Separate M1 traversal; deeper and mixed-protocol cycles are outside its guarantee |
| Tiny ready/wait selection | Configured priority/thread table size, raised priorities | Effective-priority changes can require table scans |
| Event set/broadcast/flush | Waiters, hooks per woken thread | Planned operations can batch wakes; not universally O(1) |
| Queue/pipe copy | Configured bytes per section | Planned message copy and chunk limits must enter the masking budget |
| Timer periodic catch-up | Elapsed periods, integer width | Planned checked arithmetic; no loop proportional to an unbounded number of missed periods |
| Partition switch/restart | Protection entries, threads, objects, requests, data bytes, device stop latency | Planned preemptible work with bounded protected steps and quiescence deadlines |

Some bounds are currently structural rather than enforced admission limits. A finite data structure does not by itself provide a useful published latency bound. Record the maximum admitted size and reject configurations outside a claimed scope. A timeout budget must include the sum of wake hooks, not just node removal. Depth truncation is a limitation of inheritance correctness as well as timing.

In current header annotations, `waiters` counts queued threads on the affected object, `timeouts` counts armed timeout nodes, `owned` counts mutexes in an affected thread's owned list, and `PI work` includes the owned-list scans and waiter requeues along the visited inheritance chain. `stack` is the configured stack byte count and `threads` the registry count. These variables describe work rather than cycles. Include port switch/interrupt costs, tiny table scans and synchronous trace/callback work in a complete call-path bound; a fast-path annotation does not include the execution time of a higher-priority application that runs before the caller resumes.

## Hardware campaign

Use the primary Cortex-M board chosen in the roadmap. Measure switch, IRQ-to-thread latency, masking intervals, timer accuracy and copy/lock paths with matching compiler, options, feature set, FPU state and clock settings. Confirm cycle-counter instrumentation against GPIO/probe observation where possible. Account for timer and instrumentation overhead.

Exercise maximum supported waiters, owned mutexes and inheritance depth; simultaneous timeout expiry; cancel/destroy during contention; raised priorities on tiny; competing interrupts; and logging/trace enabled configurations. Include nested combinations rather than only isolated fast paths. Preserve raw samples and report minimum, median, percentile and maximum observed. Declare cache, bus, DMA and interrupt interference assumptions.

Cross-RTOS comparisons use the same metric and equivalent features on the same hardware. Review competitor configurations publicly and pin revisions. Statements such as fastest or no slower than every competitor remain targets until supported by those results; the supported competitor/configuration set is always named. QEMU and native execution are functional evidence only.

## Response-time contracts

Timeout expiry and return from a blocking API are different events. A satisfied operation may return early; an expired waiter may be delayed by higher-priority load, suspension, partition eligibility or mutex reacquisition. A response-time guarantee requires declared task periods, execution demands, interrupt load, critical-section/scheduler-lock limits and lock interference. Fixed priority and drift-free deadlines alone do not provide admission control or schedulability proof.

Budget design must account for thread execution, syscall/kernel work, ISR charging, instrumentation and cross-partition services. Specify what happens when an exhausted owner holds a lock needed by an eligible client. State which policies preserve inheritance progress and whether execution debt is charged or donated; reject unsupported combinations. The bounded refill list must be analyzed for replenishment merging without granting more than the specified capacity. Model these interactions before implementing guarantees.

## Requirement-to-evidence matrix contract

The planned matrix has one row per requirement and applicable profile/target/configuration, linked to implementation and a specific test case or analysis artifact. A requirement identifier mentioned in a header or test is a trace link, not evidence of coverage. Validate that the case actually executed and contains assertions or an analysis that can detect violation of the statement.

Record separately:

- Design status: proposed, accepted, implemented, withdrawn.
- Evidence outcome: passed, failed, expected failure, skipped with reason, disabled, not run.
- Execution scope: runner target versus individual case, source SHA, tool versions, configuration hash, target, seed/schedule and artifact location.
- Coverage type: conformance, model agreement, bounded exploration, statement/branch coverage, static analysis, emulation or HIL.

A profile's release gate fails on unexplained required failures or uncovered applicable requirements. Explicitly out-of-scope requirements stay visible and are not counted as verified. Retain skips rather than silently reducing the denominator. A documented, triaged replay limitation needs its reproduction and disposition; it must not be labeled an expected failure merely to obtain a green gate.

The model explores interleavings at section granularity for finite scenario programs within its state budget. It does not prove that every arbitrary program within a thread/object count has been enumerated, or that C code, compiler output, interrupt hardware, SMP or DMA refines the model. Model agreement can miss a shared mistaken assumption; review normative requirements and independent tests as well. Extending a subsystem adds scenarios and independent conformance evidence before enlarging the claim.

For deep replay failures, save the exact runner/model build, full trace, scenario, seed and interrupt/tick schedule; minimize the case and classify it as a kernel defect, model defect, bridge limitation or unsupported scenario with a regression test when resolved. Keep unresolved cases visible in release notes and [Project status](../STATUS.md).

A historical report whose input and build cannot be recovered cannot be classified. After a bounded, recorded reproduction attempt, a maintainer may close it with the disposition **not reproducible: input unavailable, cause unknown**. This is not one of the four classifications, not a fix, and not evidence that the reported behavior cannot occur. The closing record states the attempt's scope, names the triggers that reopen it, and keeps the report listed among the known limits in Project status and release notes.

## Memory and reproducibility evidence

Report kernel, architecture, board, application and runtime subtotals alongside the complete linked flash image. Include `.data` initialization images in flash, live `.data`/`.bss`, alignment/padding, metadata and retained sections. Distinguish reserved from loaded sections.

For RAM, include TCBs and objects, every thread/idle/interrupt/kernel stack, pools/rings/capability tables, retained crash storage and required reserve. Publish configured stack sizes and measured high-water separately; native host stacks are not target stack evidence. Link-map totals and regression gates do not establish runtime stack sufficiency.

Reproducibility needs pinned source, tools, dependency/schema/generator inputs, configuration, and two independently produced outputs with defined treatment of build identity. A saved setup script, deterministic generator design or signed tag alone is insufficient. SBOM and support windows must match the released scope and available maintenance resources; certification requires a separate process, assessment and product scope.
