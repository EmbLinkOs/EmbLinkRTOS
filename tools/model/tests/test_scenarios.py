"""Deterministic scenario checks: specific interleavings with expected results."""
import unittest

from embmodel import Result, WaitState, build, explore
from tests import scenarios as S


def run(k, *acts):
    for a in acts:
        k.apply(a)
    return k


class WorkedExample(unittest.TestCase):
    def test_give_after_block_hands_off(self):
        k = build(S.worked_example())
        # T1 (prio 3) runs first: sections 1, 2, 3 -> blocked; T2 then blocks too.
        run(k, ("run",), ("run",), ("run",))
        self.assertEqual(k.threads[1].wait_state, WaitState.BLOCKED)
        self.assertTrue(k.timeout_armed(1))
        run(k, ("run",), ("run",), ("run",))
        self.assertEqual(k.threads[2].wait_state, WaitState.BLOCKED)
        self.assertIsNone(k.current)                      # idle
        run(k, ("tick",), ("tick",), ("tick",), ("isr", 0))
        t1 = k.threads[1]
        self.assertEqual(t1.wait_state, WaitState.READY)
        self.assertEqual(t1.wake_result, Result.SATISFIED)
        self.assertFalse(k.timeout_armed(1))              # disarmed by the wake
        self.assertEqual(k.sems[0].count, 0)              # hand-off: count unchanged
        self.assertEqual(k.current, 1)                    # P1 dispatched T1
        run(k, ("run",))
        self.assertEqual(t1.results[-1][1], Result.SATISFIED)

    def test_timeout_first_then_give_goes_to_lower_waiter(self):
        k = build(S.worked_example())
        run(k, *[("run",)] * 6)
        run(k, *[("tick",)] * 10)
        t1, t2 = k.threads[1], k.threads[2]
        self.assertEqual(t1.wake_result, Result.TIMEOUT)
        self.assertEqual(k.current, 1)
        run(k, ("run",))
        self.assertEqual(t1.results[-1][1], Result.TIMEOUT)
        run(k, ("run",))                                  # T1 exits
        run(k, ("isr", 0))
        self.assertEqual(t2.wake_result, Result.SATISFIED)
        self.assertEqual(k.stale_timeouts, 0)

    def test_give_in_window_wins_without_blocking(self):
        k = build(S.give_in_window())
        run(k, ("run",))                                  # section 1: INTEND_TO_BLOCK
        self.assertEqual(k.threads[1].wait_state, WaitState.INTEND_TO_BLOCK)
        run(k, ("isr", 0))                                # waker finds INTEND -> READY, no ready-structure change
        self.assertEqual(k.threads[1].wait_state, WaitState.READY)
        self.assertEqual(k.current, 1)
        run(k, ("run",))                                  # section 2 arms (wasted)
        self.assertTrue(k.timeout_armed(1))
        run(k, ("run",))                                  # section 3: commit fails, disarm, result
        self.assertFalse(k.timeout_armed(1))
        self.assertEqual(k.threads[1].results, ((0, Result.SATISFIED, 1),))

    def test_stale_timeout_is_counted_not_acted_on(self):
        k = build(S.give_in_window())
        run(k, ("run",), ("run",))                        # INTEND, armed with gen 0
        run(k, ("isr", 0))                                # wake in window: gen -> 1
        run(k, *[("tick",)] * 5)                          # deadline passes before section 3
        self.assertEqual(k.stale_timeouts, 1)
        run(k, ("run",))
        self.assertEqual(k.threads[1].results[0][1], Result.SATISFIED)


