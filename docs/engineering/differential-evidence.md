# Capturing, replaying and reducing differential failures

The native differential bridge saves every failed campaign/regression run as a separate JSON bundle under ignored `build/differential-failures/`. Probe failures, runner launch/exit/timeout failures, malformed traces, lost events and model/replay failures retain distinct diagnostic categories. A category describes the failed check; it does not assign a kernel defect or close an investigation.

This workflow implements the general capture and schedule-reduction part of M2-01. The historical seed-5 input remains unavailable, as recorded in the [work order](m2-checklist.md). The original manual archives and the [timer correction](m2-01-timer-correction.md) keep their own source identities and outcomes.

## Capture a campaign

From the repository root:

```sh
cmake --preset native-gcc
cmake --build --preset native-gcc
python3 -I tools/model/bridge.py \
  --runner build/native-gcc/tests/differential/diff_runner --runs 40 --seed 3
```

On failure, the bridge prints `saved failure` and the exact path. Use `--capture-dir build/my-campaign` to select another destination. Passing runs create no bundles. Existing files are never overwritten. Capture errors are reported explicitly and return exit status 2; a run with captured campaign failures returns 1; a passing campaign returns 0. Failed configuration probes also return 2 with a bundle when it can be written.

`--timeout` sets each runner's timeout in seconds (default 60). `--replay-steps` sets the model search budget (default 20,000). Both limits are stored in the capture; reducing a search limit can itself produce a replay-budget failure, which is not a kernel failure. The original catalogue RNG consumption and existing applicability exclusions are unchanged. Filtering with `--only` still changes a seeded campaign; exact replay uses the captured schedule instead.

## Bundle contents and identity

The versioned format is identified by `format: emblink-differential`, `version: 1`. It includes:

- The complete scenario with ordered threads/objects, typed operations and arguments, ISR bodies, inactive threads, owner-death policy and clock mode. Replaying it does not look up a scenario name in the current catalogue.
- The encoded runner input, exact ordered IRQ/tick placements and corresponding arguments. The reader checks that these representations agree.
- Full stdout/stderr, exit status and transport/parsing errors; timeout captures retain available partial output. Results, final states and raw trace events remain in that output.
- Effective runner settings, campaign seed/filter/run index, runner timeout and model search limit, plus the observed diagnostic and failure signature.
- Repository commit/dirty state, relevant model/runner source hashes, a kernel/port/configuration tree fingerprint, Python/platform details and the runner binary hash. For normal CMake build layouts it also embeds the generated configuration and its hash, compiler version and CMake cache version/compiler fields. Unavailable metadata is represented as null where applicable.

The campaign takes its source/build snapshot before running and compares checked fingerprints when a failure is saved. If source, binary or build metadata changed, the bundle retains both snapshots and marks `identity_consistent: false`. Archived output can still be inspected/replayed, but reduction refuses that mixed capture. These fingerprints identify a repository/model snapshot and a binary; they do not prove that the binary was freshly built from that snapshot or establish release reproducibility.

Operations use an explicit type registry and primitive JSON arguments. No code or command stored in a bundle is evaluated. The recorded runner path is descriptive: live execution requires the caller's explicit `--runner` argument. Old manually assembled M2-01 JSON files use a different format; their documented archive regression tests remain available.

## Replay one exact capture

Replace `case.json` below with the path printed by the bridge. Offline replay checks the recorded output against the current model without executing a runner:

```sh
python3 -I tools/model/evidence.py replay build/differential-failures/case.json
```

Live replay reruns the complete saved scenario and exact placement list, using the binary you select:

```sh
python3 -I tools/model/evidence.py replay build/differential-failures/case.json \
  --runner build/native-gcc/tests/differential/diff_runner
```

The command reports captured/current identities and whether the failure signature still matches. It returns 0 for agreement, 1 for a failure (including a changed signature), and 2 for invalid input/tool errors. A changed source or binary can be useful when checking a fix; it must be recorded and reviewed. A different/missing runner configuration is reported as a configuration failure rather than silently adapting the scenario to another profile or clock mode.

The saved timeout and search budget are used by default. `--timeout` can change a live timeout for diagnosis; timeout signatures include that limit, so a shorter cutoff is not the same recorded timeout failure. Agreement with a current model, or a changed infrastructure failure, does not by itself establish the historical cause.

## Reduce the schedule

Reduction requires a live runner because changing a placement list cannot change an already-recorded output:

```sh
python3 -I tools/model/evidence.py reduce build/differential-failures/case.json \
  --runner build/native-gcc/tests/differential/diff_runner \
  --output build/differential-failures/reduced.json \
  --max-attempts 100 --max-seconds 120
```

The tool first confirms the original failure signature twice. It then greedily tries deleting individual schedule entries, restarting after each accepted deletion. Every accepted candidate must reproduce the same signature twice. The scenario/program, remaining ordinal values, ordering, effective configuration and replay limit stay fixed. Source/binary/build fingerprints are checked again before output is written. A changed or flaky baseline is refused.

Trace-mismatch signatures keep the next event's semantic fields, including its tick value, while dropping process-specific queue/mutex addresses. Other signatures preserve the failed diagnostic/result, exit status, lost-event count or budget/timeout setting as applicable. Matching a signature is a conservative observable predicate, not proof of identical root cause. Removing an interrupt/tick can alter later raw event ordinals and the resulting execution; review the new full trace before adopting it as a regression.

The default limits bound candidate runner trials; metadata collection and output writing also take time. A time-limited trial cannot be accepted as the original timeout merely because the tool shortened its cutoff. If a budget expires, the last confirmed input is saved with `outcome: budget_exhausted` and exit status 1. Otherwise `one_entry_minimal` means no single remaining placement could be deleted while preserving the tested signature at that point; the command returns 0. This is not a global minimum, a program/object reducer or a proof of determinism. Parent file SHA-256, attempts, confirmation count and budgets are recorded. The parent and any existing output file are preserved.

## CI and validation

Every applicable native CTest build registers `differential_evidence`, bringing its target count to twelve. The workflow check intentionally uses a one-step model search budget to exercise four real native captures, offline/live replay, confirmed deletion and exhausted-budget handling. These controlled search-limit failures are temporary test inputs and are not presented as kernel bugs. Native CI jobs are configured to upload the default failure directory as `differential-failures-<preset>` when a job fails; an empty directory is ignored.

On source `be713d50e0c1fcd404f317b299e9e362641d4f0f`, local validation passed 66 model/tooling tests with Hypothesis, all twelve CTest targets on checked/release/tiny/periodic native builds, and both unfiltered 40-schedule campaigns on all four configurations (11,200 runs, zero divergences, existing exclusions retained). Unit tests cover ordered scenario round trips, offline failure/agreement outcomes, partial timeout output, launch/exit/malformed/lost-event distinctions, configuration drift, invalid bundle data, overwrite protection, repeat confirmation, changed/flaky failure predicates, budgets and mixed identities. Additional CLI checks exercised probe capture and an unwritable capture destination.

The [verification record](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/model/evidence/workflow-checks) preserves these logs and source/binary/configuration fingerprints. Production kernel, architecture and model state-transition sources are unchanged by this tooling work. General scenario/program reduction and recovery/classification of the original seed-5 finding remain separate work.
