# EmbLinkRTOS reference model

The executable reference model of ADR-013: the semantic oracle the kernel is tested against. This first module covers the wait and wake protocol (SPEC-004), the per-CPU context state and preemption points it depends on (SPEC-002 §2, §6), the timeout structure (SPEC-003 §5), and the fixed-priority scheduler (03 §2.1 with KRN-SCH-041), in uniprocessor form.

The model is written so that it can be read next to the specification: `embmodel/kernel.py` follows SPEC-004 §5 section by section, and `check_invariants()` is SPEC-004 §5.4 made executable. It is **not** kernel code and is never compiled into an image; divergence between the kernel and the model is a kernel bug unless the specification changes first (SPEC-004 §13).

## Layout

| Path | Contents |
|---|---|
| `embmodel/kernel.py` | State (`Thread`, `WaitQueue`, `Semaphore`, `Kernel`), the static op types (`Take`, `Give`, `Sleep`, `Yield`, `SetPrio`, `Suspend`, `Resume`, `Cancel`, `Destroy`, `SchedLock`, `SchedUnlock`, `Exit`), the protocol, interrupts and ticks, invariants |
| `embmodel/explore.py` | Exhaustive depth-first exploration of every interleaving at section granularity; seeded random walks |
| `tests/scenarios.py` | The scenario catalogue, including the worked example of SPEC-004 §15 |
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

After every action: invariants 1 to 6 of SPEC-004 §5.4 (queue membership matches the wait state; a thread is in the ready structure exactly when runnable and not current; PRIORITY_FIFO queues are ordered; at most one dequeue per wait generation; an armed timeout belongs to a waiting thread or to the open window; the result is set before READY), plus semaphore unit conservation (gives equal count plus units handed to waiters plus units taken immediately). At every terminal state: no runnable thread is left undispatched and no timeout is left armed for a blocked thread.

## Extending

Work item 5 adds the mutex with the inheritance algorithm of ADR-028 as a second object type over the same `_block_section`; work item 6 adds notification waits over the same protocol; the SMP model adds a second CPU, the three lock domains as explicit locks, and the superseded in-flight timeout bit (SPEC-004 §8).
