# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Support matrix tests (ADR-051, A81): tiers from evidence, the inventory's consistency, and
the committed page."""
import contextlib
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import matrix  # noqa: E402

CI = """
jobs:
  qemu:
    strategy: { matrix: { preset: [emu-a] } }
    steps:
      - run: cmake --build --preset ${{ matrix.preset }}
      - run: ctest --preset ${{ matrix.preset }} --output-on-failure
  build:
    steps:
      - run: |
          cmake --preset built-c
          cmake --build --preset built-c
"""


class Repo:
    def __init__(self):
        self.d = Path(tempfile.mkdtemp(prefix="support-"))
        for name in ("hwboard", "emuboard", "builtboard", "lonely"):
            b = self.d / "boards" / "v" / name
            (b / "generated").mkdir(parents=True)
            (b / "board.yaml").write_text("kind: board\n")
            (b / "generated" / "hw.json").write_text(json.dumps({"soc": {"part": "P1", "cpu": {"core": "c1"}}}))
        presets = [{"name": "hw-x", "cacheVariables": {"EMB_BOARD": "hwboard"}},
                   {"name": "emu-a", "cacheVariables": {"EMB_BOARD": "emuboard"}},
                   {"name": "built-c", "cacheVariables": {"EMB_BOARD": "builtboard"}},
                   {"name": "lonely-d", "cacheVariables": {"EMB_BOARD": "lonely"}}]
        (self.d / "CMakePresets.json").write_text(json.dumps({"configurePresets": presets}))
        (self.d / ".github" / "workflows").mkdir(parents=True)
        (self.d / ".github" / "workflows" / "ci.yml").write_text(CI)
        (self.d / "tools" / "evidence").mkdir(parents=True)
        self.configs({"emu-a": {"scope": "emulated"}, "hw-x": {"scope": "hardware", "unrun": "board"}})
        (self.d / "docs" / "engineering").mkdir(parents=True)
        (self.d / "docs" / "engineering" / "run.md").write_text("ran hw-x")
        self.runs([{"board": "hwboard", "preset": "hw-x", "build": "checked", "date": "2026-11-04",
                    "passed": 3, "total": 3, "report": "run.md"}])

    def configs(self, c):
        (self.d / "tools" / "evidence" / "configurations.json").write_text(json.dumps({"configurations": c}))

    def runs(self, r):
        (self.d / "docs" / "engineering" / "hardware-runs.json").write_text(json.dumps({"runs": r}))

    def close(self):
        shutil.rmtree(self.d)


class Tiers(unittest.TestCase):
    def setUp(self):
        self.repo = Repo()
        self.addCleanup(self.repo.close)

    def tiers(self):
        return {r["board"]: r["tier"] for r in matrix.derive(self.repo.d)}

    def test_each_tier_from_its_evidence(self):
        self.assertEqual(self.tiers(), {"hwboard": 1, "emuboard": 2, "builtboard": 3, "lonely": None})

    def test_a_failed_hardware_run_earns_nothing(self):
        self.repo.runs([{"board": "hwboard", "preset": "hw-x", "build": "checked", "date": "d",
                         "passed": 2, "total": 3, "report": "run.md"}])
        self.assertIsNone(self.tiers()["hwboard"])

    def test_a_hardware_scope_preset_in_ci_is_not_emulation(self):
        self.repo.configs({"emu-a": {"scope": "hardware"}, "hw-x": {"scope": "hardware"}})
        self.assertEqual(self.tiers()["emuboard"], 3)  # built in CI, not run on an emulator

    def test_inconsistencies_refused(self):
        self.repo.configs({"hw-x": {"scope": "hardware"}})
        with self.assertRaises(matrix.MatrixError) as cm:
            matrix.derive(self.repo.d)
        self.assertIn("configurations.json does not list", cm.exception.args[0][0])
        self.repo.configs({"emu-a": {"scope": "emulated"}})
        self.repo.runs([{"board": "hwboard", "preset": "emu-a", "build": "b", "date": "d", "passed": 1, "total": 1,
                         "report": "missing.md"}])
        with self.assertRaises(matrix.MatrixError) as cm:
            matrix.derive(self.repo.d)
        text = "\n".join(cm.exception.args[0])
        self.assertIn("is not a preset of that board", text)
        self.assertIn("no report docs/engineering/missing.md", text)

    def test_check_mode(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(matrix.main(["--root", str(self.repo.d), "--check"]), 1)
            self.assertEqual(matrix.main(["--root", str(self.repo.d)]), 0)
            self.assertEqual(matrix.main(["--root", str(self.repo.d), "--check"]), 0)
        page = (self.repo.d / matrix.PAGE).read_text()
        self.assertIn("| `hwboard` | P1 | c1 | 1 hardware | - | `hw-x` checked 3/3 (2026-11-04, [report](run.md)) |", page)
        self.assertIn("| `lonely` | P1 | c1 | untested | - | neither run nor built in CI |", page)

    def test_toolchains_from_the_presets(self):
        p = self.repo.d / "CMakePresets.json"
        d = json.loads(p.read_text())
        d["configurePresets"][1]["toolchainFile"] = "${sourceDir}/cmake/toolchains/arm-none-eabi-gcc.cmake"
        d["configurePresets"].append({"name": "emu-b", "inherits": "emu-a", "toolchainFile": "x/embcc-avr.cmake"})
        p.write_text(json.dumps(d))
        rows = {r["board"]: r for r in matrix.derive(self.repo.d)}
        self.assertEqual(rows["emuboard"]["toolchains"], "GCC")  # emu-b is not in CI: not claimed
        self.assertEqual(rows["builtboard"]["toolchains"], "host default")


class Repository(unittest.TestCase):
    def test_committed_page_is_current(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(matrix.main(["--check"]), 0)


if __name__ == "__main__":
    unittest.main()
