# M2-01: Bounded historical seed-5 triage

**Evidence date:** 2026-10-14. **Current source:** `37887c9e92e2242ff4d11cfa58c6972fce4b0e0e`. **Disposition:** the historical `cancel_waiter_disinherits` seed-5 report is closed as **not reproducible: input unavailable, cause unknown** ([timing and evidence](timing-and-evidence.md); maintainer decision, 2026-10-27). No cancellation/disinheritance correction is claimed and no classification is made. The bounded investigation is complete; the report stays listed among the known limits.

The [initial investigation](m2-01-investigation.md) recovered seed 3, the [timer correction](m2-01-timer-correction.md) addressed that model gap, and the [capture workflow](differential-evidence.md) now preserves future failures. This follow-up broadens the captured campaign and checks the source that first recorded the report. It changes no kernel, architecture, runner or model implementation.

## Current-source search

All four native runners were configured and rebuilt using the repository presets, with GCC 14.2.0, Python 3.12.14, CMake 3.31.10 and Ninja 1.13.0. Each preset ran the complete, unfiltered catalogue for seeds 1 through 10, with 40 schedules per scenario. The CLI's existing horizon of raw events 1–40 and zero to three generated tick placements were retained. Natural/early-ISR schedules and repeated random inputs count as executions; this is not a count of distinct interleavings.

| Preset | Clock/check mode | Agree / executed | Divergences | Excluded per campaign |
|---|---|---|---|---|
| native-gcc | tickless, checked base | 14,000 / 14,000 | 0 | bitmap_queue |
| native-release | tickless, release base | 14,000 / 14,000 | 0 | bitmap_queue |
| native-tiny | tickless, checked tiny | 14,000 / 14,000 | 0 | preempted_goes_ahead |
| native-periodic | periodic, checked base | 14,000 / 14,000 | 0 | bitmap_queue |

Total: **56,000 agreeing executions**, including 1,600 executions of the cancellation scenario (40 schedules × 10 seeds × 4 presets). The existing applicability exclusions were retained. All 40 campaign commands returned 0, so automatic capture produced no failure bundles and there was no failing schedule to reduce. Passing a campaign does not test timeout/error capture paths afresh; those paths have their separately recorded workflow tests.

The fixed search budget was ten seeds per preset and 40 schedules per admitted scenario, with a 15-second runner timeout, 20,000 model search steps, a 120-second whole-campaign timeout and a 600-second overall current-source cutoff. All campaigns completed within those limits. Limits are diagnostic constraints, not a timeout classification of the historical report.

## Historical-source check

Git history identifies `aff0bb9` as the commit introducing the bridge and the README statement about the seed-5 report. The historical statement does not identify the actual failing binary/configuration, exact placement or full output. The recovered source is therefore a reproduction candidate, not proof of the original execution identity.

A `git archive` of that commit was built separately from the working checkout with the current native GCC toolchain, using its original presets, runner, catalogue, bridge and model. No source patch was applied to that archive. Each historical campaign was bounded to 120 seconds.

| Archived source preset | Seed | Agree / executed | Outcome |
|---|---|---|---|
| native-gcc | 3 | 1399 / 1400 | One expected old-model suspension mismatch |
| native-gcc | 5 | 1400 / 1400 | No divergence |
| native-release | 5 | 1400 / 1400 | No divergence |
| native-tiny | 5 | 1400 / 1400 | No divergence |

The seed-3 control reproduced exactly `tick@1,tick@22,tick@24,isr0@29`, with a mismatch at canonical event 11, `tick 4`, matching 11/20 events. Its campaign returned 1. This is the already archived and corrected model timing gap, not a new regression on current source. All three historical seed-5 campaigns returned 0. Their one-per-campaign profile exclusions match the current investigation.

Rebuilding old source today does not recover its original compiler, binary, environment or unrecorded invocation. A successful seed-3 control shows this archive check can detect the known old-model mismatch; it does not establish the missing seed-5 cause. The related canceled-lock/owner-fault case from the initial investigation remains a different input.

## Preserved evidence and reproduction

The [triage archive](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/model/evidence/seed5-triage) preserves both manifests, campaign commands/limits and exit statuses, complete campaign logs, configuration snapshots, compiler/Python/platform metadata, runner/source fingerprints, the current cancellation scenario and its exact ordered schedule lists. The current source/build identities stayed unchanged throughout each preset's campaigns. Historical manifests explicitly identify a source archive and its commit; Git identity is unavailable inside the unpacked directory and is not substituted with the current checkout's SHA.

Each current campaign also records a digest of the ordered encoded inputs and schedules across the full admitted catalogue. This digest aids reconstruction checks; it is not a full kernel trace or a proof of exhaustive coverage. No successful-run trace is claimed to be archived by the failure-only capture tool. The archive README explains its paths and reconstruction snippet.

To repeat the current-source search, build all four presets and run from the repository root:

```sh
for preset in native-gcc native-release native-tiny native-periodic; do
  for seed in 1 2 3 4 5 6 7 8 9 10; do
    python3 -I tools/model/bridge.py \
      --runner "build/$preset/tests/differential/diff_runner" \
      --runs 40 --seed "$seed" --timeout 15 --replay-steps 20000 \
      --capture-dir "build/seed5-triage-recheck/$preset/seed-$seed" || exit "$?"
  done
done
```

These individual commands preserve the input parameters; they do not by themselves enforce the investigation driver's overall wall-time cutoffs. Filtering with `--only` changes RNG consumption and is not equivalent to the unfiltered seed-5 campaign.

## Remaining uncertainty and next gate

The original input and build cannot be recovered, and the bounded campaigns above found no current divergence. On 2026-10-27 the project owner closed the report as **not reproducible: input unavailable, cause unknown**. This closes the M2-01 checklist item; it does not classify the report or claim it fixed.

Reopen when any of these appears: an original trace, encoded scenario, placement or build record for the report; or a new captured cancellation failure. A new failure must be replayed, reduced within recorded budgets and classified against the contracts; do not assume it shares the seed-3 cause. The automatic failure bundles of the [evidence workflow](differential-evidence.md), the `differential_bridge`, `differential_timer_regressions` and `differential_evidence` checks and the archived regressions stay in place to catch one.

The report stays listed among the known limits in [Project status](../STATUS.md) and in the notes of any release that includes the affected code. Closing it is not a release waiver for any other required failure.
