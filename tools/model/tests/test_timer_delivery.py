"""Timer delivery at the wait protocol's timeout-arm boundary (M2-01)."""
import json
from pathlib import Path
import unittest

import bridge
from embmodel import (Give, ModelError, Result, Scenario, Sleep, Take, WaitState, build, explore)
from tests import scenarios


class TimerDelivery(unittest.TestCase):
    def test_tiny_identity_metadata_keeps_zero_a_real_thread_and_replays_legacy(self):
        new = bridge.parse_run("config profile=tiny idle_index=254\nthread 7 index 0\n")
        new.events = [(bridge.TR_SWITCH, 0, 0, 255, 0, 0),
                      (bridge.TR_SWITCH, 0, 0, 0, 254, 0)]
        self.assertEqual(bridge.kernel_canonical(new), [("switch", 7), ("switch", None)])
        old = bridge.parse_run("config profile=tiny\nthread 7 index 3\n")
        old.events = [(bridge.TR_SWITCH, 0, 0, 255, 3, 0),
                      (bridge.TR_SWITCH, 0, 0, 3, 0, 0)]
        self.assertEqual(bridge.kernel_canonical(old), [("switch", 7), ("switch", None)])

    def due_wait(self, tickless=True):
        k = build(Scenario(threads={1: (1, (Take(0, 1),))}, sems={0: dict(count=0)},
                           tickless=tickless))
        k.apply(("run",))                     # call fixes deadline 1, not armed yet
        k.apply(("tick",))                    # counter reaches deadline during the window
        k.apply(("run",))                     # timeout arm requests delivery, still INTEND
        return k

    def test_already_due_compare_delivers_without_another_tick(self):
        k = self.due_wait()
        self.assertEqual(k.threads[1].wait_state, WaitState.INTEND_TO_BLOCK)
        self.assertIsNone(k.threads[1].wake_result)
        self.assertNotIn(("run",), k.enabled())
        k.apply(("timer",))
        self.assertEqual(k.now, 1)
        self.assertEqual(k.threads[1].wake_result, Result.TIMEOUT)
        k.apply(("run",))
        self.assertEqual(k.threads[1].results, ((0, Result.TIMEOUT, 0),))
        k.apply(("run",))                     # exit; no pending timer can strand the run
        self.assertEqual(k.enabled(), [])
        k.check_terminal()

    def test_tick_before_due_delivery_preserves_deadline_order_and_preemption(self):
        k = build(Scenario(threads={1: (1, (Sleep(1),)), 2: (3, (Sleep(2),))}))
        for _ in range(3):
            k.apply(("run",))                 # T2 blocks with deadline 2
        k.apply(("run",))                     # T1 fixes deadline 1
        k.apply(("tick",))
        k.apply(("run",))                     # already-due T1 is armed, delivery pending
        k.trace = []
        k.apply(("tick",))                    # raw counter advanced during the arm section
        wakes = [e[1] for e in k.trace if e[0] == "wake"]
        self.assertEqual(wakes, [1, 2])
        self.assertEqual(k.current, 2)         # P1 chooses the higher-priority thread
        self.assertFalse(k.timer_pending)
        self.assertEqual(k.timeouts, [])

    def test_periodic_already_due_arm_waits_for_the_next_tick(self):
        k = self.due_wait(tickless=False)
        self.assertNotIn(("timer",), k.enabled())
        k.apply(("run",))                     # periodic mode commits the wait
        self.assertEqual(k.threads[1].wait_state, WaitState.BLOCKED)
        k.apply(("tick",))
        self.assertEqual(k.now, 2)
        self.assertEqual(k.threads[1].wake_result, Result.TIMEOUT)

    def test_future_deadline_does_not_request_immediate_delivery(self):
        k = build(Scenario(threads={1: (1, (Sleep(2),))}))
        k.apply(("run",))
        k.apply(("run",))
        self.assertNotIn(("timer",), k.enabled())
        k.apply(("run",))
        k.apply(("tick",))
        self.assertEqual(k.threads[1].wait_state, WaitState.BLOCKED)
        k.apply(("tick",))
        self.assertEqual(k.threads[1].wake_result, Result.TIMEOUT)

    def test_wake_in_window_survives_already_due_timer_delivery(self):
        k = build(Scenario(threads={1: (1, (Take(0, 1),))}, sems={0: dict(count=0)},
                           isrs=((Give(0),),)))
        k.apply(("run",))
        k.apply(("tick",))
        k.apply(("isr", 0))                    # give wins before timeout arming
        k.apply(("run",))                      # the model retains a stale generation
        k.apply(("timer",))
        k.apply(("run",))
        self.assertEqual(k.threads[1].results, ((0, Result.SATISFIED, 1),))
        self.assertEqual(k.stale_timeouts, 1)
        self.assertEqual(k.sems[0].count, 0)

    def test_pending_delivery_is_not_a_terminal_state_or_thread_section(self):
        k = self.due_wait()
        with self.assertRaisesRegex(ModelError, "pending timer"):
            k.check_terminal()
        with self.assertRaisesRegex(ModelError, "pending timer"):
            k.apply(("run",))
        clone = k.clone()
        self.assertEqual(clone.key(), k.key())
        clone.timer_pending = False
        self.assertNotEqual(clone.key(), k.key())

    def test_two_sleeper_exploration_terminates_in_both_clock_modes(self):
        for tickless in (False, True):
            sc = Scenario(threads={1: (1, (Sleep(1),)), 2: (3, (Sleep(2),))},
                          tickless=tickless, name="two-sleepers")
            with self.subTest(tickless=tickless):
                report = explore(sc)
                self.assertGreater(report.terminals, 0)
                for outcome in report.outcomes:
                    self.assertEqual(dict(outcome)["blocked"], ())
                    for _tid, results in dict(outcome)["threads"]:
                        self.assertEqual(results, ((0, Result.TIMEOUT, 0),))


