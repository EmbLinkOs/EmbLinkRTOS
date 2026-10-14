#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Live native evidence workflow check using a deliberately small replay search budget."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    def command(script, *options, expected):
        result = subprocess.run([sys.executable, "-I", str(root/script), *map(str, options)],
                                capture_output=True, text=True, timeout=120)
        if result.returncode != expected:
            raise AssertionError(f"{script} exit {result.returncode}, expected {expected}: "
                                 f"{result.stdout}\n{result.stderr}")
        return result.stdout
    with tempfile.TemporaryDirectory(prefix="emblink-evidence-") as temporary:
        folder = Path(temporary)
        command("bridge.py", "--runner", args.runner, "--regressions", "--replay-steps", 1,
                "--capture-dir", folder/"captures", expected=1)
        captures = sorted((folder/"captures").glob("*.json"))
        assert len(captures) == 4
        selected = None
        for path in captures:
            bundle, _sc, _schedule = evidence.read_bundle(path)
            assert bundle["failure"]["kind"] == "replay_budget"
            assert bundle["identity"]["runner_sha256"]
            assert bundle["identity"]["build"]["config_sha256"]
            assert "signature matches" in command("evidence.py", "replay", path, expected=1)
            if bundle["scenario"]["name"] == "due_sleep_arm":
                selected = path
        assert selected is not None
        original = selected.read_bytes()
        assert "signature matches" in command("evidence.py", "replay", selected,
                                               "--runner", args.runner, expected=1)
        reduced = folder/"reduced.json"
        command("evidence.py", "reduce", selected, "--runner", args.runner, "--output", reduced,
                "--max-attempts", 20, "--max-seconds", 60, expected=0)
        bundle, _sc, schedule = evidence.read_bundle(reduced)
        assert schedule == [] and bundle["reduction"]["outcome"] == "one_entry_minimal"
        assert bundle["reduction"]["parent_sha256"] == evidence.sha256(selected)
        assert selected.read_bytes() == original
        assert "signature matches" in command("evidence.py", "replay", reduced, expected=1)
        # A tiny attempt budget must preserve the confirmed original and say it is incomplete.
        limited = folder/"limited.json"
        command("evidence.py", "reduce", selected, "--runner", args.runner, "--output", limited,
                "--max-attempts", 2, expected=1)
        assert json.loads(limited.read_text())["reduction"]["outcome"] == "budget_exhausted"
    print("evidence workflow: four controlled search-limit captures, offline/live replay, "
          "confirmed reduction and budget/parent preservation checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
