"""Scenarios shared by the unit and exploration tests. Priorities: larger is more urgent."""
from embmodel import (Cancel, Destroy, Give, Policy, Resume, Scenario, SchedLock, SchedUnlock,
                      SetPrio, Sleep, Suspend, Take, Yield, FOREVER, NO_WAIT)


def worked_example():
    """SPEC-004 §15: T1 (prio 3) takes with a 10-tick timeout, T2 (prio 1) takes forever,
    an ISR gives once at an arbitrary time."""
    return Scenario(name="worked_example",
                    threads={1: (3, (Take(0, 10),)), 2: (1, (Take(0, FOREVER),))},
                    sems={0: dict(count=0)},
                    isrs=((Give(0),),))


def give_in_window():
    """A single taker and a single ISR give: the give may land in the window between
    sections 1 and 3, after commit, or before the take."""
    return Scenario(name="give_in_window",
                    threads={1: (2, (Take(0, 5),))},
                    sems={0: dict(count=0)},
                    isrs=((Give(0),),))


def two_gives_three_takers():
    return Scenario(name="two_gives_three_takers",
                    threads={1: (3, (Take(0, 4),)), 2: (2, (Take(0, 4),)), 3: (1, (Take(0, FOREVER),))},
                    sems={0: dict(count=0)},
                    isrs=((Give(0),), (Give(0),)))


def cancel_variants():
    """T2 (higher) cancels T1 before T1 ever blocks: the request is delivered at T1's next
    blocking call (KRN-WAIT-015)."""
    return Scenario(name="cancel_variants",
                    threads={1: (1, (Take(0, FOREVER), Take(0, NO_WAIT))), 2: (2, (Yield(), Cancel(1)))},
                    sems={0: dict(count=0)})


def cancel_blocked():
    """T1 (higher) blocks first; T2 then cancels it while BLOCKED."""
    return Scenario(name="cancel_blocked",
                    threads={1: (3, (Take(0, FOREVER), Take(0, NO_WAIT))), 2: (1, (Cancel(1),))},
                    sems={0: dict(count=0)})


def cancel_in_window():
    """T1 (low) takes forever; T2 (high) waits on another semaphore that an ISR gives at an
    arbitrary time, then cancels T1. When the ISR lands in T1's window, T2 preempts T1 between
    sections and cancels a thread in INTEND_TO_BLOCK."""
    return Scenario(name="cancel_in_window",
                    threads={1: (1, (Take(0, FOREVER), Take(0, NO_WAIT))), 2: (3, (Take(1, FOREVER), Cancel(1)))},
                    sems={0: dict(count=0), 1: dict(count=0)},
                    isrs=((Give(1),),))


def suspend_while_waiting():
    """T1 waits with a 3-tick timeout; T2 (higher) sleeps one tick, suspends T1 while it waits,
    sleeps again, resumes it. An ISR may give at any time, so the wake, the timeout, and the
    resume occur in every order (KRN-WAIT-014)."""
    return Scenario(name="suspend_while_waiting",
                    threads={1: (1, (Take(0, 3),)), 2: (3, (Sleep(1), Suspend(1), Sleep(1), Resume(1)))},
                    sems={0: dict(count=0)},
                    isrs=((Give(0),),))


def destroy_with_waiters():
    """T1 and T2 block (T2 with a timeout), then the lowest thread destroys the object with
    ABORT_WAITERS; T2's timeout may fire before the destroy."""
    return Scenario(name="destroy_with_waiters",
                    threads={1: (3, (Take(0, FOREVER), Take(0, NO_WAIT))), 2: (2, (Take(0, 6),)),
                             3: (1, (Destroy(0),))},
                    sems={0: dict(count=0)})


