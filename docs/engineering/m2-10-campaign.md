# M2-10 integrated campaign

**Date:** 2026-10-30. **Kernel at:** `f563ef5` (campaign) and `f563ef5` plus this report (coverage, matrix). **Scope:** every M2 primitive on every native configuration, the emulated targets, sanitizers and coverage. Hardware-in-the-loop and physical timing remain unrun: no NUCLEO-F446RE board was available.

## Combined scenarios

The `COMBINED` group of the reference model (`tools/model/tests/scenarios.py`) meets several mechanisms at section boundaries. The model explores every ISR and tick placement; the differential bridge replays seeded placements on the kernel:

| Scenario | What meets |
|---|---|
| `combined_suspend_cancel_msgq_receiver` | a suspended, canceled and resumed message queue receiver against an ISR send (capacity 1) |
| `combined_inheritance_priority_change_event_timeout` | an inheriting owner's base-priority change against another thread's event wait, its timeout and an ISR set |
| `combined_destroy_timeout_and_isr_give` | a timed take against a give and the semaphore's destroy at the same ticks |
| `combined_bindings_two_sources` | one thread bound to a message queue and to event flags |
| `combined_timer_feeds_handler_queue` | an ISR-context timer feeding a queue that a work handler reads, while the thread flushes the work queue and stops the timer |
| `combined_dwork_cancel_races_expiry` | an ISR cancel racing a delayed item's expiry |

Every invariant of every group holds in every explored state (122 model tests, `cd tools/model && python3 -m pytest`).

## Differential campaign

`tools/model/campaign.py` ran the whole catalogue with `--runs 40` for seeds 1 to 10, plus the exact timer regressions, on five native runners (summaries in `tools/model/evidence/m2-10/campaign-*.json`, clean tree at `f563ef5`):

| Runner | Configuration | Runs | Agree | Regressions |
|---|---|---:|---:|---:|
| native-gcc | base, checked, tickless | 33,600 | 33,600 | 5/5 |
| native-clang | base, checked, tickless | 33,600 | 33,600 | 5/5 |
| native-periodic | base, checked, periodic | 33,600 | 33,600 | 5/5 |
| native-release | base, release, owner death releases | 33,600 | 33,600 | 5/5 |
| native-tiny | tiny, checked (no work queues, timers or condition variables) | 24,400 | 24,400 | 4/4 |

158,800 runs, no divergence. Mutation checks during M2-07 to M2-09 confirmed that the bridge catches deliberately broken message queues, work queues, timers and delayed work.

## Defects the campaign found

- **A raised ready thread did not preempt.** `emb_thread_set_priority()` raising a ready thread above the caller left the caller running until its next block or yield: the scheduler hook marked a reschedule only when the running thread's own priority fell. The combined inheritance scenario diverged in release builds, where its owner-death policy lets the run continue. Fixed in the hook and, for builds without mutexes, in `emb_thread_set_priority()`; `sched_raising_a_ready_thread_above_the_caller_preempts` fails on the old kernel and passes on every target now.
- **AddressSanitizer could not run the suites on macOS.** Clang pads Mach-O section globals with redzones, so the test and init table walks read padding as entries. Table entries now opt out (`EMB_TABLE_ENTRY_ATTR`); `native-asan` passes 20/20 locally, as it already did on CI's Linux runners.

Earlier M2 campaigns found the work queue flush deadline (M2-08, now a timer regression) and the RISC-V reset-section collision (M2-09).

## Sanitizers

| Build | Result |
|---|---|
| native-asan (AddressSanitizer + UBSan) | 20/20 |
| native-tsan (ThreadSanitizer) | 20/20 |
| tiny AddressSanitizer build | 17/17 |

## Coverage

`native-coverage` (GCC `--coverage`, `-O0`, checked) after its full CTest run, which includes the differential bridge, measured with `tools/coverage/report.py` (CI runs the same; `tools/model/evidence/m2-10/coverage.json`): **91.4%** of 2,900 kernel lines and **72.5%** of 1,472 branches taken, over all 18 kernel files of the base build; nothing is excluded. Header inline functions are counted in the files that include them. The lowest files and why:

| File | Lines | Branches | Not reached |
|---|---:|---:|---|
| `kernel/irq/irq.c` | 21/32 | 13/16 | connect and priority paths the native port does not use |
| `kernel/exec/exec.c` | 87/108 | 32/50 | early-start failure and port-specific paths |
| `kernel/work/dwork.c` | 79/89 | 29/48 | misuse arguments and the cancel-between-expiry-and-submit window |
| `kernel/sync/mutex.c` | 242/278 | 113/152 | depth-limit and rare ceiling/recursion branches |

Misuse faults are covered where checked builds fork to observe them; release-build misuse returns are covered by the release presets' suites, which this coverage build does not measure.

## Case evidence and gaps

The requirement matrix (`tools/evidence/matrix.py` over the case evidence of all 21 configurations, `tools/model/evidence/m2-10/matrix-summary.json`) has no failed case. Of 377 catalogued requirements, 127 have a passing executed case; 39 are marked implemented in their cards. The implemented ones without an executed case are verified by analysis or by the model, as their cards say:

- KRN-IPC-022 (model coverage before the queue implementation): analysis.
- KRN-WQ-011 (work queue trace and statistics): trace records exist, statistics do not; no trace decoder test.

KRN-IPC-021 gained `msgq_small_counts_refuse_a_capacity_above_255` in this campaign. Fourteen cited KRN-SCH and KRN-MEM IDs still belong to requirement groups pending migration into the catalogue.

## Not covered

- Hardware-in-the-loop, physical timing, masked-time and jitter measurements (no board).
- Work queues, software timers, delayed work and condition variables on the tiny profile, which has none in M2.
- The EmbCC native flow, which CI does not build.
- Seed-5 of M2-01 stays closed as not reproducible.
