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


# ---------------------------------------------------------------------------------------
# SPEC-006: notifications and binding
# ---------------------------------------------------------------------------------------
from embmodel import NotifySet, NotifyWait  # noqa: E402


def notify_set_in_window():
    """The owner waits on bit 0 with a timeout; an ISR sets it at an arbitrary time."""
    return Scenario(name="notify_set_in_window",
                    threads={1: (2, (NotifyWait(0x1, timeout=5),))},
                    isrs=((NotifySet(1, 0x1),),))


def notify_level():
    """Bits set before the wait persist (level semantics): the wait returns at once."""
    return Scenario(name="notify_level",
                    threads={1: (1, (Sleep(1), NotifyWait(0x3, timeout=2))),
                             2: (2, (NotifySet(1, 0x2),))})


def notify_all_two_sets():
    """ALL mode across two separate sets from two sources; CLEAR consumes exactly the mask."""
    return Scenario(name="notify_all_two_sets",
                    threads={1: (1, (NotifyWait(0x3, all=True, timeout=6), NotifyWait(0x4, timeout=NO_WAIT))),
                             2: (3, (Sleep(1), NotifySet(1, 0x1), NotifySet(1, 0x4)))},
                    isrs=((NotifySet(1, 0x2),),))


def bound_two_sems():
    """SPEC-006 §3.3: one thread bound to two semaphores on bits 0 and 1 drains both with
    EMB_NO_WAIT takes after each notification wait; gives arrive from an ISR and a thread."""
    return Scenario(name="bound_two_sems",
                    threads={1: (1, (NotifyWait(0x3, timeout=4), Take(0, NO_WAIT), Take(1, NO_WAIT),
                                     NotifyWait(0x3, timeout=4), Take(0, NO_WAIT), Take(1, NO_WAIT))),
                             2: (2, (Sleep(1), Give(1)))},
                    sems={0: dict(count=0, binding=(1, 0)), 1: dict(count=0, binding=(1, 1))},
                    isrs=((Give(0),),))


NOTIFY = [notify_set_in_window, notify_level, notify_all_two_sets, bound_two_sems]
ALL = ALL + NOTIFY


# ---------------------------------------------------------------------------------------
# SPEC-008: start, join, exit codes
# ---------------------------------------------------------------------------------------
from embmodel import Exit, Join, Start  # noqa: E402


def join_before_exit():
    """T1 (high) joins T2 before T2 exits; the exit code 7 arrives by hand-off."""
    return Scenario(name="join_before_exit",
                    threads={1: (3, (Join(2),)), 2: (1, (Yield(), Exit(7)))})


def join_after_exit():
    """T2 exits first; T1's later join returns at once with the code."""
    return Scenario(name="join_after_exit",
                    threads={1: (1, (Sleep(2), Join(2))), 2: (3, (Exit(5),))})


def join_timeout_then_exit():
    """T1 joins with a 2-tick timeout; T2 sleeps 1 or 3 ticks (ISR-free: both orders via ticks)."""
    return Scenario(name="join_timeout_then_exit",
                    threads={1: (3, (Join(2, 2), Join(2))), 2: (1, (Sleep(3), Exit(9)))})


def join_second_ebusy():
    """Two joiners: the second is refused (modeled as EPERM standing in for EMB_EBUSY)."""
    return Scenario(name="join_second_ebusy",
                    threads={1: (3, (Join(3),)), 2: (2, (Join(3),)), 3: (1, (Yield(), Exit(1)))})


def cancel_joiner():
    """T1 waits on T2 forever; T4 cancels T1: the join returns CANCELED and T2's slot is freed."""
    return Scenario(name="cancel_joiner",
                    threads={1: (3, (Join(2), Join(2, NO_WAIT))), 2: (1, (Sleep(4), Exit(2))),
                             4: (2, (Sleep(1), Cancel(1)))})


