# M2 implementation work order

This is the ordered work list for finishing the blocking kernel, derived from the [roadmap](../architecture/07-roadmap.md). It prepares our later coding sessions; no item is marked complete merely because its specification or checklist exists. IDs such as **M2-01** are work-item labels, not new normative requirement IDs. The current baseline is the M1 source and the documentation/API review merged in `efff40c`.

For each item, open a scoped issue using the feature/bug form, settle contract decisions, implement the model and conformance evidence with the behavior change, update user/API docs and status, and submit one reviewable PR. Use the [required checks](repository-workflow.md) and keep passed/skipped/failed/unrun evidence distinct. Never widen the CI exclusions to conceal an unexplained failure.

## Order and dependencies

| Order | Work item | Depends on | Completion evidence |
|---|---|---|---|
| 1 | M2-01: Capture and classify the known deep differential failures | Existing native runner/model | Exact archived replays, diagnosis, minimized regression cases |
| 2 | M2-02: Close current contract and introspection inconsistencies | M2-01 evidence workflow | Explicit decisions, applicable port/checked/release cases and updated contracts |
| 3 | M2-03: Finish binding and object-lifetime decisions | M2-01/02 | Teardown/state-machine evidence; no dangling target access |
| 4 | M2-04: Make case-level evidence and bounded resource costs explicit | M2-01 through 03 | Actual case outcomes/configurations and a reviewed resource-bound inventory |
| 5 | M2-05: Event flags | Common wait protocol and M2-03/04 | ANY/ALL/CLEAR interleavings, ordered batch wakes and destroy/timeout/cancel cases |
| 6 | M2-06: Condition variables | Reviewed mutex/wait semantics and M2-03/04 | Atomic release/wait and mandatory reacquisition in every completion path |
| 7 | M2-07: Copy-by-value message queues | M2-03/04 | Unit/slot conservation, bounded copies and sender/receiver hand-off |
| 8 | M2-08: Work queues | M2-03/04; chosen wake/queue mechanism | Serial callback execution, submit/cancel/flush lifecycle and self-wait misuse |
| 9 | M2-09: Software timers | M2-08 and existing deadline engine | Callback context, drift/overrun arithmetic, rearm/cancel/destroy races |
| 10 | M2-10: Integrated misuse/property/coverage campaign | M2-05 through 09 | Adversarial combined scenarios, complete implemented-scope evidence and disclosed gaps |
| 11 | M2-11: API/documentation and M2 exit review | All required items | No unexplained required failures; implementation inventory matches delivered scope |

Items 5–8 can be developed independently once their common prerequisites are sound, but begin with event flags as a focused extension of the shared wait protocol. Timers follow work queues because the default timer callback executes there. Do not substitute a speculative new semaphore/queue design for an explicit work-queue execution contract.

## M2-01: Reproducible failure capture first

The [initial investigation](m2-01-investigation.md) archives the seed-3 reproduction and a two-sleeper reduction. The [timer-delivery correction](m2-01-timer-correction.md) adds model/live regressions and records 11,200 agreeing deep-campaign runs. The [evidence workflow](differential-evidence.md) now provides automatic bundles, exact offline/live replay and bounded schedule reduction with repeat confirmation. The [bounded seed-5 triage](m2-01-seed5-triage.md) records 56,000 current-source agreements and a historical-source check: the old seed-3 control fails as expected, but seed 5 passes. The original seed-5 input and build cannot be recovered; the project owner closed the report as not reproducible (input unavailable, cause unknown) on 2026-10-27. The cause remains unknown and the report stays a disclosed limit. M2-01 is complete.

- [x] Capture `suspend_while_waiting` from the unfiltered 40-schedule campaign with seed 3.
- [x] Recover/capture and classify the historical `cancel_waiter_disinherits` seed-5 input; the current documented campaign passes without reproducing it. Closed as **not reproducible: input unavailable, cause unknown** (maintainer decision 2026-10-27; disposition defined in [timing and evidence](timing-and-evidence.md)): not classified, not fixed; reopen triggers in the [triage](m2-01-seed5-triage.md).
- [x] Complete bounded seeds 1–10 campaigns on checked/release/tiny/periodic and check the report's originating source; retain the historical finding as unconfirmed and identify evidence that would reopen reproduction work.
- [x] Archive observed runner/model source identity, profile/check/owner-death settings, encoded scenarios, exact IRQ/tick placement, complete traces, operation results and final states.
- [x] Replay the captured schedules exactly; do not assume `--only` preserves an unfiltered random schedule.
- [x] Reduce and classify the reproduced seed-3 timing gap, preserve regressions and rerun affected campaigns.
- [x] Add general automatic failure-bundle capture and bounded schedule reduction. Keep unresolved outcomes open; passing campaigns do not replace the historical reproduction. Program/object reduction remains outside this tool's current scope.

