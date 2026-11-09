# FLT - Fault management

Group `FLT`. Design: `docs/architecture/03-kernel-architecture.md` §10; `docs/specs/SPEC-015-observability-formats.md` §4 (the crash record); `docs/specs/SPEC-011-architecture-port-contract.md` §11, §14. Related groups: OBS (the crash record's format), KRN-PART (partition faults), DRV (the watchdog class), PORT (the port's fault entry and reset).

FLT-001 to 004 originate in `docs/architecture/03-kernel-architecture.md` §10 and are restated here as the authoritative copy. New requirements start at 005.

---

## Requirements

### FLT-001  A crash record before the action
**Statement.** Every fatal fault shall write a versioned crash record to retained memory before taking the configured action.
**Rationale.** The record is what diagnoses a field failure after the reset (03 §10).
**Status.** Accepted 2026-10-07; implemented (A71 to A74): version 1, in memory the reset leaves alone, on every port; decoded by `tools/crashdec`.
**Verification.** Test: `test_crash` and `test_crash_reset` on native, QEMU, EmbSIM and the physical boards; Demonstration: the reference controller's injected fault decoded from its record (`docs/engineering/controller-nucleo-2026-11-07.md`).
**Trace.** 03 §10; SPEC-015 §4; A71; A73

### FLT-002  Partition faults to the supervisor
**Statement.** Partition faults shall be delivered to the supervisor as a notification with a fault descriptor; the kernel shall not decide policy.
**Rationale.** What to do about a failed partition is the system's decision, not the kernel's.
**Status.** Accepted 2026-10-07
**Verification.** Test: a fault in an isolated partition notifies the supervisor with its descriptor (with M3b's isolated profile).
**Trace.** 03 §10; SPEC-010; KRN-PART

### FLT-003  Recovery actions
**Statement.** Recovery actions shall include halt-for-debug, reboot, safe-state hook, and partition restart.
**Rationale.** Development wants a halt; a product wants a reboot or a safe state; an isolated system restarts the partition.
**Status.** Accepted 2026-10-07; partly implemented: halt (`CONFIG_EMB_FAULT_ACTION_HALT`), reboot (`CONFIG_EMB_FAULT_ACTION_REBOOT`, A106) and a stop for an attached debugger (`CONFIG_EMB_FAULT_HALT_FOR_DEBUG`); the safe-state hook and partition restart are not.
**Verification.** Test: the halt and reboot actions in `test_fault` and on the boards (A89); the safe-state hook and partition restart, planned.
**Trace.** 03 §10; A106

### FLT-004  A software watchdog over the hardware one
**Statement.** A software watchdog service shall aggregate per-thread or per-partition check-ins and feed the hardware watchdog only when all monitored parties have checked in within their windows.
**Rationale.** A hardware watchdog fed by one thread proves only that thread runs; every monitored party must have shown progress.
**Status.** Accepted 2026-10-07
**Verification.** Test: with one party missing its window, the hardware watchdog is not fed and the reset reason records it; Demonstration: a hung thread resets the board through the watchdog on both reference boards (M4.2).
**Trace.** 03 §10; SPEC-017 §8 (the watchdog class and the software watchdog, A120, A121)