def start_inactive():
    """T2 is created INACTIVE; an ISR or T1 starts it; T1 joins it."""
    return Scenario(name="start_inactive",
                    threads={1: (2, (Sleep(1), Start(2), Join(2))), 2: (1, (Exit(3),))},
                    inactive=(2,),
                    isrs=((Start(2),),))


def exit_owning_mutex_joined():
    """T2 exits owning M0 (RELEASE policy) while T1 joins it and T3 waits for M0."""
    return Scenario(name="exit_owning_mutex_joined",
                    threads={1: (3, (Join(2),)), 2: (1, (Lock(0), Yield(), Exit(4))),
                             3: (2, (Sleep(1), Lock(0), Unlock(0)))},
                    mutexes={0: {}})


JOIN = [join_before_exit, join_after_exit, join_timeout_then_exit, join_second_ebusy, cancel_joiner,
        start_inactive, exit_owning_mutex_joined]
ALL = ALL + JOIN


# ---------------------------------------------------------------------------------------
# M2-03: binding lifetime (SPEC-006 §3, ADR-046)
# ---------------------------------------------------------------------------------------
from embmodel import BindNotify, ThreadDestroy, UnbindNotify  # noqa: E402


def unbind_then_give():
    """T1 binds S0 to T2 and unbinds before giving: T2's notification wait times out."""
    return Scenario(name="unbind_then_give",
                    threads={1: (3, (BindNotify(0, 2, 0), UnbindNotify(0), UnbindNotify(0), Sleep(1), Give(0))),
                             2: (2, (NotifyWait(0x1, timeout=3),))},
                    sems={0: dict(count=0)})


def isr_give_vs_unbind():
    """An ISR gives S0 around T1's unbind: T2 gets the bit only if the give came first."""
    return Scenario(name="isr_give_vs_unbind",
                    threads={1: (3, (BindNotify(0, 2, 0), Sleep(1), UnbindNotify(0))),
                             2: (2, (NotifyWait(0x1, timeout=3),))},
                    sems={0: dict(count=0)},
                    isrs=((Give(0),),))


def destroy_target_while_bound():
    """T1 binds S0 to T2, joins it, and may destroy it only after unbinding."""
    return Scenario(name="destroy_target_while_bound",
                    threads={1: (3, (BindNotify(0, 2, 0), Join(2), ThreadDestroy(2), UnbindNotify(0),
                                     ThreadDestroy(2))),
                             2: (2, (Sleep(1), Exit(4)))},
                    sems={0: dict(count=0)})


def destroy_bound_sem_releases_target():
    """Destroying the bound semaphore releases the target; a waiter leaves with DESTROYED."""
    return Scenario(name="destroy_bound_sem_releases_target",
                    threads={1: (1, (BindNotify(0, 2, 0), Join(2), Destroy(0), ThreadDestroy(2))),
                             2: (2, (Exit(1),)),
                             3: (3, (Take(0, 2),))},
                    sems={0: dict(count=0)})


def bind_terminated_target():
    """Binding a terminated thread is refused; an INACTIVE one can be bound and then destroyed after unbind."""
    return Scenario(name="bind_terminated_target",
                    threads={1: (1, (Join(2), BindNotify(0, 2, 0), BindNotify(0, 3, 0), ThreadDestroy(3),
                                     UnbindNotify(0), ThreadDestroy(3))),
                             2: (2, (Exit(0),)),
                             3: (3, (Exit(0),))},
                    sems={0: dict(count=0)},
                    inactive=(3,))


def suspended_target_accumulates():
    """A bound target suspended while waiting keeps the handed-off bit and returns it at resume."""
    return Scenario(name="suspended_target_accumulates",
                    threads={1: (1, (BindNotify(0, 2, 0), Suspend(2), Give(0), Resume(2))),
                             2: (2, (NotifyWait(0x1),))},
                    sems={0: dict(count=0)})