This work may change evidence/replay tooling before kernel code, but its investigation and design still belong in a reviewed coding session. No failure is pre-classified as “just the bridge.”

## M2-02: Current contracts before new APIs

The [contract review](m2-02-contract-review.md) records source-grounded proposals for IRQ domains/mapping, independent diagnostic indexes, explicit public fault IDs, POINTER lifetime and resource bounds. The review preserves the original proposals; the [IRQ correction](m2-02-irq-priority.md) and ADR-042 subsequently implement and verify the kernel-aware priority portion. The remaining proposals are not completed implementation evidence. The unresolved historical M2-01 item remains visible during this review.

- [x] Reconcile Cortex-M `emb_irq_set_level()` in the current kernel-aware domain with larger-is-more-urgent; advertise fixed-port limits and verify encodings/pending order/nested delivery under QEMU and F446 EmbSIM. Raw-class/generator scope and physical measurements remain separate; see the IRQ report.
- [x] Give every profile a separate stable diagnostic index and bounded allocation/reclamation; preserve scheduler slots and historical trace replay. See the identity report for tests and resource costs.
- [x] Give public misuse faults explicit stable API IDs, preserve wrapper attribution through helpers, export a decoder map, and test class/argument/context/identity including location strings disabled. See the fault attribution report; retained crash format remains separate.
- [x] Review current POINTER-handle/lifecycle error behavior and resource costs against comments; do not promise generation-based stale rejection or unrestricted cycle detection. See the [object lifecycle report](m2-02-object-lifecycle.md): semaphore/mutex init now refuses live storage with `CONFIG_EMB_OBJ_LIST` (ADR-045); init cost annotations corrected. Depth bounds and the full resource inventory move to M2-04.
- [x] Add relevant checked/release and port/model evidence. Native checked/release/tiny/periodic and QEMU Cortex-M checked/release/tiny ran locally for the lifecycle change; embedded checked builds skip the fork-based fault assertion. No model change: storage reuse is not a modeled transition. Hardware priority observation stays unrun until an appropriate emulator/board test is actually executed.

The current docs disclose these gaps. Correcting one must update its contract, regression case and implementation together; documentation coverage is not a fix.

## M2-03: Binding and object lifetimes

- [x] Decide the Proposed KRN-NOTIF-012 initial-readiness contract: binding's current one-bit mask and explicit initial drain must remain unambiguous. Accepted: no initial edge, one validated bit.
- [x] Define and implement unbind/teardown with a bound target, racing give/set, suspended/terminated target, destruction and storage reuse. `emb_sem_unbind_notify()`; the target cannot be destroyed while bound (ADR-046, KRN-NOTIF-013/014).
- [x] Preserve direct waiters and no-lost-wakeup recheck semantics; model the bind/unbind/destroy transitions. Model operations `BindNotify`, `UnbindNotify`, `ThreadDestroy`, the BINDING scenario group and the "no binding names a destroyed thread" invariant; the bridge agrees on all native presets.
- [x] Specify admission/static storage reuse and any M2 freeze behavior needed by the supported controller. Distinguish the unimplemented TABLE/partition generation mechanism from POINTER lifetime responsibility. Storage is reused only through destroy (ADR-045); a bound thread cannot be destroyed (ADR-046); the controller needs no freeze in M2 (KRN-OBJ-004 stays with the isolated configuration). POINTER handles carry no generation.
- [x] Establish shutdown ownership and producer quiescence before object/target storage is invalidated; test deterministic waiter outcomes. The teardown order is SPEC-006 §3.2; `destroy_bound_sem_releases_target` checks the waiter's `EMB_EDESTROYED` and the released target.

## M2-04: Executed cases and bounded costs

- [x] Export individual conformance outcomes with requirement IDs, assertions/analyses, configuration hash, source SHA, skips and unrun reasons. The framework prints `EVIDENCE`, `req` and `CASE` lines; `tools/evidence/collect.py` records them with the source identity, and every CI test job uploads them (see [case evidence](case-evidence.md)).
- [x] Validate declared mappings against actual executed cases rather than counting header/test ID strings. Migrate legacy requirement definitions deliberately when their owning subsystem needs them. `tools/evidence/matrix.py` counts only citations printed by executed, asserting cases. The fourteen cited `KRN-SCH`/`KRN-MEM` identifiers of groups pending migration stay visible as uncataloged rows; their v0.1 text is not in the repository, so their migration waits for the scheduler and memory requirement work rather than being reconstructed.
- [ ] Inventory admitted waiters, armed timeouts, owned mutexes, inheritance depth, tiny table scans and callbacks/batch wakes.
- [x] Define feature/configuration applicability for each case and analysis. Accepted design status remains separate from evidence outcome. A case applies where its feature gate compiles it; `tools/evidence/configurations.json` gives every configuration its execution scope or unrun reason, and the matrix keeps design status and evidence outcome in separate columns. Telling an out-of-scope `no case` from a gap automatically is not yet done.

