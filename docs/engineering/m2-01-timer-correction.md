# M2-01: Timer-delivery correction and regression evidence

**Source:** `ca0ed3f50662efc30bd905d80f20eac52b7aee9c`. **Evidence date:** 2026-10-14. The model now accepts the seed-3 and reduced traces archived by the [initial investigation](m2-01-investigation.md). Kernel and architecture implementation sources are unchanged. The historical seed-5 cancellation finding remains open without its original input/build.

## Corrected behavior

Arming an already-due timeout in tickless mode sets `timer_pending` instead of expiring the timeout inside the arming section. A separate `timer` action delivers the interrupt without advancing `now`. A `tick` action can also deliver it after counter advancement during the masked arming section, as in the captured two-sleeper trace. Expiry remains in deadline order, followed by P1 scheduling; deadlines remain fixed at the call.

Pending delivery is part of the model's state identity and cannot be treated as a terminal execution or bypassed by another thread section. This uses the native runner's timer-first interrupt order (timer IRQ 0). It does not model arbitrary programmable interrupt priorities or qualify physical timer hardware. Periodic mode does not request an interrupt merely by inserting a due node; its next periodic tick expires the wait.

`Scenario(tickless=False)` selects periodic behavior. The runner reports `tickless=0` or `1`, and the bridge adapts the model accordingly. The older archived native traces omit this field and came from tickless configurations; the bridge preserves that compatibility. `bridge.parse_run()` reads an archived stdout/stderr/exit-status record without executing a binary.

Periodic testing also exposed that the differential harness's virtual idle timer never stopped after finite waits ended. The harness now peeks at the protected timeout list before allowing another periodic idle tick, so leftover schedule items and the final verdict can run. This changes the test harness's simulation cutoff, including its existing artificial “sleep forever” threshold; it does not cancel or modify the kernel's periodic timer.

## Regression gates

Nine additional model tests bring the suite to 53 tests. They cover immediate delivery without another tick, delayed delivery with ordered wakes/P1, periodic arming, ordinary future deadlines, a wake that wins in the window, pending-state/terminal handling, bounded two-sleeper exploration in both modes, all seven original archived traces, and rejection of corrupted early-expiry/result records.

Every applicable native CTest build now registers `differential_timer_regressions`, alongside the existing six-schedule campaign. It runs four exact inputs without consuming the catalogue RNG:

| Input | Raw trace-event placements | Purpose |
|---|---|---|
| Two sleepers | `tick@10,tick@13` | Reproduced arm/delivery ordering with another armed timeout |
| One sleeper | `tick@4` | Already-due delivery with no later injected tick |
| Original suspension scenario | `tick@1,tick@22,tick@24,isr0@29` | Captured seed-3 case |
| Related cancellation scenario | `tick@12,tick@15` | Captured canceled-lock/owner-fault trace; not the historical seed-5 input |

```sh
cmake --preset native-gcc
cmake --build --preset native-gcc
python3 -I tools/model/bridge.py \
  --runner build/native-gcc/tests/differential/diff_runner --regressions --verbose
ctest --preset native-gcc --output-on-failure --no-tests=error
```

Use the new `native-periodic` configure/build/test presets for periodic mode. Its configuration is `configs/native-periodic.conf`, and the native CI matrix includes this preset. The new regression check also runs on the existing GCC, Clang, release, tiny and sanitizer native jobs. A CI definition is separate from its observed outcome.

## Recorded local outcomes

All four configurations passed eleven CTest targets (nine conformance executables and two bridge checks), four exact live regression inputs, and both unfiltered deep campaigns. The six-schedule campaign remains unchanged; no catalogue entries were added and RNG consumption is preserved.

| Preset | Clock | CTest targets | Seed 3, 40 schedules/scenario | Seed 5, 40 schedules/scenario |
|---|---|---|---|---|
| native-gcc | tickless | 11 / 11 | 1400 / 1400 agree | 1400 / 1400 agree |
| native-release | tickless | 11 / 11 | 1400 / 1400 agree | 1400 / 1400 agree |
| native-tiny | tickless | 11 / 11 | 1400 / 1400 agree | 1400 / 1400 agree |
| native-periodic | periodic | 11 / 11 | 1400 / 1400 agree | 1400 / 1400 agree |

Total deep-campaign executions: 11,200; zero divergences. Base excludes `bitmap_queue`; tiny excludes `preempted_goes_ahead` for its shared priorities. No exclusions were added. Model tests: 53 passed with Hypothesis installed, zero failures or skips. These are bounded native/model observations, not universal requirement coverage, release reproducibility or physical timing evidence.

The [correction evidence](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/model/evidence/m2-01-correction) preserves source identity/file hashes, tools, generated configurations, runner/configuration SHA-256, CTest/model/campaign logs and sixteen full live replay outputs with exact scenarios/placements and model outcomes. The original failure artifacts remain unchanged in `tools/model/evidence/m2-01/`.

To replay the original archived outputs without rebuilding their binaries:

```sh
cd tools/model
python3 -m unittest tests.test_timer_delivery.ArchivedReplays -v
```

## Remaining M2-01 scope

The reproduced seed-3/model timing defect has a correction and automated regressions. Recovering and classifying the historical seed-5 input, and general automatic failure-bundle capture/minimization, remain open. A related cancellation trace agreeing after this correction does not establish that the historical input had the same cause. Continue that work before marking all of M2-01 complete.