def rebind_to_another_thread():
    """A second bind is refused until unbind; the rebound thread receives the next ready transition."""
    return Scenario(name="rebind_to_another_thread",
                    threads={1: (4, (BindNotify(0, 2, 0), BindNotify(0, 3, 1), UnbindNotify(0),
                                     BindNotify(0, 3, 1), Sleep(1), Give(0))),
                             2: (2, (NotifyWait(0x1, timeout=3),)),
                             3: (3, (NotifyWait(0x2, timeout=3),))},
                    sems={0: dict(count=0)})


BINDING = [unbind_then_give, isr_give_vs_unbind, destroy_target_while_bound, destroy_bound_sem_releases_target,
           bind_terminated_target, suspended_target_accumulates, rebind_to_another_thread]
ALL = ALL + BINDING


# ---------------------------------------------------------------------------------------
# M2-05: event flags (SPEC-005 §5)
# ---------------------------------------------------------------------------------------
from embmodel import EventDestroy, EventSet, EventWait  # noqa: E402


def event_overlapping_clear():
    """Two CLEAR waiters on the same bit: the higher one consumes the first set, the other the second."""
    return Scenario(name="event_overlapping_clear",
                    threads={1: (3, (EventWait(0, 0x1, clear=True),)), 2: (2, (EventWait(0, 0x1, clear=True),)),
                             3: (1, (EventSet(0, 0x1), EventSet(0, 0x1)))},
                    events={0: dict(bits=0)})


def event_all_and_any():
    """An ALL waiter wakes only when both bits are set; an ANY waiter at the first one."""
    return Scenario(name="event_all_and_any",
                    threads={1: (3, (EventWait(0, 0x3, all=True),)), 2: (2, (EventWait(0, 0x1),)),
                             3: (1, (EventSet(0, 0x1), EventSet(0, 0x2)))},
                    events={0: dict(bits=0)})


def event_isr_set_vs_timed_wait():
    """An ISR sets the bit at any point around a timed CLEAR wait: the bit is consumed or still set, never lost."""
    return Scenario(name="event_isr_set_vs_timed_wait",
                    threads={1: (2, (EventWait(0, 0x1, clear=True, timeout=3),))},
                    events={0: dict(bits=0)},
                    isrs=((EventSet(0, 0x1),),))


def event_destroy_and_cancel_waiters():
    """One waiter leaves with DESTROYED, another with CANCELED."""
    return Scenario(name="event_destroy_and_cancel_waiters",
                    threads={1: (4, (EventWait(0, 0x1),)), 2: (3, (EventWait(1, 0x1),)),
                             3: (1, (Sleep(1), Cancel(2), EventDestroy(0)))},
                    events={0: dict(bits=0), 1: dict(bits=0)})


def event_binding_after_direct_waiters():
    """The bound thread is notified only for bits the direct waiter left set."""
    return Scenario(name="event_binding_after_direct_waiters",
                    threads={1: (3, (NotifyWait(0x1, timeout=4),)), 2: (2, (EventWait(0, 0x1, clear=True),)),
                             3: (1, (EventSet(0, 0x1), EventSet(0, 0x2)))},
                    events={0: dict(bits=0, binding=(1, 0))})


def event_timeout_then_set():
    """A waiter times out before the set; the bit stays set for later."""
    return Scenario(name="event_timeout_then_set",
                    threads={1: (2, (EventWait(0, 0x1, timeout=1),)), 2: (1, (Sleep(2), EventSet(0, 0x1)))},
                    events={0: dict(bits=0)})


EVENT = [event_overlapping_clear, event_all_and_any, event_isr_set_vs_timed_wait, event_destroy_and_cancel_waiters,
         event_binding_after_direct_waiters, event_timeout_then_set]
ALL = ALL + EVENT


# ---------------------------------------------------------------------------------------
# M2-06: condition variables (SPEC-005 §6)
# ---------------------------------------------------------------------------------------
from embmodel import CondBroadcast, CondDestroy, CondSignal, CondWait  # noqa: E402


def cond_signal_not_lost():
    """The release and the enqueue are one section: the signal after it always finds the waiter."""
    return Scenario(name="cond_signal_not_lost",
                    threads={1: (2, (Lock(0), CondWait(0, 0), Unlock(0))),
                             2: (1, (Lock(0), CondSignal(0), Unlock(0)))},
                    mutexes={0: {}}, conds={0: {}})


