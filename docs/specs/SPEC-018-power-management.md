# SPEC-018 - Power Management: States, Constraints, the Policy and Its Measurement

**Implementation and release scope:** [Project status](../STATUS.md) records the implemented subset; [roadmap](../architecture/07-roadmap.md) defines release gates. Nothing in this specification is implemented yet; M4.4 implements it.

**Status:** Proposed 2026-11-09 for the project owner's review (M4.0).
**Requirements:** `docs/requirements/PWR.md` (PWR-001 to 007); DRV-002; KRN-TIM-017, KRN-TIM-036.
**Builds on:** 04 §4 (the model); ADR-059 (tickless through a SoC timer), ADR-057 (the two families); SPEC-003 (time), SPEC-014 §3 (the SoC's `power:` states), SPEC-016 (device lifecycle, suspend and resume), SPEC-017 §8 (the watchdog), §10 (RTC wake-up); A108 (sleep in the port contract).

---

## 1. Scope

What the kernel does when nothing is runnable: which power state it enters, under which constraints, how it wakes on time, and how that is measured. **Current and energy are not measured**: the bench has no instrument for them yet (PWR). States, residency and wake latency are, with the logic analyzer.

Out of scope in M4: frequency and voltage scaling while running (PWR-005 keeps it for later), per-peripheral clock gating beyond what a device's suspend does, and power domains inside a SoC beyond its system states.

## 2. States

### 2.1 The generic set

| State | Meaning (04 §4.1) | Typical wake |
|---|---|---|
| ACTIVE | running | n/a |
| IDLE | the core waits for an interrupt; every clock and peripheral runs | any interrupt, at once |
| SLEEP | the core and most clocks stop; RAM and peripheral state retained | a wake source the SoC lists |
| DEEP_SLEEP | most domains off; RAM retained, the main oscillators off | a wake source of the always-on domain |
| OFF | only the always-on domain runs; waking is a reset | the RTC or a wake pin |

### 2.2 The SoC's states

The SoC description lists its system states under `power:` (SPEC-014 §3), extended here:

```yaml
power:   # the STM32F446's, with illustrative figures until they are measured
  states:
    - { name: sleep,      class: IDLE,       entry_us: 0,   exit_us: 1 }
    - { name: stop,       class: SLEEP,      entry_us: 10,  exit_us: 120, loses: [clocks],
        wake: [exti, rtc_wakeup, rtc_alarm], timebase: rtc }
    - { name: stop_lp,    class: DEEP_SLEEP, entry_us: 10,  exit_us: 400, loses: [clocks, flash_power],
        wake: [exti, rtc_wakeup, rtc_alarm], timebase: rtc }
    - { name: standby,    class: OFF,        exit_us: 3000, loses: [sram, clocks],
        wake: [rtc_wakeup, wkup_pin], retains: [backup_sram, rtc] }
```

- `entry_us` and `exit_us` start as the datasheet's figures and are **replaced by measurements** (PWR-006); the policy reads what the generator emits.
- `loses` names what software must restore; `timebase` names what keeps time while the main timer is stopped (§5).
- **The break-even time** of a state is `entry_us + exit_us + min_residency_us`; a stay shorter than it costs more than it saves, and the policy does not choose the state for it. Without a current instrument, `min_residency_us` defaults to `entry_us + exit_us`.

### 2.3 The two families

| Generic | STM32F446 | RP2350 |
|---|---|---|
| IDLE | Sleep: WFI | WFI |
| SLEEP | Stop, main regulator, flash powered | WFI with clocks gated by `SLEEP_EN`, the timer still running |
| DEEP_SLEEP | Stop, low-power regulator, flash powered down | DORMANT: the crystal stopped, woken by the always-on timer or a GPIO |
| OFF | Standby: RAM lost except the 4 KiB backup SRAM | a powman state that turns the switched domains off |
| Timebase below IDLE | the RTC (subseconds, from the LSE crystal) | TIMER0 in SLEEP; the always-on timer on the low-power oscillator in DEEP_SLEEP |
| Restore on wake | the PLL relocks before the core returns to 180 MHz | the clocks restart |

**The RP2350's always-on timer runs from its low-power oscillator, which is not accurate.** Its error is measured (§8) and disclosed; the policy uses it through the tolerance constraint (§3.2).

## 3. Constraints

### 3.1 Wake latency (PWR-001)

```c
emb_pm_constraint_t c;
emb_pm_latency_get(&c, 50);   /* nothing may take longer than 50 µs to wake while c is held */
...
emb_pm_latency_put(&c);
```

- Threads and drivers hold constraints; the active bound is the smallest held. The policy never enters a state whose `exit_us` exceeds it.
- A driver with a request in progress whose hardware a state would stop holds a constraint that excludes that state (a UART receiving, a DMA transfer), so the device's work is never cut by sleep.
- Holding and releasing is O(1) per constraint and allowed from interrupts.

### 3.2 Timing tolerance (PWR-007)

`emb_pm_tolerance_get(&c, ppm)` bounds how far a deadline may drift. The policy enters a state only if its timebase's measured error, times the sleep's length, stays under the tightest tolerance held, and under one tick when none is held. This keeps deadlines correct across sleep, within each state's documented accuracy.

### 3.3 Other bounds

- **The watchdog:** the hardware watchdog keeps running in Stop and Standby (the STM32's IWDG runs from the LSI). The policy never sleeps past the next point the software watchdog of FLT-004 must feed it; it wakes to feed it, or refuses the state.
- **A debugger:** a probe attached in Stop needs `DBGMCU_CR` to keep its clocks, which changes the state; runs that measure power state it in their record.

## 4. The policy

The idle thread, with `CONFIG_EMB_PM`, calls the policy instead of `emb_arch_idle()` directly (PWR-004):

1. **Next deadline:** the earliest of the timers', the timeouts' and the time slice's (SPEC-003). States below IDLE need the tickless clock (ADR-059); with a periodic tick, the next deadline is the next tick, and only IDLE fits.
2. **Choose:** the deepest state whose `exit_us` fits the latency bound, whose break-even fits before the deadline, whose timebase meets the tolerance, and whose `loses` no held constraint forbids. None fits: IDLE.
3. **Enter:**
   - suspend the devices the state stops, in the inverse of their initialization order (PWR-002, SPEC-016 §4); a device that refuses (`EMB_EBUSY`) makes the policy fall back to a shallower state;
   - arm the wake-up on the state's timebase for the deadline, earlier by `exit_us`;
   - record the entry for the residency counters and trace `PM_ENTER`;
   - `emb_soc_power_enter(state)`, then `emb_arch_sleep()` (A108).
4. **Wake:**
   - `emb_soc_power_exit(state)` restores what the state lost (the PLL);
   - the kernel clock is corrected from the timebase that kept running (§5);
   - devices resume in initialization order; the residency counters advance; trace `PM_EXIT` with the cause;
   - the scheduler runs whatever the wake made ready.

The policy runs with interrupts masked from the decision to the sleep instruction, so an interrupt that arrives in between is not missed: the core wakes at once.

## 5. Time across sleep (PWR-007)

- **IDLE and the RP2350's SLEEP** keep the main timer running: nothing to correct.
- **States that stop it** sleep on their timebase: before entering, the policy reads the kernel's raw time and the timebase together; after waking, it advances kernel time by the timebase's elapsed count, converted, and reprograms the main timer.
- **The error** is the timebase's resolution plus its oscillator's drift over the sleep. The RTC's subseconds on the STM32 resolve 1/256 s at the default prescaler; the always-on timer's error is measured. Both are archived with the power run.

## 6. Residency and observability (PWR-003)

- Per state: entries, total time, longest stay, and the last wake's cause, through `emb_pm_residency(state, &stats)`.
- In the debug descriptor (appended indices, SPEC-015 §5), so a debugger reads them without the application.
- Two new trace events, `PM_ENTER` and `PM_EXIT`, with the state and the cause, added to SPEC-015 §3's set when M4.4 implements this (when the trace is on).

## 7. Configuration and the port

- `CONFIG_EMB_PM` (default `n`). Off, the idle loop is the port's idle instruction only (PWR-004).
- `CONFIG_EMB_PM_STATE_MAX` caps the deepest state an application allows, for development (a probe stays attached above it).
- **The port's part** (A108): `emb_arch_sleep()` is required only with `CONFIG_EMB_PM` and a manifest that declares `power.sleep: true`; otherwise the configuration refuses power management. On Cortex-M it sets SLEEPDEEP and executes `wfi`; what the SoC needs before and after is in `emb_soc_power_enter()` and `emb_soc_power_exit()`.
- OFF is entered only by an explicit `emb_pm_off(wake)`, never by the policy: waking from it is a reset.

## 8. Measurement (PWR-006)

`tools/hil/power_timing.py` measures each state on each board with the logic analyzer, reusing the controller timing tool's capture, VCD and statistics:

| | NUCLEO-F446RE | Pico 2 W |
|---|---|---|
| The wake event | the RTC's wake-up output on PC13 (RTC_OUT): the hardware's own edge (B1 shares the pin; it must not be pressed) | a pulse from the bench's NUCLEO on a wake GPIO |
| The woken marker | a GPIO the wake path sets first, before restoring clocks | the same |
| The state marker | a GPIO high while asleep, from just before the sleep instruction | the same |

- **Wake latency** is the marker's edge minus the wake event's, as a distribution over at least 10,000 wakes per state, archived with every edge.
- **Residency** is the state marker's high time, compared with the residency counters of §6; they must agree within the timebase's resolution.
- **Timebase error** is the deadline the kernel set against the analyzer's clock, over long sleeps.
- **Current is not measured.** When an instrument is on the bench (an INA219 for milliamperes, a power profiler for microamperes, [HIL](../engineering/hil.md)), its records join the same runs.

## 9. Verification

| Requirement | How |
|---|---|
| PWR-001 | a constraint below a state's `exit_us` keeps the policy out of it, on native and both boards; measured wake latency stays under the constraint (§8) |
| PWR-002 | the recorded suspend and resume order of a set of dependent devices |
| PWR-003 | the counters through the API and the descriptor, against the analyzer's residency |
| PWR-004 | `CONFIG_EMB_PM=n`: the idle loop's disassembly is the idle instruction, and no power symbol is linked |
| PWR-005 | the generator emits the states the description lists; the policy offers no other |
| PWR-006 | the archived `power` records of both boards (§8) |
| PWR-007 | the time conformance suite passes with power management on, on both boards; deadline error across deep sleep measured |

## 10. Defaults to confirm at review

- `min_residency_us` defaults to `entry_us + exit_us` while current is not measured.
- The tolerance constraint (§3.2) as the way the policy uses an inaccurate timebase, defaulting to one tick.
- OFF only on the application's explicit call, never chosen by the policy.
- Power management off by default, and states below IDLE only with the tickless clock.
- On the NUCLEO, the RTC's output on PC13 as the wake event, with B1 left alone during runs.
