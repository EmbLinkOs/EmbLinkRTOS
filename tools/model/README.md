# EmbLinkRTOS reference model

The executable reference model of ADR-013: the semantic oracle the kernel is tested against. It covers the wait and wake protocol (SPEC-004), the mutex with priority inheritance, ceilings, recursion, hand-off, owner death and deadlock detection (SPEC-005 §2, §3), notifications and the object-to-notification binding (SPEC-006 §2, §3), thread start, join with exit-code hand-off and the one-joiner rule (SPEC-008 §4 to §6), the per-CPU context state and preemption points they depend on (SPEC-002 §2, §6), the timeout structure (SPEC-003 §5), and the fixed-priority scheduler (03 §2.1 with KRN-SCH-041), in uniprocessor form.

The model is written so that it can be read next to the specification: `embmodel/kernel.py` follows SPEC-004 §5 section by section, and `check_invariants()` is SPEC-004 §5.4 made executable. It is **not** kernel code and is never compiled into an image; divergence between the kernel and the model is a kernel bug unless the specification changes first (SPEC-004 §13).

## Layout

| Path | Contents |
|---|---|
| `embmodel/kernel.py` | State (`Thread`, `WaitQueue`, `Semaphore`, `Mutex`, `Kernel`), the static op types (`Take`, `Give`, `Lock`, `Unlock`, `NotifySet`, `NotifyWait`, `Start`, `Join`, `Sleep`, `Yield`, `SetPrio`, `Suspend`, `Resume`, `Cancel`, `Destroy`, `SchedLock`, `SchedUnlock`, `Exit`), the wait protocol, effective priority with `effective()` and `propagate()`, interrupts and ticks, invariants |
| `embmodel/explore.py` | Exhaustive depth-first exploration of every interleaving at section granularity; seeded random walks |
| `tests/scenarios.py` | The scenario catalogue: the SPEC-004 §15 worked example and the wait-protocol scenarios (`ALL`), plus the SPEC-005 §15 mutex scenarios (`MUTEX`): classic inversion, nested chain with timeouts, deadlock in both orders, owner death with and without a waiter, ceiling, waiter priority change, cancellation of a waiter, recursion, two-waiter hand-off; plus the SPEC-006 §11 notification scenarios (`NOTIFY`): set in the window, level semantics, ALL across two sets, one thread bound to two semaphores; plus the SPEC-008 §15 lifecycle scenarios (`JOIN`): join before and after exit, join with timeout, second joiner refused, canceled joiner, start of an inactive thread, exit while owning a mutex with a joiner |
| `tests/test_scenarios.py` | Specific interleavings with expected results |
| `tests/test_explore.py` | Exhaustive exploration of every scenario plus outcome properties |
| `tests/test_random.py` | Random walks over generated scenarios; a `hypothesis` stateful test when installed |

## Granularity

A kernel operation is a sequence of *sections* (SPEC-004 §5.1): code executed inside one lock domain. The model makes a section atomic and lets an interrupt (`isr` action), the timer (`tick` action), or nothing (`run` action) happen between sections. That is exactly the real system's atomicity on a uniprocessor, so every interleaving the explorer visits is one the hardware can produce, and no interleaving the hardware can produce is missed at that granularity.

## Running

```
cd tools/model
python3 -m unittest discover -v
python3 -c "from embmodel import explore; from tests import scenarios as S; print(explore(S.worked_example()).summary())"
```

Only the standard library is required; `pip install hypothesis` enables the stateful randomized test. The exhaustive tests take a few seconds.

## Differential bridge (TEST-008, SPEC-004 §13)

`bridge.py` runs every scenario of the catalogue on the kernel and replays the kernel's trace through the model:

```
cmake --preset native-gcc && cmake --build --preset native-gcc
python3 -I tools/model/bridge.py --runner build/native-gcc/tests/differential/diff_runner --runs 6 --seed 1
```