def cond_timeout_relocks():
    """The wait times out while another thread holds the mutex; the relock waits for its unlock."""
    return Scenario(name="cond_timeout_relocks",
                    threads={1: (2, (Lock(0), CondWait(0, 0, timeout=2), Unlock(0))),
                             2: (1, (Lock(0), Sleep(4), Unlock(0)))},
                    mutexes={0: {}}, conds={0: {}})


def cond_cancel_during_relock():
    """A cancel sent while the signaled waiter relocks is deferred: the wait still reports the signal."""
    return Scenario(name="cond_cancel_during_relock",
                    threads={1: (3, (Lock(0), CondWait(0, 0), Unlock(0))),
                             2: (2, (Lock(0), CondSignal(0), Cancel(1), Unlock(0)))},
                    mutexes={0: {}}, conds={0: {}})


def cond_broadcast_wakes_all():
    """A broadcast wakes both waiters; each returns owning the mutex in turn."""
    return Scenario(name="cond_broadcast_wakes_all",
                    threads={1: (4, (Lock(0), CondWait(0, 0), Unlock(0))),
                             2: (3, (Lock(0), CondWait(0, 0), Unlock(0))),
                             3: (1, (Lock(0), CondBroadcast(0), Unlock(0)))},
                    mutexes={0: {}}, conds={0: {}})


def cond_destroy_aborts_waiter():
    """Destroying the condition variable aborts its waiter, which still relocks the mutex."""
    return Scenario(name="cond_destroy_aborts_waiter",
                    threads={1: (3, (Lock(0), CondWait(0, 0), Unlock(0))), 2: (2, (CondDestroy(0),))},
                    mutexes={0: {}}, conds={0: {}})


COND = [cond_signal_not_lost, cond_timeout_relocks, cond_cancel_during_relock, cond_broadcast_wakes_all,
        cond_destroy_aborts_waiter]
ALL = ALL + COND


# ---------------------------------------------------------------------------------------
# M2-07: message queues (SPEC-007 §2, KRN-IPC-022): modeled before the implementation
# ---------------------------------------------------------------------------------------
from embmodel import MsgDestroy, MsgPeek, MsgRecv, MsgSend, MsgSendFront  # noqa: E402


def msgq_fifo_order():
    """Capacity 2, three sends: the third waits for a slot; the receiver gets 1, 2, 3 in order."""
    return Scenario(name="msgq_fifo_order",
                    threads={1: (2, (MsgSend(0, 1), MsgSend(0, 2), MsgSend(0, 3))),
                             2: (1, (MsgRecv(0), MsgRecv(0), MsgRecv(0)))},
                    msgqs={0: dict(capacity=2)})


def msgq_receiver_handoff():
    """A waiting receiver gets the item directly; the ring stays empty."""
    return Scenario(name="msgq_receiver_handoff",
                    threads={1: (2, (MsgRecv(0),)), 2: (1, (MsgSend(0, 7),))},
                    msgqs={0: dict(capacity=1)})


def msgq_send_front_and_peek():
    """send_front puts an urgent item at the head and never waits; peek copies the head without removing it."""
    return Scenario(name="msgq_send_front_and_peek",
                    threads={1: (1, (MsgPeek(0), MsgSend(0, 1), MsgSendFront(0, 2), MsgSendFront(0, 3),
                                     MsgPeek(0), MsgRecv(0), MsgRecv(0), MsgRecv(0, NO_WAIT)))},
                    msgqs={0: dict(capacity=2)})


def msgq_isr_send_vs_timed_recv():
    """An ISR sends with EMB_NO_WAIT at any point around a timed receive: received or stored, never lost."""
    return Scenario(name="msgq_isr_send_vs_timed_recv",
                    threads={1: (2, (MsgRecv(0, 3),))},
                    msgqs={0: dict(capacity=1)},
                    isrs=((MsgSend(0, 5, NO_WAIT),),))


