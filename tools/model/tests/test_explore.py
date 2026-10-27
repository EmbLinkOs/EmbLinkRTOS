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


class NotifyExhaustive(unittest.TestCase):
    """SPEC-006 §11 / KRN-NOTIF-008."""

    def test_all_notify_scenarios_hold_invariants(self):
        for make in S.NOTIFY:
            sc = make()
            with self.subTest(scenario=sc.name):
                rep = explore(sc)
                self.assertGreater(rep.terminals, 0, rep.summary())

    def test_set_in_window_never_lost(self):
        rep = explore(S.notify_set_in_window())
        for outcome in rep.outcomes:
            o = dict(outcome)
            d = dict(o["threads"])
            bits = dict(o["notify_bits"])[1]
            if d[1][0][1] == Result.SATISFIED:
                self.assertEqual(d[1][0][2], 0x1)
            else:
                self.assertEqual(d[1][0][1], Result.TIMEOUT)   # the set landed after the deadline
            self.assertEqual(bits, 0)                         # consumed by CLEAR, or discarded at exit (SPEC-008 §5)

    def test_all_mode_delivers_exactly_the_mask(self):
        rep = explore(S.notify_all_two_sets())
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            first = d[1][0]
            if first[1] == Result.SATISFIED:
                self.assertEqual(first[2], 0x3)
            # bit 2 is set by T2 after bit 0; it is never consumed by the ALL wait on 0x3
            second = d[1][1]
            self.assertIn(second[1], (Result.SATISFIED, Result.TIMEOUT))

    def test_bound_thread_drains_every_unit_or_leaves_it_counted(self):
        rep = explore(S.bound_two_sems())
        for outcome in rep.outcomes:
            o = dict(outcome)
            d = dict(o["threads"])
            sems = dict(o["sems"])
            taken = sum(1 for (idx, r, _) in d[1] if r == Result.SATISFIED and idx in (1, 2, 4, 5))
            # two units were given in total; each is either taken by the bound thread or still in a count
            self.assertEqual(taken + sems[0] + sems[1], 2, outcome)


class LifecycleExhaustive(unittest.TestCase):
    """SPEC-008 §15 / KRN-THR-030."""

    def test_all_join_scenarios_hold_invariants(self):
        for make in S.JOIN:
            sc = make()
            with self.subTest(scenario=sc.name):
                rep = explore(sc)
                self.assertGreater(rep.terminals, 0, rep.summary())

    def test_join_delivers_the_exit_code(self):
        for make, code in ((S.join_before_exit, 7), (S.join_after_exit, 5), (S.start_inactive, 3),
                           (S.exit_owning_mutex_joined, 4)):
            rep = explore(make())
            with self.subTest(scenario=make.__name__):
                for outcome in rep.outcomes:
                    o = dict(outcome)
                    d = dict(o["threads"])
                    self.assertEqual(d[1][-1][1:], (Result.SATISFIED, code))   # the join is T1's last op
                    self.assertEqual(o["blocked"], ())

    def test_join_timeout_then_second_join_succeeds(self):
        rep = explore(S.join_timeout_then_exit())
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            self.assertEqual(d[1][0][1], Result.TIMEOUT)
            self.assertEqual(d[1][1][1:], (Result.SATISFIED, 9))

    def test_second_joiner_refused_first_gets_code(self):
        rep = explore(S.join_second_ebusy())
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            self.assertEqual(d[1][0][1:], (Result.SATISFIED, 1))
            self.assertEqual(d[2][0][1], Result.EPERM)

    def test_canceled_joiner_frees_the_slot(self):
        rep = explore(S.cancel_joiner())
        for outcome in rep.outcomes:
            d = dict(dict(outcome)["threads"])
            self.assertEqual(d[1][0][1], Result.CANCELED)
            self.assertIn(d[1][1][1], (Result.TIMEOUT, Result.SATISFIED))   # NO_WAIT join: T2 may or may not have exited


