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


class MutexExhaustive(unittest.TestCase):
    """SPEC-005 §15 / KRN-SYNC-038: every mutex scenario explored exhaustively."""

    def test_all_mutex_scenarios_hold_invariants(self):
        for make in S.MUTEX:
            sc = make()
            with self.subTest(scenario=sc.name):
                rep = explore(sc)
                self.assertGreater(rep.terminals, 0, rep.summary())

    def test_deadlock_always_detected_never_silent(self):
        rep = explore(S.deadlock_two_lockers())
        saw_deadlock = False
        for outcome in rep.outcomes:
            o = dict(outcome)
            d = dict(o["threads"])
            results = [r for res in d.values() for _, r, _ in res]
            if Result.DEADLOCK in results:
                saw_deadlock = True
            self.assertEqual(o["blocked"], (), "a terminal state must not leave a thread blocked")
        self.assertTrue(saw_deadlock)

    def test_owner_death_outcomes(self):
        from embmodel import OWNERDEAD_FLAG
        for make in (S.owner_death_with_waiter, S.owner_death_no_waiter):
            rep = explore(make())
            with self.subTest(scenario=make.__name__):
                for outcome in rep.outcomes:
                    o = dict(outcome)
                    d = dict(o["threads"])
                    # exactly one of T2, T3 learns about the owner's death; every mutex ends free
                    flags = [data for tid in (2, 3) for (_, r, data) in d[tid] if r == Result.SATISFIED]
                    self.assertEqual(flags.count(OWNERDEAD_FLAG), 1, outcome)
                    self.assertEqual(dict(o["mutex_owners"])[0], None)

    def test_inversion_scenarios_end_clean(self):
        for make in (S.classic_inversion, S.nested_chain_timeout, S.waiter_priority_change,
                     S.cancel_waiter_disinherits, S.two_waiters_handoff, S.ceiling_mutex, S.recursive_mutex):
            rep = explore(make())
            with self.subTest(scenario=make.__name__):
                for outcome in rep.outcomes:
                    o = dict(outcome)
                    self.assertEqual(o["blocked"], ())
                    for mid, owner in o["mutex_owners"]:
                        self.assertIsNone(owner, f"mutex {mid} still owned at a terminal state")