def msgq_isr_recv_frees_a_slot():
    """An ISR receive moves the waiting sender's item into the freed slot in the same section."""
    return Scenario(name="msgq_isr_recv_frees_a_slot",
                    threads={1: (2, (MsgSend(0, 1), MsgSend(0, 2, 4)))},
                    msgqs={0: dict(capacity=1)},
                    isrs=((MsgRecv(0, NO_WAIT),),))


def msgq_sender_timeout_and_cancel():
    """A full queue: one sender times out and one is canceled; neither item is ever stored."""
    return Scenario(name="msgq_sender_timeout_and_cancel",
                    threads={1: (3, (MsgSend(0, 1), MsgSend(0, 2, 2))), 2: (2, (MsgSend(0, 3),)),
                             3: (1, (Cancel(2), Sleep(3), MsgRecv(0), MsgRecv(0, NO_WAIT)))},
                    msgqs={0: dict(capacity=1)})


def msgq_destroy_aborts_both_sides():
    """Destroy wakes a waiting receiver and a waiting sender with DESTROYED and drops the stored item."""
    return Scenario(name="msgq_destroy_aborts_both_sides",
                    threads={1: (4, (MsgRecv(0),)), 2: (3, (MsgSend(1, 1), MsgSend(1, 2))),
                             3: (1, (MsgDestroy(0), MsgDestroy(1)))},
                    msgqs={0: dict(capacity=1), 1: dict(capacity=1)})


def msgq_binding_on_empty_to_nonempty():
    """The bound thread is notified when the ring goes from empty to nonempty, not on a hand-off."""
    return Scenario(name="msgq_binding_on_empty_to_nonempty",
                    threads={1: (3, (NotifyWait(0x1, timeout=4),)), 2: (2, (MsgRecv(0),)),
                             3: (1, (MsgSend(0, 1), MsgSend(0, 2), MsgSend(0, 3)))},
                    msgqs={0: dict(capacity=2, binding=(1, 0))})


def msgq_refill_does_not_signal_binding():
    """A receive that refills the freed slot from a waiting sender never leaves the queue empty:
    the bound thread is not notified again."""
    return Scenario(name="msgq_refill_does_not_signal_binding",
                    threads={1: (2, (MsgSend(0, 1), MsgSend(0, 2))),
                             2: (1, (NotifyWait(0x1, clear=True), MsgRecv(0), NotifyWait(0x1, timeout=NO_WAIT)))},
                    msgqs={0: dict(capacity=1, binding=(2, 0))})


MSGQ = [msgq_fifo_order, msgq_receiver_handoff, msgq_send_front_and_peek, msgq_isr_send_vs_timed_recv,
        msgq_isr_recv_frees_a_slot, msgq_sender_timeout_and_cancel, msgq_destroy_aborts_both_sides,
        msgq_binding_on_empty_to_nonempty, msgq_refill_does_not_signal_binding]
ALL = ALL + MSGQ


# ---------------------------------------------------------------------------------------
# M2-08: work queues (SPEC-006 §4): a queue thread drains a FIFO of items whose handlers run
# model operations; thread 9 is the queue's thread
# ---------------------------------------------------------------------------------------
from embmodel import Submit, WorkCancel, WorkCancelSync, WorkFlush, WorkqDestroy  # noqa: E402


def work_fifo_and_idempotent_submit():
    """Items run in submission order on the queue thread; a second submit of a queued item is EEXIST."""
    return Scenario(name="work_fifo_and_idempotent_submit",
                    threads={1: (2, (Submit(0, 0), Submit(0, 0), Submit(0, 1), Take(0), Take(1)))},
                    sems={0: dict(count=0), 1: dict(count=0)},
                    workqs={0: dict(tid=9, prio=1)},
                    works={0: dict(body=(Give(0),)), 1: dict(body=(Give(1),))})


