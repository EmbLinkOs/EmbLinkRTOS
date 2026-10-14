"""Portable capture/replay and conservative reduction of differential failures."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import bridge
import evidence
from tests import scenarios


class Evidence(unittest.TestCase):
    def setUp(self):
        saved = json.loads((Path(__file__).resolve().parents[1] /
                            "evidence/m2-01/two-sleepers-native-gcc.json").read_text())
        self.sc = bridge.timer_regressions()[0][0]
        self.schedule = [("tick", 0, 10), ("tick", 0, 13), ("tick", 0, 15)]
        self.good = bridge.parse_run(saved["stdout"], saved["stderr"], saved["exit_code"])
        self.bad_stdout = saved["stdout"].replace("res 1 0 0 0", "res 1 0 -5 0")
        self.bad = bridge.parse_run(self.bad_stdout)
        self.error = evidence.verdict(self.sc, self.bad, self.bad.config, 20000)
        self.identity = evidence.identity("unused")
        self.bundle = evidence.make_bundle(self.sc, self.schedule, self.bad, self.error, "unused",
                                           fingerprint=self.identity)

    def roundtrip(self, bundle):
        with tempfile.TemporaryDirectory() as folder:
            path = evidence.write_bundle(bundle, Path(folder)/"capture.json")
            return evidence.read_bundle(path)

    def test_scenarios_roundtrip_without_catalogue_lookup_or_reordering(self):
        for sc in [f() for f in scenarios.ALL] + [bridge.Scenario(
                threads={17: (1, (bridge.Sleep(None),)), 4: (3, (bridge.Exit(7),))},
                inactive=(17,), tickless=False, name="non-catalogue")]:
            with self.subTest(name=sc.name):
                restored = evidence.scenario_from_data(json.loads(json.dumps(evidence.scenario_data(sc))))
                self.assertEqual(bridge.encode_scenario(restored), bridge.encode_scenario(sc))
                self.assertEqual(restored, sc)

    def test_offline_replay_needs_no_runner_and_keeps_failure(self):
        bundle, sc, schedule = self.roundtrip(self.bundle)
        with patch("bridge.run_kernel", side_effect=AssertionError("must not execute")):
            run, error = evidence.replay(bundle, sc, schedule)
        self.assertEqual(run.stdout, self.bad_stdout)
        self.assertEqual(error.signature, self.error.signature)

    def test_archived_agreement_is_reported_as_agreement(self):
        bundle = deepcopy(self.bundle)
        bundle["execution"]["stdout"] = self.good.stdout
        bundle, sc, schedule = self.roundtrip(bundle)
        _run, error = evidence.replay(bundle, sc, schedule)
        self.assertIsNone(error)               # never fabricate a formerly recorded failure

    def test_timeout_preserves_partial_output_and_infrastructure_category(self):
        timeout = subprocess.TimeoutExpired("runner", 0.1, output=b"partial stdout", stderr=b"partial stderr")
        with patch("bridge.subprocess.run", side_effect=timeout):
            run = bridge.run_kernel("runner", self.sc, [], 0.1)
        self.assertEqual((run.stdout, run.stderr, run.exit_code), ("partial stdout", "partial stderr", None))
        error = evidence.verdict(self.sc, run, {}, 20000)
        self.assertEqual(error.kind, "runner_timeout")
        bundle = evidence.make_bundle(self.sc, [], run, error, "unused", fingerprint=self.identity, timeout=0.1)
        loaded, sc, schedule = self.roundtrip(bundle)
        _run, replayed = evidence.replay(loaded, sc, schedule)
        self.assertEqual(replayed.signature, error.signature)

    def test_launch_exit_loss_and_malformed_output_stay_distinct(self):
        with patch("bridge.subprocess.run", side_effect=FileNotFoundError(2, "missing")):
            missing = bridge.run_kernel("missing", self.sc, [])
        malformed = bridge.execution_run("ev not-a-number\n", "stderr", 0)
        invalid_reason = bridge.parse_run(self.good.stdout)
        invalid_reason.events = [(e[0], e[1], e[2], e[3], 99, e[5])
                                 if e[0] == bridge.TR_BLOCK else e for e in invalid_reason.events]
        exited = bridge.execution_run("partial", "failure", 7)
        lost = bridge.parse_run(self.good.stdout)
        lost.events_lost = 2
        for run, kind in ((missing, "runner_launch"), (malformed, "malformed_trace"),
                          (invalid_reason, "malformed_trace"), (exited, "runner_exit"), (lost, "event_loss")):
            with self.subTest(kind=kind):
                self.assertEqual(evidence.verdict(self.sc, run, None, 20000).kind, kind)

    def test_changed_configuration_does_not_silently_adapt_replay(self):
        run = bridge.parse_run(self.good.stdout)
        run.config["profile"] = "tiny"
        error = evidence.verdict(self.sc, run, self.good.config, 20000)
        self.assertEqual(error.kind, "runner_config")

    def test_bundle_validation_rejects_tampering_and_unknown_operation_types(self):
        for field in ("encoding", "operation", "schedule", "version"):
            bundle = deepcopy(self.bundle)
            if field == "encoding":
                bundle["encoded_scenario"] = "changed"
            elif field == "operation":
                bundle["scenario"]["threads"][0]["program"][0]["op"] = "__import__"
            elif field == "schedule":
                bundle["schedule"] = [["isr", 99, 1]]
            else:
                bundle["version"] = 999
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.roundtrip(bundle)

    def test_capture_never_overwrites_existing_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            path = evidence.write_bundle(self.bundle, Path(folder)/"original.json")
            original = path.read_bytes()
            with self.assertRaises(FileExistsError):
                evidence.write_bundle({}, path)
            self.assertEqual(path.read_bytes(), original)

    def oracle(self, _runner, _sc, schedule, _timeout):
        return bridge.parse_run(self.bad_stdout if ("tick", 0, 13) in schedule else self.good.stdout)

    def test_reduction_preserves_failure_and_confirms_each_accepted_schedule(self):
        with patch("evidence.identity", return_value=self.identity), \
                patch("bridge.run_kernel", side_effect=self.oracle) as runner:
            result = evidence.reduce_schedule(self.bundle, self.sc, self.schedule, "unused")
        self.assertEqual(result["schedule"], [("tick", 0, 13)])
        self.assertEqual(result["failure"]["signature"], self.error.signature)
        self.assertEqual(result["reduction"]["outcome"], "one_entry_minimal")
        calls = [call.args[2] for call in runner.call_args_list]
        self.assertEqual(calls.count([("tick", 0, 13)]), 2)
        self.assertEqual(self.bundle["schedule"], self.schedule)

    def test_reduction_budget_retains_confirmed_input_without_minimality_claim(self):
        with patch("evidence.identity", return_value=self.identity), \
                patch("bridge.run_kernel", side_effect=self.oracle):
            result = evidence.reduce_schedule(self.bundle, self.sc, self.schedule, "unused", max_attempts=2)
        self.assertEqual(result["schedule"], self.schedule)
        self.assertEqual(result["reduction"]["outcome"], "budget_exhausted")
        self.assertEqual(result["reduction"]["attempts"], 2)

    def test_reduction_refuses_changed_or_flaky_baseline(self):
        for results in ((self.good,), (self.bad, self.good)):
            with patch("evidence.identity", return_value=self.identity), \
                    patch("bridge.run_kernel", side_effect=results), self.assertRaisesRegex(ValueError, "twice"):
                evidence.reduce_schedule(self.bundle, self.sc, self.schedule, "unused")

    def test_reduction_does_not_accept_a_different_failure(self):
        def changed(_runner, _sc, schedule, _timeout):
            return self.bad if schedule == self.schedule else bridge.execution_run("", "", 7)
        with patch("evidence.identity", return_value=self.identity), patch("bridge.run_kernel", side_effect=changed):
            result = evidence.reduce_schedule(self.bundle, self.sc, self.schedule, "unused")
        self.assertEqual(result["schedule"], self.schedule)

    def test_changed_capture_identity_is_retained_and_refuses_reduction(self):
        after = deepcopy(self.identity)
        after["source_sha256"]["tools/model/bridge.py"] = "changed"
        with patch("evidence.identity", return_value=after):
            bundle = evidence.make_bundle(self.sc, self.schedule, self.bad, self.error, "unused",
                                          fingerprint=self.identity)
        self.assertFalse(bundle["identity_consistent"])
        self.assertEqual(bundle["identity_after"], after)
        with self.assertRaisesRegex(ValueError, "changed during execution"):
            evidence.reduce_schedule(bundle, self.sc, self.schedule, "unused")


if __name__ == "__main__":
    unittest.main()