The runner (`tests/differential/diff_runner.c`, native port only) builds the scenario's threads, semaphores, mutexes and interrupt bodies from a one-line encoding, executes it under a schedule of interrupt raises and ticks (each placed after the n-th trace event, or at idle time), and prints the kernel's trace events, every operation's result, and the final states. The bridge follows that trace with the model at section granularity: at each step it applies the one enabled model action (run the current thread's next section, fire the interrupt the kernel fired, tick as the kernel ticked) whose events match the kernel's next events, with the model's invariants checked after every action; an interrupt or a tick may land after sections that emit nothing, which is the one place the search branches. One kernel tick event can stand for the idle-time jump to the next deadline, which the model takes as that many ticks with the earlier ones silent. At the end the per-operation results, the blocked and terminated sets and the semaphore counts are compared; a run that ends in a checked-build misuse fault must find the model refusing the same step or recording the error result the fault stands for.

The shared event vocabulary is `switch`, `wait_begin`, `wake`, `wait_end`, `notify_set`, `mutex_unlock`, `thread_exit`, `thread_start`, `tick`, `isr`; kernel events the model does not express (timeout arming, scheduler lock depth, priority changes, idle) are dropped, and the model's `mutex_lock`, `notify_dropped` and `stale_timeout` likewise. Queue and mutex addresses in the kernel's trace are mapped to the model's ids as they are first seen. The schedules are the natural run (interrupts at idle time), each interrupt as early as possible, then seeded random placements of interrupts and up to three explicit ticks; `--runs` and `--seed` choose how many. The runner reports its build (profile, owner-death policy, checked), and the bridge adapts the scenario (bitmap queues on the tiny profile, the owner-death policy) or skips what the build cannot express (shared priorities on the tiny profile, bitmap queues on the base profile). `ctest` runs the bridge as `differential_bridge` on every native preset; CI runs it per commit (05 §7).

Divergence is a kernel bug unless the specification changes first. The first campaign found three in the kernel: a notification set through a semaphore binding reached a terminated thread, a recursive re-lock left no trace, and the generated object storage was aligned to 8 where the control block needs 16 (all fixed in the same change). It found one in the model, against the specification: a relative timeout became a deadline at section 2 instead of at the call (SPEC-003 §5.2), which differs when a tick lands between the two; the model now fixes the deadline at the call and expires an already-due deadline when it is armed, as the kernel's timer programming does.

Known open item: deep campaigns (40 schedules per scenario) still show two runs out of about 1,800 where a tick lands right after a scheduler-lock or cancel section and the bridge cannot place it (`suspend_while_waiting` with seed 3, `cancel_waiter_disinherits` with seed 5). The CI campaign (6 schedules, seed 1) is clean on every native preset; these two are being triaged as replay-search limits before being called kernel or model bugs.

## What is checked

After every action: invariants 1 to 6 of SPEC-004 §5.4 (queue membership matches the wait state; a thread is in the ready structure exactly when runnable and not current; PRIORITY_FIFO queues are ordered; at most one dequeue per wait generation; an armed timeout belongs to a waiting thread or to the open window; the result is set before READY), semaphore unit conservation (gives equal count plus units handed to waiters plus units taken immediately), and the SPEC-005 invariants (every thread's effective priority equals `effective(base, owned)`; an inheriting mutex's owner is at least as high as every waiter on it; a ceiling mutex's owner is at least at the ceiling; an owner is never in its own queue; owned lists and owner fields agree; a free mutex has no waiters). At every terminal state: no runnable thread is left undispatched, no timeout is left armed for a blocked thread, and no blocked thread sits on an undetected inheritance cycle.

## Extending

Condition variables, event flags, barriers, and message queues are added when their conformance tests are written (SPEC-005 §17.11, SPEC-007); the SMP model adds a second CPU, the three lock domains as explicit locks, and the superseded in-flight timeout bit (SPEC-004 §8).