class CancelSuspendDestroy(unittest.TestCase):
    def test_cancel_pending_delivered_at_next_block(self):
        k = build(S.cancel_variants())
        # T2 (prio 2) runs first: Yield -> T2 still highest, continues; Cancel(1) while T1 not waiting.
        run(k, ("run",), ("run",))
        self.assertTrue(k.threads[1].cancel_pending)
        run(k, ("run",))                                  # T2 exits -> T1 runs
        run(k, ("run",))                                  # T1 section 1: CANCELED before enqueue
        self.assertEqual(k.threads[1].results[0][1], Result.CANCELED)
        self.assertFalse(k.threads[1].cancel_pending)

    def test_suspend_keeps_queue_position_and_deadline(self):
        k = build(S.suspend_while_waiting())
        run(k, ("run",), ("run",), ("run",))              # T2 sleeps one tick
        run(k, ("run",), ("run",), ("run",))              # T1 blocks with a 3-tick timeout
        self.assertEqual(k.threads[1].wait_state, WaitState.BLOCKED)
        run(k, ("tick",), ("run",))                       # T2 wakes, finishes its sleep
        run(k, ("run",))                                  # T2 suspends T1 while it waits
        t1 = k.threads[1]
        self.assertTrue(t1.suspended)
        self.assertEqual(t1.wait_state, WaitState.BLOCKED)
        self.assertEqual([tid for tid, _ in k.queues[0].waiters], [1])   # position kept
        self.assertTrue(k.timeout_armed(1))                            # deadline kept
        run(k, ("run",), ("run",), ("run",))              # T2 sleeps again -> idle
        run(k, ("tick",), ("tick",))                      # T1's deadline (3) passes while suspended
        self.assertEqual(t1.wake_result, Result.TIMEOUT)
        self.assertEqual(t1.wait_state, WaitState.READY)
        self.assertFalse(k.in_ready(1))                   # not made ready: still suspended
        run(k, ("run",), ("run",))                        # T2 finishes sleep, resumes T1
        self.assertTrue(k.in_ready(1) or k.current == 1)

    def test_wake_while_suspended_takes_effect_at_resume(self):
        from embmodel import Scenario, Take, Give, Suspend, Resume, FOREVER
        k2 = build(Scenario(name="y", threads={1: (3, (Take(0, FOREVER),)),
                                                 2: (1, (Suspend(1), Give(0), Resume(1)))},
                            sems={0: dict(count=0)}))
        run(k2, ("run",), ("run",), ("run",))              # T1 blocks
        run(k2, ("run",))                                  # T2 suspends T1 (overlay)
        self.assertEqual(k2.threads[1].wait_state, WaitState.BLOCKED)
        run(k2, ("run",))                                  # T2 gives: wake completes, T1 not made ready
        self.assertEqual(k2.threads[1].wait_state, WaitState.READY)
        self.assertFalse(k2.in_ready(1))
        self.assertEqual(k2.current, 2)
        run(k2, ("run",))                                  # T2 resumes T1 -> T1 preempts
        self.assertEqual(k2.current, 1)
        run(k2, ("run",))
        self.assertEqual(k2.threads[1].results[0][1], Result.SATISFIED)

    def test_destroy_flushes_in_queue_order_then_stale(self):
        from embmodel import Scenario, Take, Destroy, FOREVER, NO_WAIT
        sc = Scenario(name="d", threads={1: (3, (Take(0, FOREVER), Take(0, NO_WAIT))), 2: (2, (Take(0, 6),)),
                                          3: (1, (Destroy(0),))}, sems={0: dict(count=0)})
        k = build(sc)
        run(k, *[("run",)] * 6)                            # T1 then T2 block
        self.assertEqual([tid for tid, _ in k.queues[0].waiters], [1, 2])
        run(k, ("run",))                                   # T3 destroys
        self.assertEqual(k.threads[1].wake_result, Result.DESTROYED)
        self.assertEqual(k.threads[2].wake_result, Result.DESTROYED)
        self.assertFalse(k.timeout_armed(2))
        self.assertEqual(k.current, 1)                     # single reschedule, highest first
        run(k, ("run",), ("run",))                         # T1 finishes DESTROYED, then Take on destroyed -> STALE
        self.assertEqual([r for _, r, _ in k.threads[1].results], [Result.DESTROYED, Result.STALE])


