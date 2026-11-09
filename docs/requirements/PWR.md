# PWR - Power management

Group `PWR`. Design: `docs/architecture/04-platform-architecture.md` §4; `docs/specs/SPEC-018-power-management.md` (planned, M4.0). Related groups: DRV (suspend and resume), KRN-TIM (tickless and the timebase), HW (SoC power states), OBS (residency counters and trace).

PWR-001 to 005 originate in `docs/architecture/04-platform-architecture.md` §4 and are restated here as the authoritative copy. PWR-006 and 007 were added with M4's plan (2026-11-08). New requirements start at 008.

The project has no instrument for current today. Energy and current are not measured; the power requirements below are about states, their residency and their wake latency, which a logic analyzer measures. Current measurement is deferred until an instrument is on the bench.

---

## Requirements

### PWR-001  Wake latency constraints
**Statement.** Threads and drivers shall be able to declare wake latency constraints; the policy shall never choose a state violating an active constraint.
**Rationale.** A deeper state saves more and wakes slower; the parts that cannot tolerate the delay must be able to forbid it.
**Status.** Accepted 2026-10-07
**Verification.** Test: with a constraint shorter than a state's exit latency, the policy never enters that state; Demonstration: the measured wake latency stays under the constraint on both reference boards (PWR-006).
**Trace.** 04 §4.1; SPEC-018

### PWR-002  Suspend in the inverse of initialization order
**Statement.** Device suspend and resume order shall follow the inverse of initialization order unless a device declares otherwise.
**Rationale.** A device is suspended before what it depends on (a peripheral before its clock and bus) and resumed after it.
**Status.** Accepted 2026-10-07
**Verification.** Test: a system of devices with dependencies records the suspend and resume order.
**Trace.** 04 §4.1; DRV-001; SPEC-016

### PWR-003  Residency counted and exposed
**Statement.** Power state residency shall be counted and exposed for energy accounting.
**Rationale.** Without a current instrument, residency is the evidence of what the policy did; with one, it is what energy is computed from.
**Status.** Accepted 2026-10-07
**Verification.** Test: the counters through the API and the debug descriptor; Demonstration: the counters agree with residency measured externally (PWR-006).
**Trace.** 04 §4.1; SPEC-018; OBS-008

### PWR-004  A compile-time option
**Statement.** Power management shall be a compile-time option; disabled, the idle path is the arch idle instruction only.
**Rationale.** A system that does not need it pays nothing for it.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: with `CONFIG_EMB_PM=n` the idle loop's disassembly is the port's idle instruction, and no power-management symbol is linked.
**Trace.** 04 §4.1; SPEC-018

### PWR-005  SoC power capabilities from the description
**Statement.** Dynamic voltage and frequency scaling, clock gating per peripheral, and power domains are SoC capabilities expressed in the hardware description and used by the policy where present.
**Rationale.** The policy is generic; what a SoC can do is data (ADR-004).
**Status.** Accepted 2026-10-07
**Verification.** Test: the generator emits the states and gates the SoC describes; the policy offers only those.
**Trace.** 04 §4.1; SPEC-014; SPEC-018

### PWR-006  Wake latency and residency measured per reference board
**Statement.** Each power state shall have its wake latency and residency measured on each reference board that claims it, with a hardware marker and an external instrument, and archived with the run.
**Rationale.** A state's exit latency is the input to the policy's choice (PWR-001); a figure from a datasheet is not evidence for this board and configuration.
**Status.** Proposed 2026-11-09
**Verification.** Demonstration: `tools/hil/power_timing.py` captures at least 10,000 wakes per state on the NUCLEO-F446RE and the Pico 2 W, with the distribution archived.
**Trace.** SPEC-018; PWR-001; PWR-003

### PWR-007  Time stays correct across sleep
**Statement.** Kernel time, deadlines and timers shall stay correct across every power state, within the documented accuracy of that state's timebase.
**Rationale.** A deep state stops the main timer; the clock must be corrected from the timebase that kept running, and a state whose timebase is too coarse for a pending deadline must not be chosen.
**Status.** Proposed 2026-11-09
**Verification.** Test: the time conformance suite passes with power management on, on both boards; Demonstration: deadline error across deep sleep measured against an external reference.
**Trace.** SPEC-018; ADR-059; KRN-TIM-017
