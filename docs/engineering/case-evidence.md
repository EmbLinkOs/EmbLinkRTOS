# M2-04: Executed case evidence

This page defines how conformance runs report individual cases, how those reports are collected, and how the requirement matrix is derived from them. It implements the first and second M2-04 items of the [checklist](m2-checklist.md) under the rules of [timing and evidence](timing-and-evidence.md): design status, evidence outcome, execution scope and coverage type stay four separate facts. The resource-bound inventory is a separate item.

## What a run prints

The test framework (`tests/framework/emb_test.c`) adds three line types to the existing `TEST`, `-> ok`/`-> FAILED`, `skip:` and summary lines, all unchanged so that the QEMU and EmbSIM runners keep parsing `N tests, M failed`:

```text
EVIDENCE v1 suite=test_sem config=9212dd95ff5d9762 profile=base board=native checked=1
TEST sem_counts_and_nowait
  -> ok
  req KRN-SYNC-020 KRN-SYNC-021
CASE sem_counts_and_nowait outcome=passed asserts=8 skips=0
```

- `EVIDENCE` opens every run: the executable (generated per test by `emb_add_test()`), the configuration hash of `<emb/config.h>`, profile, board and checked mode.
- `req` lists the case's `EMB_TEST_REQ` citations. They are buffered and printed after the case body, because output inside a case takes ticks on a real-time port. Up to eight are kept; more are reported as `+N` and not recorded.
- `CASE` closes every case. Every assertion macro counts once, passing or not (`EMB_ASSERT_*`, marks and elapsed checks, and fault checks: by fork on native, by reset and replay on a board whose reset keeps RAM, A89). The outcome is `failed` when any assertion failed, `skipped` when the case asserted nothing and skipped at least once (typically a misuse case whose only check needs a fault check the board cannot run), and `passed` otherwise. A case whose fault check reset the board prints its `TEST` line once, on the first boot, and its `CASE` line once, on the boot that finishes it. The boot that finishes the case prints `fault checks that reset the board: N` before its outcome, and no boot after the first prints a second `EVIDENCE` line.

A case that prints `TEST` without its `CASE` line crashed or timed out inside the body; the collector reports it as `incomplete`. A case compiled out by its feature gate prints nothing in that configuration.

## Collection

`tools/evidence/collect.py --build build/<preset> --preset <preset> --out <file>` reads the build's `Testing/Temporary/LastTest.log`, which CTest writes with every executable's full output whether it ran natively, under QEMU or on EmbSIM. Nothing runs twice. The JSON record (`emblink-case-evidence`, version 1) holds the source identity (`git rev-parse HEAD` and whether tracked files differ), the configuration identity the executables reported (the collector fails when they disagree), and per executable its CTest result and cases with outcome, assertion and skip counts, skip reasons and citations. Every CI test job collects right after its CTest run, also when tests fail, and uploads `case-evidence-<preset>.json`. The collection must come before any later `ctest` run of the same build: one that runs a single test rewrites `LastTest.log`. Until 2026-11-08, five jobs ran theirs afterwards, to decode a crash record or check a second board. In four configurations that rewrote the log, so their evidence held one test:

| Configuration | Requirements passing | Its Clang sibling |
|---|---|---|
| `native-gcc` | 4 | 135 |
| `cm4-an386` | 2 | 136 |
| `rv32-virt` | 2 | 134 |
| `avr-uno-embsim` | 3 | — |

The fifth job built its second board in another directory, so its evidence was intact.

## The requirement matrix

`tools/evidence/matrix.py <evidence files...> [--csv FILE] [--json FILE]` joins the requirement catalog of `docs/requirements/` (through `tools/docs/build.py`) with the evidence files and the configuration inventory `tools/evidence/configurations.json`. Each row is one requirement in one configuration:

