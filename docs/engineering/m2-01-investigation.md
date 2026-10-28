# M2-01: Native timeout replay investigation

**Evidence date:** 2026-10-14. **Source:** `4021959379d679c07a3c97eb43a06b7271a48ea2`. **Outcome:** the seed-3 failure is reproduced and reduced; the historical seed-5 failure remains unconfirmed. Kernel, model, bridge and CI behavior are unchanged by this investigation.

This page preserves the pre-correction investigation. The [timer-delivery correction](m2-01-timer-correction.md) records the subsequent model change and passing regressions/campaigns; the original archives retain their failing outcomes and source identity.

The reproduced trace exposes a limitation in the reference model's representation of timer delivery during timeout arming. It is not evidence of a broken semaphore or suspend implementation. M2-01 remains open until the timer-delivery model, regression checks and affected campaigns are reviewed and implemented.

## Executed campaigns

Each campaign used the complete catalogue, 40 schedules per scenario and the recorded seed. Filtering was not used. All builds used the native virtual clock with tickless enabled. Checked base and tiny report owner-death `fault`; release reports `release`.

| Preset | Seed | Agree / executed | Divergences | Excluded scenarios |
|---|---|---|---|---|
| native-gcc | 3 | 1399 / 1400 | 1: suspend_while_waiting | 1 |
| native-gcc | 5 | 1400 / 1400 | 0 | 1 |
| native-release | 3 | 1399 / 1400 | 1: suspend_while_waiting | 1 |
| native-release | 5 | 1400 / 1400 | 0 | 1 |
| native-tiny | 3 | 1399 / 1400 | 1: suspend_while_waiting | 1 |
| native-tiny | 5 | 1400 / 1400 | 0 | 1 |

Base excludes `bitmap_queue` because it requires the tiny profile; tiny excludes `preempted_goes_ahead` because it uses shared priorities. These exclusions predate this investigation. Total executed: 8,400 runs, with three divergences representing the same seed-3 case on three builds. The seed-3 campaigns returned 1; seed 5 returned 0.

The [evidence directory](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/model/evidence/m2-01) contains campaign logs, generated configurations, a manifest, six primary replay JSON files and one related cancellation/fault capture. Each replay records source SHA, encoded scenario, exact placements, effective runner settings, runner/configuration SHA-256, exit status, lost-event count, complete stdout/stderr, canonical events and the bridge diagnostic. Source-file hashes and compiler/build/Python versions are in `manifest.json`. Native pointer addresses can differ between executions; they are retained in the original traces and mapped by the bridge rather than compared byte for byte.

## Exact seed-3 reproduction

The captured placement is `tick@1,tick@22,tick@24,isr0@29`. Re-running the checked capture three times produced the same mismatch at canonical event 11, `tick 4`, after matching 11 of 20 events. The runner completed normally, lost no events, returned timeout from T1's semaphore take and left one semaphore unit after the ISR give.

```sh
cmake --preset native-gcc
cmake --build --preset native-gcc
build/native-gcc/tests/differential/diff_runner \
  'T1=1:take 0 3|T2=3:sleep 1;suspend 1;sleep 1;resume 1|S0=0|I0:give 0' \
  --schedule 'tick@1,tick@22,tick@24,isr0@29'
```

This command exposes the kernel trace and returns success; the divergence is reported when the bridge replays it. The JSON archive preserves both outcomes.

## Reduced reproduction and cause

Two sleeping threads suffice. No semaphore, application ISR, mutex, suspend, resume or cancel operation is needed:

```sh
build/native-gcc/tests/differential/diff_runner \
  'T1=1:sleep 1|T2=3:sleep 2' --schedule 'tick@10,tick@13'
```

T2 has higher priority and arms deadline 2 first. T1 subsequently computes deadline 1. The placements count every raw trace event, including construction events; they are not canonical-event indexes or elapsed ticks.

| Boundary | Captured kernel behavior | Current model behavior |
|---|---|---|
| T1's sleep trace, event 10 | The harness advances the raw counter to 1 while the scheduler lock masks timer delivery | The bridge places the first timer action after T1's wait-begin section |
| End of T1's first section | Tick 1 is delivered; T1's timeout is not armed yet | Tick 1 leaves T2's deadline 2 armed |
| T1's timeout-arm trace, event 13 | Deadline 1 is inserted; the harness advances the raw counter to 2 while delivery is masked | Arming an already-due deadline calls `_expire_due()` in the same model action |
| Timeout lock released | Tick 2 is delivered, then T1 and T2 wake in deadline order | T1's wake is emitted before the bridge can consume tick 2 |

The relevant implementation paths are `tests/differential/diff_runner.c` (`sink`, `pump_schedule`, `fire`), `arch/native/native_port.c` (`emb_native_tick`, `emb_arch_timer_set_deadline_raw`), `kernel/wait/wait.c` (`embk_wait_commit`) and `kernel/time/time.c` (`embk_timeout_arm`, timer ISR). The native port queues a timer interrupt for an already-due compare; delivery waits for the critical section to end. The model's `Kernel._block_section`, section 2, instead performs expiry immediately when `deadline <= now`.

