# SPEC-013 - Native Port (`arch/native`)

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §13. Specification work item 12 of the roadmap (07 §3). Built in M1 alongside the AVR port (ADR-001).
**Requirements:** `docs/requirements/SIM.md` (SIM-001 to 003 from 04 §9 restated; new from 004).
**Builds on:** 02 §8 (the native port), 04 §9 (simulation layers); SPEC-011 (the contract it implements); SPEC-002 §13, SPEC-003 §13 (native rows); SPEC-004 §13 and `tools/model` (the oracle the differential bridge feeds); SPEC-010 §4.1 (software protection checks); 05 §4 (verification architecture); 09 §2 (EmbCC hosts: x86-64 Linux, macOS, AArch64, Windows incomplete); ADR-013.
**Research:** R-001 §9: Zephyr's `native_sim` and NuttX's simulator show that a host port is only useful when it is a *real* port held to the same conformance suite, and only deterministic when time is virtual and exactly one simulated thread runs at a time. This port is designed for determinism first and host fidelity second.

---

## 1. Purpose and limits

The native port runs the unmodified kernel in a host process so that:

- kernel semantics are tested in seconds with sanitizers, coverage, fuzzing, and the reference model as the oracle (SIM-001, TEST-008);
- the conformance suite is developed before any hardware runs it (M1);
- fault injection and interleaving control that hardware cannot offer are available (§6, §7).

It is never timing evidence (SIM-003), it does not measure footprint, and it cannot reproduce target-specific toolchain effects (`const` in RAM on AVR, lazy FPU stacking). Hardware and emulation cover those (04 §9).

## 2. Manifest

```yaml
arch: native
variants: [posix, win32]
word_bits: host
endian: host
stack: { align: 16, growth: down, min_thread: 0, context_frame: 0 }   # host stacks; sizes are hints only
features:
  irq_nesting: true           # simulated
  zero_latency_irqs: []
  cas: [posix, win32]         # C11 atomics
  interrupt_stack: true       # the injector runs on its own host thread
  hw_stack_limit: []
  mpu: software               # region checks in the gate and the marshallers; no trapping of plain accesses
  fpu_lazy: []
  cycle_counter: [posix, win32]
  tickless: true
  smp: false                  # one simulated CPU in 1.0; N CPUs reserved (§10)
  syscall_trap: simulated
  fault_entry: true           # host signals and injected faults
tcb_extension_bytes: host     # host thread handle, condition variable, gate state
```

## 3. Host abstraction

The port uses a minimal internal host layer (`embh_*`) with two backends:

| Primitive | POSIX | Win32 |
|---|---|---|
| `embh_thread_create/join` | `pthread_create` | `CreateThread` |
| `embh_mutex`, `embh_cond` | `pthread_mutex`, `pthread_cond` | `SRWLOCK`, `CONDITION_VARIABLE` |
| `embh_clock_now_ns` | `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` |
| `embh_sleep_ns` (wall-clock mode) | `nanosleep` | `WaitableTimer` |
| `embh_signal` (async interrupt mode) | `pthread_kill` with a real-time signal | `SuspendThread` + context inspection |
| `embh_fault_hook` | `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL` handlers | vectored exception handler |