def work_resubmit_while_running():
    """A submit while the handler runs is not lost: the item runs again after it returns."""
    return Scenario(name="work_resubmit_while_running",
                    threads={1: (1, (Submit(0, 0), Submit(0, 0), Take(0), Take(0)))},
                    sems={0: dict(count=0)},
                    workqs={0: dict(tid=9, prio=2)},
                    works={0: dict(body=(Sleep(1), Give(0)))})


def work_cancel_states():
    """cancel reports PENDING for a queued item, IDLE after, and RUNNING for a running handler;
    cancel_sync then waits for that handler to return."""
    return Scenario(name="work_cancel_states",
                    threads={1: (2, (Submit(0, 0), WorkCancel(0), WorkCancel(0), Submit(0, 0), Sleep(1),
                                     WorkCancel(0), WorkCancelSync(0)))},
                    workqs={0: dict(tid=9, prio=1)},
                    works={0: dict(body=(Sleep(3),))})


def work_flush_and_timeout():
    """A flush returns once everything queued before it ran; a timed-out flush leaves no marker."""
    return Scenario(name="work_flush_and_timeout",
                    threads={1: (2, (Submit(0, 0), WorkFlush(0, 1), Submit(0, 1), WorkFlush(0)))},
                    workqs={0: dict(tid=9, prio=1)},
                    works={0: dict(body=(Sleep(2),)), 1: dict(body=())})


def work_isr_submit():
    """An ISR submits at any point: the item runs exactly once."""
    return Scenario(name="work_isr_submit",
                    threads={1: (2, (Take(0, 4),))},
                    sems={0: dict(count=0)},
                    workqs={0: dict(tid=9, prio=1)},
                    works={0: dict(body=(Give(0),))},
                    isrs=((Submit(0, 0),),))


def workq_destroy_joins_its_thread():
    """Destroy of an idle queue stops its thread and returns after the thread exited."""
    return Scenario(name="workq_destroy_joins_its_thread",
                    threads={1: (2, (Submit(0, 0), WorkFlush(0), WorkqDestroy(0), Submit(0, 0)))},
                    workqs={0: dict(tid=9, prio=1)},
                    works={0: dict(body=())})


def work_flush_waits_for_running_handler():
    """A flush while a handler runs and nothing else is queued waits for that handler."""
    return Scenario(name="work_flush_waits_for_running_handler",
                    threads={1: (1, (Submit(0, 0), WorkFlush(0), WorkCancel(0)))},
                    workqs={0: dict(tid=9, prio=2)},
                    works={0: dict(body=(Sleep(2),))})


WORKQ = [work_fifo_and_idempotent_submit, work_resubmit_while_running, work_cancel_states, work_flush_and_timeout,
         work_isr_submit, workq_destroy_joins_its_thread, work_flush_waits_for_running_handler]
ALL = ALL + WORKQ


# ---------------------------------------------------------------------------------------
# M2-09: software timers (SPEC-003 §7): ISR_CONTEXT callbacks run their operations in the
# expiry pass; work queue callbacks run on queue 0's thread 9
# ---------------------------------------------------------------------------------------
from embmodel import TimerStart, TimerStop, TimerStopSync  # noqa: E402


def timer_one_shot_isr_context():
    """A one-shot ISR_CONTEXT timer gives the semaphore its thread waits on."""
    return Scenario(name="timer_one_shot_isr_context",
                    threads={1: (1, (TimerStart(0, 2), Take(0)))},
                    sems={0: dict(count=0)},
                    timers={0: dict(isr=True, body=(Give(0),))})


def timer_periodic_isr_stopped_by_thread():
    """A periodic timer expires on its grid until the thread stops it; a late stop sees one more."""
    return Scenario(name="timer_periodic_isr_stopped_by_thread",
                    threads={1: (1, (TimerStart(0, 1, 2), Take(0), Take(0), TimerStop(0)))},
                    sems={0: dict(count=0)},
                    timers={0: dict(isr=True, body=(Give(0),))}, horizon=10)