class BindingLifetime(unittest.TestCase):
    """M2-03 (ADR-046): no binding outlives its target; unbind stops signals deterministically."""

    @staticmethod
    def results(make):
        rep = explore(make())
        return rep, [dict(dict(o)["threads"]) for o in rep.outcomes]

    def test_every_binding_scenario_holds_invariants(self):
        for make in S.BINDING:
            sc = make()
            with self.subTest(scenario=sc.name):
                self.assertGreater(explore(sc).terminals, 0)

    def test_unbind_before_give_never_signals(self):
        _, outs = self.results(S.unbind_then_give)
        for d in outs:
            self.assertEqual([r[1] for r in d[1] if r[0] < 3], [Result.SATISFIED, Result.SATISFIED, Result.NOENT])
            self.assertEqual(d[2][0][1], Result.TIMEOUT)

    def test_isr_give_races_unbind_both_ways(self):
        _, outs = self.results(S.isr_give_vs_unbind)
        self.assertEqual({d[2][0][1] for d in outs}, {Result.SATISFIED, Result.TIMEOUT})

    def test_bound_target_is_destroyed_only_after_unbind(self):
        _, outs = self.results(S.destroy_target_while_bound)
        for d in outs:
            self.assertEqual([r[1] for r in d[1]],
                             [Result.SATISFIED, Result.SATISFIED, Result.BUSY, Result.SATISFIED, Result.SATISFIED])

    def test_destroying_the_object_releases_the_target(self):
        _, outs = self.results(S.destroy_bound_sem_releases_target)
        for d in outs:
            self.assertEqual(d[1][-1][1], Result.SATISFIED)
            self.assertIn(d[3][0][1], (Result.DESTROYED, Result.TIMEOUT))

    def test_terminated_target_refused_inactive_target_released_by_unbind(self):
        _, outs = self.results(S.bind_terminated_target)
        for d in outs:
            self.assertEqual([r[1] for r in d[1]][1:],
                             [Result.STATE, Result.SATISFIED, Result.BUSY, Result.SATISFIED, Result.SATISFIED])

    def test_suspended_target_receives_the_bit_at_resume(self):
        _, outs = self.results(S.suspended_target_accumulates)
        for d in outs:
            self.assertEqual(d[2][0][1], Result.SATISFIED)

    def test_rebind_signals_the_new_target_only(self):
        _, outs = self.results(S.rebind_to_another_thread)
        for d in outs:
            self.assertEqual([r[1] for r in d[1] if r[0] < 4],
                             [Result.SATISFIED, Result.EXIST, Result.SATISFIED, Result.SATISFIED])
            self.assertEqual(d[2][0][1], Result.TIMEOUT)          # the old target is never signaled
        self.assertIn(Result.SATISFIED, {d[3][0][1] for d in outs})


class EventFlags(unittest.TestCase):
    """M2-05 (SPEC-005 §5): ANY/ALL/CLEAR, deterministic consumption, no lost bit, binding after waiters."""

    @staticmethod
    def outcomes(make):
        return [(dict(dict(o)["threads"]), dict(dict(o)["events"])) for o in explore(make()).outcomes]

    def test_every_event_scenario_holds_invariants(self):
        for make in S.EVENT:
            sc = make()
            with self.subTest(scenario=sc.name):
                self.assertGreater(explore(sc).terminals, 0)

    def test_overlapping_clear_serves_each_waiter_once(self):
        for d, ev in self.outcomes(S.event_overlapping_clear):
            self.assertEqual((d[1][0][1], d[1][0][2]), (Result.SATISFIED, 1))
            self.assertEqual((d[2][0][1], d[2][0][2]), (Result.SATISFIED, 1))
            self.assertEqual(ev[0], 0)

    def test_all_waits_for_every_bit(self):
        for d, ev in self.outcomes(S.event_all_and_any):
            self.assertEqual(d[1][0][2], 0x3)
            self.assertEqual(d[2][0][2], 0x1)
            self.assertEqual(ev[0], 0x3)

    def test_isr_set_is_consumed_or_kept(self):
        seen = set()
        for d, ev in self.outcomes(S.event_isr_set_vs_timed_wait):
            r = d[1][0][1]
            seen.add(r)
            self.assertEqual(ev[0], 0 if r == Result.SATISFIED else 1)
        self.assertEqual(seen, {Result.SATISFIED, Result.TIMEOUT})

    def test_destroy_and_cancel_results(self):
        for d, _ in self.outcomes(S.event_destroy_and_cancel_waiters):
            self.assertEqual(d[1][0][1], Result.DESTROYED)
            self.assertEqual(d[2][0][1], Result.CANCELED)

    def test_binding_fires_for_what_waiters_left(self):
        for d, ev in self.outcomes(S.event_binding_after_direct_waiters):
            self.assertEqual((d[2][0][1], d[2][0][2]), (Result.SATISFIED, 1))
            self.assertEqual(ev[0], 0x2)
        self.assertIn(Result.SATISFIED, {d[1][0][1] for d, _ in self.outcomes(S.event_binding_after_direct_waiters)})

    def test_timeout_leaves_the_later_bit_set(self):
        for d, ev in self.outcomes(S.event_timeout_then_set):
            self.assertEqual(d[1][0][1], Result.TIMEOUT)
            self.assertEqual(ev[0], 1)