def requeue_on_priority_change():
    """Three waiters block while T4 sleeps; T4 then raises the lowest waiter above the others
    and gives three times: the raised waiter must be served first (KRN-WAIT-003)."""
    return Scenario(name="requeue_on_priority_change",
                    threads={1: (1, (Take(0, FOREVER),)), 2: (2, (Take(0, FOREVER),)), 3: (3, (Take(0, FOREVER),)),
                             4: (5, (Sleep(1), SetPrio(1, 4), Give(0), Give(0), Give(0)))},
                    sems={0: dict(count=0)})


def give_under_sched_lock():
    """T2 gives while holding the scheduler lock: T1 becomes READY at once but runs only
    after the unlock (SPEC-002 §5)."""
    return Scenario(name="give_under_sched_lock",
                    threads={1: (3, (Take(0, FOREVER),)), 2: (1, (SchedLock(), Give(0), SchedUnlock()))},
                    sems={0: dict(count=0)})


def preempted_goes_ahead():
    """KRN-SCH-041: T1 and T2 share a priority; T1 is preempted by T3 (woken from an ISR)
    and must run again before T2."""
    return Scenario(name="preempted_goes_ahead",
                    threads={1: (1, (Yield(), Give(1))), 2: (1, (Take(1, FOREVER),)), 3: (3, (Take(0, FOREVER),))},
                    sems={0: dict(count=0), 1: dict(count=0)},
                    isrs=((Give(0),),))


def no_wait_path():
    return Scenario(name="no_wait_path",
                    threads={1: (1, (Take(0, NO_WAIT), Give(0), Take(0, NO_WAIT)))},
                    sems={0: dict(count=0)})


def sleep_and_cancel():
    return Scenario(name="sleep_and_cancel",
                    threads={1: (1, (Sleep(4), Sleep(FOREVER))), 2: (2, (Yield(), Cancel(1)))},
                    sems={})


def binding_hook():
    """A give with no direct waiter sets the bound thread's bit; a give consumed by a
    direct waiter does not."""
    return Scenario(name="binding_hook",
                    threads={1: (2, (Take(0, 3),)), 2: (1, (Yield(),))},
                    sems={0: dict(count=0, binding=(2, 0))},
                    isrs=((Give(0),),))


def bitmap_queue():
    """Tiny profile wait set (ADR-036): unique priorities, wake-highest."""
    return Scenario(name="bitmap_queue",
                    threads={1: (1, (Take(0, FOREVER),)), 2: (2, (Take(0, FOREVER),)), 3: (3, (Sleep(1), Give(0), Give(0)))},
                    sems={0: dict(count=0, policy=Policy.BITMAP)})


ALL = [worked_example, give_in_window, two_gives_three_takers, cancel_variants, cancel_blocked, cancel_in_window,
       suspend_while_waiting,
       destroy_with_waiters, requeue_on_priority_change, give_under_sched_lock, preempted_goes_ahead,
       no_wait_path, sleep_and_cancel, binding_hook, bitmap_queue]


# ---------------------------------------------------------------------------------------
# SPEC-005: mutexes and priority inheritance
# ---------------------------------------------------------------------------------------
from embmodel import Lock, Unlock, Protocol  # noqa: E402


def classic_inversion():
    """v0.1 §8.3: L (prio 1) owns M0; H (prio 3) wakes and blocks on M0; M (prio 2) is runnable.
    With inheritance L runs at 3 until it unlocks, so M never runs while H waits."""
    return Scenario(name="classic_inversion",
                    threads={1: (1, (Lock(0), Yield(), Yield(), Unlock(0))),
                             2: (2, (Sleep(1), Yield(), Yield())),
                             3: (3, (Sleep(1), Lock(0), Unlock(0)))},
                    mutexes={0: {}})


def nested_chain_timeout():
    """H waits for A held by M, M waits for B held by L (v0.1 §8.3 nested donation); both waits
    have timeouts, so the chain is raised to 3 and lowered again in every order."""
    return Scenario(name="nested_chain_timeout",
                    threads={1: (1, (Lock(1), Sleep(3), Unlock(1))),
                             2: (2, (Sleep(1), Lock(0), Lock(1, 2), Unlock(0))),
                             3: (3, (Sleep(2), Lock(0, 2)))},
                    mutexes={0: {}, 1: {}})