The [timing/evidence contract](timing-and-evidence.md) controls these artifacts. Physical timing and full release reproducibility remain the subsequent hardware/release gates, not inferred results of this export.

## Tiny-profile RAM headroom

The `footprint_t1` reference build was at the R-003 T2 limit of 64 bytes of kernel static RAM, which includes the idle context's control block (R-003 §4, T2 accounting). This work is independent of the M2 order. It brought the build to 39 bytes, and the gate now sits at that measured value.

- [x] Small savings of about 6 bytes: bind the fault hook at link time instead of through a RAM pointer, pack the per-CPU flags, and drop the idle slot from the priority-indexed thread table. Done: the thread table has no idle slot (2 bytes), and `CONFIG_EMB_FAULT_HOOK_RUNTIME=n` in the reference configuration calls the application's `emb_fault_hook()` (2 bytes). The per-CPU flags stay unpacked: the masked reads would be inlined into every public entry and cost more flash than the RAM saved (see the [AVR port page](../ports/avr.md)).
- [x] Slim idle context: idle never blocks, times out, takes notifications, joins or changes priority, so on the tiny profile it needs only its saved stack pointer and the fields the scheduler and switch path read. Expected to recover about 25 of its 32 bytes. Check every path that treats idle as a full `embk_thread_t` (switch, diagnostics, identity, trace) on native, AVR, Cortex-M and RV32 before changing its type. Done under ADR-050: an 11-byte prefix on AVR, `footprint_t1` kernel RAM 64 → 43 bytes; no handle resolves to idle and `emb_thread_self()` is null there.
- [x] Lower the `avr-uno-t1` RAM threshold to the new measured value so the recovered bytes are not silently spent. `kernel_ram_max` is 39 (was 64); the `first_execution` gates follow their baseline-plus-10-percent rule at 48 (checked) and 46 (release) bytes.

## M2-05 through M2-09: Primitive acceptance

- [ ] **Event flags:** model overlapping masks and ANY/ALL/CLEAR; test deterministic waiter order, clearing before the next waiter, multi-wake cost, ISR set, timeout/cancel/destroy and binding semantics. Follow SPEC-005 and KRN-SYNC-029.
- [ ] **Condition variables:** model atomic mutex release/enqueue, no stored signal, and mandatory mutex reacquisition even after timeout/cancel; test broadcasts, priority inheritance, recursive-mutex restrictions and relock response-time implications. Follow SPEC-005 and KRN-SYNC-030.
- [ ] **Message queues:** start with the copy-by-value subset; validate item-size/capacity arithmetic and aligned caller storage, direct receiver/slot hand-off, front send, no-wait ISR paths, absolute deadlines, cancellation/destruction and readiness recheck. Bound bytes copied while masked. Follow SPEC-007/KRN-IPC; ownership queues depend on a later pool gate and must not be advertised early.
- [ ] **Work queues:** define callback thread/priority and serialized execution; model submit/resubmit states and cancel/flush including callback self-wait misuse, delayed scheduling and teardown. Follow SPEC-006/KRN-WQ; expose only implemented configurations.
- [ ] **Software timers:** use the work queue for default callbacks; model absolute periodic rearm, missed-period policy and checked overflow arithmetic, cancel/destroy while callbacks are queued/running, and any explicitly supported ISR callback mode. Follow SPEC-003/KRN-TIM; do not silently enable embedded tickless before its port contract is implemented.

For each new public API: complete annotations and parameter/lifetime docs, generated storage checks, native checked/release/tiny applicability, boundary misuse, model agreement, emulator evidence for supported ports and a finite user example. An unsupported configuration must be rejected or explicitly excluded; it is not verified by a skipped test.

## M2-10 and M2-11: Integrated exit

- [ ] Combine timeout, cancel, suspend, destroy, hand-off, priority change and callback delivery at section boundaries, with boundary resource counts and adversarial IRQ/tick placement.
- [ ] Execute applicable misuse/property/sanitizer campaigns and report statement/branch coverage with exclusions and actual case identity.
- [ ] Reconcile every delivered API's docs, configuration, model, current complexity and implemented requirement scope.
- [ ] Require green current CI plus the scoped individual evidence; no unexplained required failure and no unsupported feature advertised as working.
- [ ] Update STATUS, Unreleased changelog and tutorials, then record the M2 exit review and remaining M3a hardware/release work.

Barriers, pipes, buffer pools and INDEXED handles have separate feature gates unless the reference application's agreed scope requires them. TABLE capabilities, partitions/restart, budgets, SMP/AMP and a full driver platform remain later gates. Completing M2 is not a 1.0 release or hardware-support qualification; use the [release checklist](releases.md) when that stage is reached.
