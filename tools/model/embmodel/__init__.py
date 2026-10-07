"""EmbLinkRTOS executable reference model (ADR-013).

Uniprocessor model of the wait and wake protocol (SPEC-004), the mutex with priority
inheritance, ceilings, recursion, hand-off, owner death and deadlock detection (SPEC-005),
the per-CPU context state (SPEC-002 §2), the timeout structure (SPEC-003 §5), and the
fixed-priority scheduler (03 §2.1, KRN-SCH-041). Kernel operations are written as the lock-domain sections of
SPEC-004 §5; a section is atomic, and interrupts or the timer may fire only between
sections. The explorer in `explore.py` enumerates every interleaving at that
granularity and checks the invariants of SPEC-004 §5.4 after every step.
"""
from .kernel import (  # noqa: F401
    Cancel, Destroy, Exit, Give, Kernel, Lock, ModelError, Mutex, Policy, Protocol, Reason,
    Result, Resume, SchedLock, SchedUnlock, Scenario, SetPrio, Sleep, Suspend, Take, Thread,
    Unlock, WaitState, Yield, FOREVER, NO_WAIT, OWNERDEAD_FLAG, PI_MAX_DEPTH, build,
)
from .explore import explore, random_walk, Report  # noqa: F401