Nothing else of the host is used (02 §8: non-POSIX hosts stay possible, matching EmbCC's host strategy). The C runtime of the host is used only by the test harness and the trace writer, never by the kernel under test.

## 4. The gate

One **gate** per simulated CPU: a host mutex, a condition variable per EmbLinkRTOS thread, and the identity of the one host thread allowed to run. Every EmbLinkRTOS thread is a host thread parked on its condition variable until the kernel dispatches it; `emb_arch_switch_to(next)` signals `next`'s variable and waits on the caller's, under the gate mutex, so exactly one simulated thread runs at any time and the kernel's scheduling decisions are exact (02 §8). The idle context and the interrupt injector are host threads of the same gate.

- `emb_arch_context_init` records entry, argument, and exit function in the TCB extension; the host thread is created lazily at the thread's first dispatch (SPEC-008 §4 `start`) with a trampoline that waits for the gate, calls `entry(arg)`, and calls `exit_fn(0)` on return.
- Thread exit (SPEC-008 §5) hands the gate to the next thread and then lets the host thread return; `emb_arch_switch_out_done` reports true once the host thread has released the gate mutex, which is the only "switch-out completion" this port has.
- Host stack sizes are generous and unrelated to the configured thread stack sizes; `emb_thread_stack_info` reports the configured size and a high-water mark of zero, and the stack guard check is a no-op (the port's documentation says so).

## 5. Interrupts

Two delivery modes, selected per test run:

- **Deterministic (default).** Interrupts are events queued by the test harness or by the virtual timer; they are delivered at the next **gate crossing** (any kernel entry or exit by the running thread, any `emb_arch_idle`, and explicit `emb_native_yield_point()` calls that tests may insert). Delivery runs the ISR body on the running host thread, inside the gate, with the kernel's `irq_nesting_depth` incremented and P1 applied at exit exactly as on hardware. Replaying the same event sequence reproduces the same interleaving.
- **Asynchronous (stress).** The injector host thread interrupts the running thread with a real-time signal (POSIX) or by suspending it and running the ISR on the injector's thread with the victim's state captured (Win32); delivery can land between any two host instructions, which exercises the critical-section discipline of the kernel the way hardware does. Not reproducible; used with sanitizers and the invariant checker (§7).

`emb_arch_irq_lock` sets the gate's `masked` flag and returns its previous value; while masked, deterministic delivery is deferred to the unlock and asynchronous delivery is refused by the signal handler, which re-queues the event. `emb_arch_in_isr` reads the kernel depth. Controller operations keep per-interrupt enable and pending bits in a table; `pend_soft` queues an event.

## 6. Time

Virtual time is the default: `now` is a counter that advances only when the simulated CPU is idle. `emb_arch_idle` asks the kernel for the next deadline (`embk_timeout_next`, SPEC-003 §5), advances `now` to it, and delivers the timer interrupt; if there is no deadline and no pending event, the run has finished or deadlocked and the harness reports which (SIM-005). Timeout and sleep tests therefore run in microseconds of wall time with exact tick arithmetic; the periodic-tick mode is simulated by delivering a timer interrupt every `CONFIG_EMB_TICK_NS` of virtual time.

Wall-clock mode (`EMB_NATIVE_WALLCLOCK=1`) maps virtual ticks to the host monotonic clock with `embh_sleep_ns` in idle, for interactive use and for host-side demos; conformance never runs in it.

`emb_arch_cycles` returns the host monotonic clock in nanoseconds in wall-clock mode and a per-step counter in virtual mode; neither is published as a measurement.

## 7. Faults, injection, and sanitizers

- Host faults (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`) in the running simulated thread are decoded into `emb_fault_info_t` and routed to `embk_fault_dispatch` as kernel faults (there is no hardware boundary, so a wild pointer in a "partition" is reported, not contained); the harness then records the crash and ends the run.
- **Partition faults are injected**: `emb_native_fault_inject(thread, class, code)` asks the gate to deliver a fault to that thread at its next gate crossing, exercising the supervisor path of SPEC-010 §6 deterministically. The marshallers' software access checks (`emb_arch_user_access_ok` over the generated region sets) return real `EMB_EPERM` statuses for out-of-region pointers, so the validation logic of SPEC-010 §5.4 is tested exactly; what is not tested is the hardware trap on a plain load, which the MPU ports cover.
- Builds with AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer are standard CI legs (GCC and Clang); EmbCC's `-fsanitize=undefined` traps in place (09 §7) and is a third leg. Coverage (`gcov`, `llvm-cov`) feeds the statement and branch numbers of the traceability matrix (TEST-009, R-003 T14).
- Fuzzing (libFuzzer or AFL++ harnesses under `tests/fuzz/`) drives the public API and the syscall marshallers with random arguments; the invariant checker (§8) runs after every call.

## 8. Differential testing against the reference model

The port emits the kernel's trace events (SPEC-015) to a host pipe as newline-delimited JSON; `tools/model/bridge.py` replays them into the reference model as operations and compares `wake_result`, queue order, priorities, and mutex ownership after every event (SPEC-004 §13 oracle). Each conformance scenario therefore has two verdicts: the kernel's own assertions and the model's agreement. A divergence fails the test and names the first diverging event. The same bridge consumes CTF traces from hardware runs (04 §9) at lower event rates, so the model also checks the ports.

A host-side **invariant checker** (`CONFIG_EMB_NATIVE_INVARIANTS`) calls into the kernel's checked-build invariant functions (SPEC-004 §5.4, SPEC-005 §11) after every kernel operation; in asynchronous mode it is the primary oracle because replay is impossible.

## 9. Determinism and replay

Given the same binary, the same configuration, and the same event schedule (a seed or a schedule file), a deterministic-mode run produces the same trace byte for byte. The harness records the schedule of every run; a failing run's schedule is attached to the failure and `--replay <file>` reproduces it. Host scheduling cannot affect the result because the gate serializes execution and time is virtual (SIM-004).

## 10. Multi-CPU simulation (FUTURE, interface reserved)

For the SMP work (ADR-012) the port grows to N gates, one per simulated CPU, each with its own running host thread; cross-CPU wake-ups become IPI events delivered at the target CPU's next gate crossing; spinlocks are real C11 atomics spun by concurrently running host threads. Deterministic mode then enumerates interleavings at gate crossings the way the reference model enumerates sections, which is what makes the SMP model and the SMP port comparable.

## 11. Build and CI

- Compilers: GCC and Clang on Linux and macOS, EmbCC on Linux (x86-64 and AArch64, 09 §2); Windows with MSVC-compatible Clang is a best-effort leg until EmbCC's Windows target completes.
- The port is a normal CMake target; `ctest` runs the conformance suite in deterministic virtual-time mode; sanitizer, coverage, fuzz, and asynchronous stress legs are separate presets.
- The reference configurations `tiny`, `base`, and `isolated` all build for native (profiles are configuration, 02 §7); the isolated configuration uses the simulated trap and software region checks.

## 12. Conformance

`tests/arch/native`: gate hand-over correctness (exactly one running thread, verified by a counter), deterministic delivery at gate crossings, deferred delivery while masked, virtual time advance to the next deadline, wall-clock mode sanity, fault injection delivery, replay reproducibility (two runs, identical traces), bridge agreement on the catalogue scenarios of `tools/model/tests/scenarios.py` transcribed as conformance tests. The full kernel conformance suite runs here first (SIM-001).

## 13. Decisions taken at acceptance (2026-10-07)

1. One host thread per EmbLinkRTOS thread behind a gate; the kernel's scheduling decisions are exact.
2. Deterministic interrupt delivery at gate crossings is the default; asynchronous signal-based delivery is a stress mode with the invariant checker as oracle.
3. Virtual time by default, advancing only in idle; wall-clock mode for interactive use only.
4. Host faults are kernel faults; partition faults are injected; software region checks test the marshallers exactly.
5. Trace events over a pipe feed the reference model through `tools/model/bridge.py`; every conformance scenario gets the model's verdict too.
6. Runs are replayable from a recorded schedule; a failure ships its schedule.
7. Host stack sizes are not the configured sizes; stack statistics are reported as not applicable on this port.
8. The multi-CPU gate design is reserved for the SMP work and mirrors the model's section-granularity exploration.