| Column | Meaning |
|---|---|
| `design_status` | the catalog's Status: Proposed, Accepted, Implemented, Verified, Withdrawn |
| `evidence_outcome` | `passed`: an executed case citing the ID passed with at least one assertion; `failed`: a citing case failed or is incomplete; `skipped`: citing cases only skipped; `no assertion`: they ran but checked nothing; `no case`: nothing cites it here; `not run`: no evidence file for the configuration |
| `execution_scope` | `host`, `emulated` or `hardware` from the inventory, or `unrun:` and the stated reason |
| `coverage_type` | `conformance` for case evidence; model, analysis and coverage rows join as those tools report |
| `cases` | the citing cases as `executable:case` |

Only a citation printed by an executed case counts. `@req` header tags, Verification fields and identifiers that merely appear in test sources are traceability links, not evidence; a requirement is never "passed" by its declaration. The inventory lists every preset with its scope, and names the hardware configurations (`avr-uno-hil`, `cm4-nucleo-hil`, `cm33-pico2w`) as unrun here, with their reasons. Their runs on boards are archived in `docs/engineering/hardware-runs.json` and counted by the [support matrix](support-matrix.md), not as case evidence. `tools/support/matrix.py` refuses a preset CI runs that this inventory does not list.

Applicability follows the feature gates. A case exists in a configuration exactly when its `#if` gate is true there, so a `no case` row in a configuration that enables the feature is a gap. A `no case` row where the feature is off is out of scope for that configuration. The matrix does not yet tell the two apart automatically.

## The gate

CI's `evidence` job gathers every job's evidence and runs the matrix with `--baseline tools/evidence/baseline.json`. The baseline lists, per configuration, the requirements an executed case passed for. The job fails when:
- a configuration loses one of them;
- a baseline configuration has no evidence;
- a case cites an unknown requirement;
- a case failed.

New evidence passes. The job reports it and writes `requirement-baseline-now.json` with the matrix (the `requirement-matrix` artifact), for a pull request to commit. Removing or moving a case is then a visible change to the baseline file. The `CI merge gate` requires the job.

This makes TEST-009's matrix a regression gate, not yet its completeness gate. TEST-009 asks CI to fail on uncovered normative requirements, which needs each requirement marked as in or out of the released scope. Today the catalog's status says Accepted or Proposed, and only three statuses say Implemented.

The first baseline (2026-11-08) comes from this change's own CI run, with the four configurations collecting their whole suite: 4,964 requirement/configuration pairs in 40 configurations. Against `main`'s run at `f76cc7c`, it has 502 more: `native-gcc` 135 requirements instead of 4, `cm4-an386` 136 instead of 2, `rv32-virt` 134 instead of 2, `avr-uno-embsim` 108 instead of 3.

## First findings

Local runs of this change, on macOS with the toolchains of the earlier M2 reports:

| Configuration | Cases | Requirements passed / skipped / no case |
|---|---|---|
| native-gcc | 97 passed | 94 / 0 / 283 |
| native-release | 90 passed | 94 / 0 / 283 |
| native-tiny | 92 passed | 90 / 0 / 287 |
| cm4-an386 (QEMU) | 85 passed, 10 skipped | 94 / 2 / 281 |
| cm4-nucleo-embsim | 85 passed, 10 skipped | 94 / 2 / 281 |
| avr-uno (QEMU) | 73 passed, 9 skipped | 87 / 1 / 289 |

Most catalog requirements have no executed case: they describe planned features (event flags, queues, timers, isolation, hardware generation) or are verified by analysis that is not yet reported. The scheduler (`KRN-SCH`) and memory (`KRN-MEM`) groups were migrated into `docs/requirements/` on 2026-10-31: their 03 identifiers verbatim, and the v0.1 identifiers that cases cite restated from the specifications that fix their meaning. Groups still pending migration keep their evidence in rows marked `uncataloged (migration pending)` instead of inventing catalog text. A citation outside every known group fails the matrix.

## Limits

The case report proves that a case ran and what it asserted, not that its assertions express the requirement fully: reviewing the cited cases against the requirement text stays a human step. The outcome counts assertions, not branches; statement and branch coverage are M2-10. Evidence records the source identity at collection time; a build from a dirty tree is marked as such, not excluded.
