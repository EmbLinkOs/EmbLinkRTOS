# SIM - Simulation and the Native Port

Group `SIM`. Design: `docs/specs/SPEC-013-native-port.md`; `docs/architecture/04-platform-architecture.md` §9. Related groups: PORT (contract), TEST (verification architecture), OBS (trace as the bridge's input), KRN-WAIT (the model as oracle).

SIM-001 to 003 originate in `docs/architecture/04-platform-architecture.md` §9 and are restated here as the authoritative copy. New requirements start at 004.

---

### SIM-001  Native port passes the conformance suite
**Statement.** The native port shall pass the full kernel conformance suite.
**Rationale.** It is a real port, not a mock (02 §8).
**Status.** Accepted 2026-10-07
**Verification.** CI: `ctest` on the native port for the tiny, base, and isolated configurations.
**Trace.** SPEC-013 §1, §11, §12

### SIM-002  One emulated target per architecture
**Statement.** Each supported architecture shall have at least one emulated target in CI.
**Rationale.** Real port code under an emulator catches what the native port cannot.
**Status.** Accepted 2026-10-07
**Verification.** CI matrix: QEMU AVR, QEMU or Renode Cortex-M, QEMU RISC-V.
**Trace.** 04 §9; 09 §9

### SIM-003  Emulated results are not timing evidence
**Statement.** Emulated results shall never be published as timing evidence.
**Rationale.** Only hardware tells the truth about time.
**Status.** Accepted 2026-10-07
**Verification.** Release checklist; harness metadata records the source of every number.
**Trace.** SPEC-013 §1; 05 §4.4

### SIM-004  Deterministic, replayable runs
**Statement.** In deterministic mode the native port shall deliver interrupts only at gate crossings, advance time only in idle, and reproduce the same trace from the same binary, configuration, and event schedule; the harness shall record every run's schedule and replay a given one.
**Rationale.** A race that cannot be reproduced cannot be fixed.
**Status.** Accepted 2026-10-07
**Verification.** Test: two runs with the same schedule produce byte-identical traces; replay of a recorded failing schedule reproduces the failure.
**Trace.** SPEC-013 §5, §6, §9

### SIM-005  Exact scheduling through the gate
**Statement.** Exactly one simulated thread of a simulated CPU shall execute at any time; the kernel's dispatch decisions shall be the only source of thread interleaving; a run with no runnable thread, no deadline, and no pending event shall be reported as finished or deadlocked.
**Rationale.** Host scheduling must not leak into kernel semantics.
**Status.** Accepted 2026-10-07
**Verification.** Test: running-thread counter never exceeds one; deadlock report test.
**Trace.** SPEC-013 §4, §6

### SIM-006  Asynchronous stress mode
**Statement.** An asynchronous delivery mode shall interrupt the running simulated thread at arbitrary host instructions and shall run with the invariant checker enabled.
**Rationale.** Exercises critical-section discipline the way hardware does.
**Status.** Accepted 2026-10-07
**Verification.** CI stress leg with sanitizers.
**Trace.** SPEC-013 §5, §7

### SIM-007  Fault injection and software region checks
**Statement.** The native port shall route host faults to the kernel fault path, inject partition faults on request at gate crossings, and implement `emb_arch_user_access_ok` over the generated region sets so that marshaller validation returns real statuses.
**Rationale.** The supervisor path and the validation logic are tested before the MPU ports exist.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/native/fault_inject`, `part/validate_*` on native.
**Trace.** SPEC-013 §7

### SIM-008  Differential bridge to the reference model
**Statement.** The native port shall emit kernel trace events to a host pipe; `tools/model/bridge.py` shall replay them into the reference model and compare results, queue order, priorities, and mutex ownership after every event; a divergence shall fail the test naming the first diverging event; the bridge shall also accept CTF traces from hardware runs.
**Rationale.** TEST-008: the model is the oracle for the kernel on every port.
**Status.** Accepted 2026-10-07
**Verification.** Test: the catalogue scenarios of `tools/model/tests/scenarios.py` as conformance tests with bridge agreement; a deliberately wrong kernel build diverges.
**Trace.** SPEC-013 §8; SPEC-004 §13; TEST-008

### SIM-009  Sanitizers, coverage, fuzzing
**Statement.** CI shall run the native conformance suite under AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer on GCC and Clang and under EmbCC's `-fsanitize=undefined`; coverage shall feed the traceability matrix; fuzz harnesses shall drive the public API and the marshallers with the invariant checker after every call.
**Rationale.** 05 §4; R-003 T14.
**Status.** Accepted 2026-10-07
**Verification.** CI legs present and green; coverage report generated.
**Trace.** SPEC-013 §7, §11

### SIM-010  Host portability of the port
**Statement.** The native port shall depend only on the `embh_*` host layer (threads, mutex, condition variable, monotonic clock, sleep, signal or suspend, fault hook) with POSIX and Win32 backends.
**Rationale.** Non-POSIX hosts stay possible (02 §8).
**Status.** Accepted 2026-10-07
**Verification.** Build on Linux, macOS, and Windows; CI grep for direct host API use outside `embh_*`.
**Trace.** SPEC-013 §3

### SIM-011  Multi-CPU reservation
**Statement.** The gate design shall extend to N simulated CPUs with IPI events at gate crossings for the SMP work, without changing the single-CPU interface.
**Rationale.** ADR-012; keeps the SMP model and the SMP port comparable.
**Status.** Accepted 2026-10-07 (FUTURE)
**Verification.** Design review at SMP specification time.
**Trace.** SPEC-013 §10
