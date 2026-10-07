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

## What is checked

After every action: invariants 1 to 6 of SPEC-004 §5.4 (queue membership matches the wait state; a thread is in the ready structure exactly when runnable and not current; PRIORITY_FIFO queues are ordered; at most one dequeue per wait generation; an armed timeout belongs to a waiting thread or to the open window; the result is set before READY), semaphore unit conservation (gives equal count plus units handed to waiters plus units taken immediately), and the SPEC-005 invariants (every thread's effective priority equals `effective(base, owned)`; an inheriting mutex's owner is at least as high as every waiter on it; a ceiling mutex's owner is at least at the ceiling; an owner is never in its own queue; owned lists and owner fields agree; a free mutex has no waiters). At every terminal state: no runnable thread is left undispatched, no timeout is left armed for a blocked thread, and no blocked thread sits on an undetected inheritance cycle.

## Extending

Condition variables, event flags, barriers, and message queues are added when their conformance tests are written (SPEC-005 §17.11, SPEC-007); the SMP model adds a second CPU, the three lock domains as explicit locks, and the superseded in-flight timeout bit (SPEC-004 §8).