def timer_equal_deadlines_thread_first():
    """A thread timeout and a timer due at the same tick: the thread's expires first."""
    return Scenario(name="timer_equal_deadlines_thread_first",
                    threads={1: (1, (TimerStart(0, 2), Sleep(2))), 2: (3, (Take(0),))},
                    sems={0: dict(count=0)},
                    timers={0: dict(isr=True, body=(Give(0),))})


def timer_work_queue_callback():
    """A timer's callback runs on its work queue's thread."""
    return Scenario(name="timer_work_queue_callback",
                    threads={1: (1, (TimerStart(0, 2), Take(0)))},
                    sems={0: dict(count=0)},
                    workqs={0: dict(tid=9, prio=2)},
                    timers={0: dict(q=0, body=(Give(0),))})


def timer_stop_running_callback_and_stop_sync():
    """stop reports a running callback; stop_sync waits until it returns."""
    return Scenario(name="timer_stop_running_callback_and_stop_sync",
                    threads={1: (1, (TimerStart(0, 1), Sleep(2), TimerStop(0), TimerStopSync(0)))},
                    workqs={0: dict(tid=9, prio=2)},
                    timers={0: dict(q=0, body=(Sleep(2),))})


def timer_periodic_overrun_on_long_callback():
    """Expiries that find the callback's rerun already due are overruns, not extra runs."""
    return Scenario(name="timer_periodic_overrun_on_long_callback",
                    threads={1: (1, (TimerStart(0, 1, 1), Sleep(4), TimerStopSync(0)))},
                    workqs={0: dict(tid=9, prio=2)},
                    timers={0: dict(q=0, body=(Sleep(2),))}, horizon=10)


TIMER = [timer_one_shot_isr_context, timer_periodic_isr_stopped_by_thread, timer_equal_deadlines_thread_first,
         timer_work_queue_callback, timer_stop_running_callback_and_stop_sync, timer_periodic_overrun_on_long_callback]
ALL = ALL + TIMER


# ---------------------------------------------------------------------------------------
# M2-09: delayed work (SPEC-006 §4.3, KRN-WQ-003, KRN-WQ-007): an internal ISR_CONTEXT timer
# submits the item to the queue named at schedule time
# ---------------------------------------------------------------------------------------
from embmodel import DworkCancel, DworkSchedule  # noqa: E402


def dwork_runs_after_its_delay():
    """A scheduled item runs after its delay; a second schedule while it waits is EEXIST."""
    return Scenario(name="dwork_runs_after_its_delay",
                    threads={1: (1, (DworkSchedule(0, 0, 2), DworkSchedule(0, 0, 1), Take(0)))},
                    sems={0: dict(count=0)},
                    workqs={0: dict(tid=9, prio=2)},
                    works={0: dict(body=(Give(0),), delayed=True)})


def dwork_reschedule_and_cancel():
    """reschedule replaces the delay; cancel reports PENDING for a running delay, then IDLE."""
    return Scenario(name="dwork_reschedule_and_cancel",
                    threads={1: (1, (DworkSchedule(0, 0, 5), DworkSchedule(0, 0, 1, replace=True), Take(0),
                                     DworkSchedule(0, 0, 3), DworkCancel(0), DworkCancel(0)))},
                    sems={0: dict(count=0)},
                    workqs={0: dict(tid=9, prio=2)},
                    works={0: dict(body=(Give(0),), delayed=True)})


def dwork_isr_schedule():
    """An ISR schedules the item at any point; the thread's timed take sees it or times out."""
    return Scenario(name="dwork_isr_schedule",
                    threads={1: (1, (Take(0, 4),))},
                    sems={0: dict(count=0)},
                    workqs={0: dict(tid=9, prio=2)},
                    works={0: dict(body=(Give(0),), delayed=True)},
                    isrs=((DworkSchedule(0, 0, 1),),))


DWORK = [dwork_runs_after_its_delay, dwork_reschedule_and_cancel, dwork_isr_schedule]
ALL = ALL + DWORK