The bridge cannot repair the reduced ordering by simply moving the arm after tick 2: T2's already-armed deadline would then wake T2 first, contrary to the captured deadline order. This explains why the mismatch needs another armed timeout. In the bounded reduction search, the two-sleeper program passed with zero or one explicit tick at event positions 1–19 and failed with the two placements above. A separate one-sleeper search passed with up to two ticks at positions 1–11. This is a reduced regression candidate, not a proof of global minimality.

An isolated diagnostic experiment left the already-due node armed instead of invoking `_expire_due()` inside section 2. The bridge then accepted both the original and reduced captured traces on all three presets, including operation results and final states. That experiment was not applied to repository sources and is not a proposed production fix: it does not yet model pending timer delivery when no later explicit tick arrives.

**Classification:** the reproduced cases expose a model atomicity/timer-delivery limitation. Their normal runner outcomes and the isolated replay experiment do not establish a kernel defect. SPEC-002's masked-interrupt delivery and SPEC-003's deadline interrupt semantics must guide the model correction; weakening deadlines or skipping the trace is not justified.

## Replaying the archived inputs through the bridge

From the repository root after building the desired preset, use this diagnostic snippet. It reconstructs the scenarios, applies the runner's configuration and uses the archived placement directly, without consuming the campaign RNG. On the investigated source it prints a divergence for each input. It deliberately does not register either case as a passing CI test.

```sh
python3 -I - <<'PY'
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path('tools/model').resolve()))
import bridge
from embmodel import Scenario, Sleep

preset = 'native-gcc'  # also native-release or native-tiny
runner = f'build/{preset}/tests/differential/diff_runner'
for name in ('suspend', 'two-sleepers'):
    path = Path(f'tools/model/evidence/m2-01/{name}-{preset}.json')
    saved = json.loads(path.read_text())
    sc = (bridge.catalogue.suspend_while_waiting() if name == 'suspend'
          else Scenario(name='due_sleep_arm',
                        threads={1: (1, (Sleep(1),)), 2: (3, (Sleep(2),))}))
    sc = bridge.adapt(sc, saved['config'])
    assert bridge.encode_scenario(sc) == saved['encoded_scenario']
    run = bridge.run_kernel(runner, sc, [tuple(x) for x in saved['schedule']])
    assert all(run.config.get(key) == value for key, value in saved['config'].items())
    try:
        bridge.Replay(sc, run).replay()
    except bridge.Divergence as error:
        print(name, 'DIVERGENCE:', error)
    else:
        print(name, 'AGREES: assess the changed source/build against the archive')
PY
```

The recorded binary/configuration hashes identify this execution. A freshly built binary can differ because of build paths or toolchains; record its identity before treating it as equivalent release evidence.

## Historical seed-5 cancellation case

The exact documented unfiltered seed-5 command passed on all three presets. That does not close the historical finding: its original encoded input, schedule and build identity were not archived, so this investigation cannot establish that the same run was exercised.

A further search of the current `cancel_waiter_disinherits` scenario considered all 12,340 combinations of one, two or three explicit tick placements at raw events 1–40, allowing repeated placements. No bridge divergence was found among runs ending normally. A checked-fault trace at `tick@12,tick@15` does diverge at the same already-due sleep boundary, before any mutex inheritance is exercised: T3's next lock is canceled and its unconditional unlock triggers an owner misuse fault. That trace is not a reproduction of a disinheritance defect or proof of the historical seed-5 cause.

Keep the historical finding open. Recover its original schedule/build if available, and broaden the captured campaign after correcting and reviewing timer delivery. Do not claim cancellation or inheritance is fixed from a passing six-schedule campaign or from this reduced sleep case.

## Follow-up work identified by this investigation

Items 1–3 below are addressed by the [timer-delivery correction](m2-01-timer-correction.md), and the general capture/schedule-reduction work is implemented by the [evidence workflow](differential-evidence.md). The [bounded follow-up](m2-01-seed5-triage.md) checks broader campaigns and the report's originating source. The historical seed-5 input/cause remains unconfirmed.

1. Represent timer request and delivery at critical-section boundaries explicitly. Preserve deadlines fixed at the call, deadline-order expiry, wait-generation invariants and P1 scheduling. An already-due arm must eventually deliver a timer interrupt even without another injected tick.
2. Add regression checks for the two-sleeper ordering and original suspension case, plus already-due arming with no later tick, wake-in-window races and ordinary future deadlines. Keep periodic and tickless applicability explicit.
3. Replay the archived inputs, run model/conformance checks and repeat both unfiltered campaigns on checked, release and tiny. Record changed source/configuration identities and actual outcomes.
4. Add reusable failure capture/replay tooling and retain the historical seed-5 item until it has a concrete explanation. M2-01's completed capture work does not close these remaining steps. *Outcome:* the tooling exists; after the [bounded triage](m2-01-seed5-triage.md) the seed-5 report was closed as not reproducible (input unavailable, cause unknown) on 2026-10-27, without a classification.