def deadlock_two_lockers():
    """T1 locks A then B; T2 locks B then A. One of them must receive EMB_EDEADLK; no terminal
    state may hold an undetected cycle."""
    return Scenario(name="deadlock_two_lockers",
                    threads={1: (2, (Lock(0), Sleep(1), Lock(1), Unlock(1), Unlock(0))),
                             2: (1, (Lock(1), Lock(0), Unlock(0), Unlock(1)))},
                    mutexes={0: {}, 1: {}})


def owner_death_with_waiter():
    """T1 locks M and exits while T2 waits: T2's lock returns with the OWNERDEAD flag and T2
    owns the mutex; after T2 unlocks, T3 locks cleanly."""
    return Scenario(name="owner_death_with_waiter",
                    threads={1: (3, (Lock(0), Yield())),
                             2: (2, (Lock(0), Unlock(0))),
                             3: (1, (Lock(0), Unlock(0)))},
                    mutexes={0: {}})


def owner_death_no_waiter():
    """T1 locks M and exits with nobody waiting: the mutex is marked inconsistent; the next
    lock returns OWNERDEAD, its unlock clears the mark, the lock after that is clean."""
    return Scenario(name="owner_death_no_waiter",
                    threads={1: (3, (Lock(0),)),
                             2: (2, (Lock(0), Unlock(0))),
                             3: (1, (Lock(0), Unlock(0)))},
                    mutexes={0: {}})


def ceiling_mutex():
    """T1 (prio 1) locks a ceiling-3 mutex: it runs at 3 until unlock, so T2 (prio 2), woken
    by its sleep meanwhile, cannot preempt it."""
    return Scenario(name="ceiling_mutex",
                    threads={1: (1, (Lock(0), Yield(), Unlock(0))),
                             2: (2, (Sleep(1), Yield()))},
                    mutexes={0: dict(protocol=Protocol.CEILING, ceiling=3)})


def waiter_priority_change():
    """L owns M; H waits on it; T4 lowers then raises H's base priority: L's effective priority
    must follow in the same operation (KRN-SYNC-019, 032)."""
    return Scenario(name="waiter_priority_change",
                    threads={1: (1, (Lock(0), Sleep(4), Unlock(0))),
                             3: (3, (Sleep(1), Lock(0), Unlock(0))),
                             4: (5, (Sleep(2), SetPrio(3, 2), SetPrio(3, 4)))},
                    mutexes={0: {}})


def cancel_waiter_disinherits():
    return Scenario(name="cancel_waiter_disinherits",
                    threads={1: (1, (Lock(0), Sleep(3), Unlock(0))),
                             3: (3, (Sleep(1), Lock(0), Unlock(0))),
                             4: (5, (Sleep(2), Cancel(3)))},
                    mutexes={0: {}})


def recursive_mutex():
    return Scenario(name="recursive_mutex",
                    threads={1: (2, (Lock(0), Lock(0), Yield(), Unlock(0), Unlock(0))),
                             2: (1, (Lock(0), Unlock(0)))},
                    mutexes={0: dict(recursive=True)})


def two_waiters_handoff():
    """Two waiters of different priority; the unlock hands the mutex to the higher one, which
    then inherits nothing further; the second gets it on the next unlock (KRN-SYNC-021)."""
    return Scenario(name="two_waiters_handoff",
                    threads={1: (1, (Lock(0), Sleep(2), Unlock(0))),
                             2: (2, (Sleep(1), Lock(0), Unlock(0))),
                             3: (3, (Sleep(1), Lock(0), Unlock(0)))},
                    mutexes={0: {}})


MUTEX = [classic_inversion, nested_chain_timeout, deadlock_two_lockers, owner_death_with_waiter,
         owner_death_no_waiter, ceiling_mutex, waiter_priority_change, cancel_waiter_disinherits,
         recursive_mutex, two_waiters_handoff]
ALL = ALL + MUTEX