class OrderAndScheduling(unittest.TestCase):
    def test_requeue_on_priority_change(self):
        from embmodel import Scenario, Take, SetPrio, Give, FOREVER, Sleep
        sc = Scenario(name="r", threads={1: (1, (Take(0, FOREVER),)), 2: (2, (Take(0, FOREVER),)),
                                          3: (3, (Take(0, FOREVER),)),
                                          4: (5, (Sleep(1), SetPrio(1, 4), Give(0), Give(0), Give(0)))},
                      sems={0: dict(count=0)})
        k = build(sc)
        run(k, ("run",), ("run",), ("run",))               # T4 sleeps 1 tick
        run(k, *[("run",)] * 9)                            # T3, T2, T1 block in priority order
        self.assertEqual([tid for tid, _ in k.queues[0].waiters], [3, 2, 1])
        run(k, ("tick",))                                  # T4 wakes
        run(k, ("run",))                                   # T4 finishes sleep op
        run(k, ("run",))                                   # SetPrio(1, 4): T1 moves to the head
        self.assertEqual([tid for tid, _ in k.queues[0].waiters], [1, 3, 2])
        run(k, ("run",))                                   # Give -> T1
        self.assertEqual(k.threads[1].wake_result, Result.SATISFIED)
        self.assertEqual([tid for tid, _ in k.queues[0].waiters], [3, 2])

    def test_give_under_sched_lock_defers_switch(self):
        k = build(S.give_under_sched_lock())
        run(k, ("run",), ("run",), ("run",))               # T1 blocks
        run(k, ("run",))                                   # T2 SchedLock
        run(k, ("run",))                                   # T2 Give: T1 READY, no switch
        self.assertEqual(k.threads[1].wait_state, WaitState.READY)
        self.assertTrue(k.reschedule_pending)
        self.assertEqual(k.current, 2)
        run(k, ("run",))                                   # SchedUnlock -> switch
        self.assertEqual(k.current, 1)

    def test_preempted_thread_goes_ahead_of_its_peers(self):
        k = build(S.preempted_goes_ahead())
        # T3 (prio 3) blocks on sem 0 first (3 sections), then T1 and T2 (prio 1) are ready, T1 first.
        run(k, ("run",), ("run",), ("run",))
        self.assertEqual(k.current, 1)
        run(k, ("isr", 0))                                 # T3 woken: preempts T1 -> T1 goes to the FRONT
        self.assertEqual(k.current, 3)
        self.assertEqual(k.ready[1], [1, 2])
        run(k, ("run",), ("run",))                         # T3 finishes take, exits
        self.assertEqual(k.current, 1)

    def test_no_wait_touches_nothing(self):
        k = build(S.no_wait_path())
        run(k, ("run",))
        self.assertEqual(k.threads[1].results[0][1], Result.TIMEOUT)
        self.assertEqual(k.queues[0].waiters, [])
        self.assertEqual(k.timeouts, [])
        run(k, ("run",), ("run",))
        self.assertEqual(k.threads[1].results[1][1], Result.SATISFIED)

    def test_binding_bit_set_only_without_direct_waiter(self):
        from embmodel import Scenario, Give, Yield
        # give with a blocked waiter: no bit
        k = build(S.binding_hook())
        run(k, ("run",), ("run",), ("run",))               # T1 blocks
        run(k, ("isr", 0))
        self.assertEqual(k.threads[2].notify_bits, 0)
        # give with nobody waiting: bit set on the bound thread
        k = build(Scenario(name="b", threads={2: (1, (Yield(),))}, sems={0: dict(count=0, binding=(2, 0))},
                           isrs=((Give(0),),)))
        run(k, ("isr", 0))
        self.assertEqual(k.threads[2].notify_bits, 1)

    def test_bitmap_queue_wakes_highest(self):
        from embmodel import Scenario, Take, Give, Sleep, Policy, FOREVER
        k = build(Scenario(name="bm", threads={1: (1, (Take(0, FOREVER),)), 2: (2, (Take(0, FOREVER),)),
                                               3: (3, (Sleep(1), Give(0), Give(0)))},
                           sems={0: dict(count=0, policy=Policy.BITMAP)}))
        run(k, *[("run",)] * 9)                            # T3 sleeps; T2 then T1 block
        run(k, ("tick",), ("run",), ("run",))              # T3 wakes, finishes sleep, gives
        self.assertEqual(k.threads[2].wake_result, Result.SATISFIED)
        self.assertIsNone(k.threads[1].wake_result)


if __name__ == "__main__":
    unittest.main()