class ArchivedReplays(unittest.TestCase):
    def two_sleepers(self):
        return Scenario(name="due_sleep_arm",
                        threads={1: (1, (Sleep(1),)), 2: (3, (Sleep(2),))})

    def test_captured_timer_boundary_traces_agree(self):
        folder = Path(__file__).resolve().parents[1] / "evidence/m2-01"
        captures = [p for p in sorted(folder.glob("*.json"))
                    if "encoded_scenario" in json.loads(p.read_text())]
        self.assertEqual(len(captures), 7)
        for path in captures:
            saved = json.loads(path.read_text())
            with self.subTest(capture=path.name):
                if saved["scenario"] == "suspend_while_waiting":
                    sc = scenarios.suspend_while_waiting()
                elif saved["scenario"] == "cancel_waiter_disinherits":
                    sc = scenarios.cancel_waiter_disinherits()
                else:
                    sc = Scenario(name="due_sleep_arm",
                                  threads={1: (1, (Sleep(1),)), 2: (3, (Sleep(2),))})
                sc = bridge.adapt(sc, saved["config"])
                self.assertEqual(bridge.encode_scenario(sc), saved["encoded_scenario"])
                run = bridge.parse_run(saved["stdout"], saved["stderr"], saved["exit_code"])
                self.assertEqual(run.events_lost, 0)
                bridge.Replay(sc, run).replay()  # validates events, results and final states

    def test_replay_still_rejects_early_expiry_and_wrong_results(self):
        path = Path(__file__).resolve().parents[1] / "evidence/m2-01/two-sleepers-native-gcc.json"
        saved = json.loads(path.read_text())
        for corruption in ("early expiry", "wrong result"):
            with self.subTest(corruption=corruption):
                run = bridge.parse_run(saved["stdout"], saved["stderr"], saved["exit_code"])
                if corruption == "early expiry":
                    run.events = [(e[0], e[1], 1, 1, e[4], e[5])
                                  if e[0] == bridge.TR_TICK and e[3] == 2 else e
                                  for e in run.events]
                else:
                    run.results[1][0] = (-5, 0)  # an unexplained canceled sleep
                with self.assertRaises(bridge.Divergence):
                    bridge.Replay(self.two_sleepers(), run).replay()


if __name__ == "__main__":
    unittest.main()
