"""Exhaustive exploration of every scenario (KRN-WAIT-007, KRN-WAIT-022) and outcome checks."""
import unittest

from embmodel import Result, explore
from tests import scenarios as S


class Exhaustive(unittest.TestCase):
    def test_all_scenarios_hold_invariants(self):
        for make in S.ALL:
            sc = make()
            with self.subTest(scenario=sc.name):
                rep = explore(sc)
                self.assertGreater(rep.terminals, 0, rep.summary())

    def test_worked_example_outcomes(self):
        rep = explore(S.worked_example())
        results = set()
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            results.add((d[1][0][1], d[2][0][1] if d[2] else None))
        # T1 either gets the unit or times out; when T1 times out the unit goes to T2 if the give
        # happens after the timeout, else T1 got it and T2 waits forever.
        self.assertIn((Result.SATISFIED, None), results)
        self.assertIn((Result.TIMEOUT, Result.SATISFIED), results)
        for r1, r2 in results:
            self.assertFalse(r1 == Result.SATISFIED and r2 == Result.SATISFIED, "one unit, two takers satisfied")

    def test_give_in_window_never_loses_the_unit(self):
        rep = explore(S.give_in_window())
        for outcome in rep.outcomes:
            o = dict(outcome)
            d, sems = dict(o["threads"]), dict(o["sems"])
            # Either the taker got the unit, or it timed out before the give and the unit is still there.
            if d[1][0][1] == Result.SATISFIED:
                self.assertEqual(sems[0], 0)
            else:
                self.assertEqual(d[1][0][1], Result.TIMEOUT)
                self.assertEqual(sems[0], 1, "a give that misses the taker must leave the unit in the count")

    def test_cancel_always_cancels_the_forever_wait(self):
        rep = explore(S.cancel_variants())
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            self.assertEqual(d[1][0][1], Result.CANCELED)
            self.assertEqual(d[1][1][1], Result.TIMEOUT)      # the NO_WAIT take after it

    def test_cancel_always_wins_against_a_forever_wait(self):
        for make in (S.cancel_blocked, S.cancel_in_window):
            rep = explore(make())
            with self.subTest(scenario=make.__name__):
                self.assertGreater(len(rep.outcomes), 0)
                for outcome in rep.outcomes:
                    d = dict(dict(outcome)["threads"])
                    self.assertEqual(d[1][0][1], Result.CANCELED)
                    self.assertEqual(d[1][1][1], Result.TIMEOUT)

    def test_suspend_while_waiting_outcomes(self):
        rep = explore(S.suspend_while_waiting())
        seen = set()
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            seen.add(d[1][0][1])
        self.assertEqual(seen, {Result.SATISFIED, Result.TIMEOUT})

    def test_requeue_serves_the_raised_waiter_first(self):
        rep = explore(S.requeue_on_priority_change())
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            self.assertEqual(d[1][0][1], Result.SATISFIED)

    def test_destroy_outcomes(self):
        rep = explore(S.destroy_with_waiters())
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            # T1 is flushed with DESTROYED if it was waiting, or finds the object already gone (STALE);
            # its second, NO_WAIT take is always STALE.
            self.assertIn(d[1][0][1], (Result.DESTROYED, Result.STALE))
            self.assertEqual(d[1][1][1], Result.STALE)
            self.assertIn(d[2][0][1], (Result.DESTROYED, Result.TIMEOUT, Result.STALE))


if __name__ == "__main__":
    unittest.main()
