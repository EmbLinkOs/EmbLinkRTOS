# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""archive.py tests: records written as run_hil.py --archive writes them."""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import archive  # noqa: E402


def record(d, image, outcome="passed", commit="abc123", config="cfg1", board="arduino_uno", bench=None):
    rec = {"format": "emb-hil-run v1", "board": board, "image": image, "image_sha256": "0" * 64,
           "flashed_sha256": {image.replace(".elf", ".hex"): "1" * 64}, "outcome": outcome,
           "started": "2026-11-05T10:00:00Z", "ended": "2026-11-05T10:01:00Z",
           "git": {"commit": commit, "dirty": False}, "cases": [{"name": "a", "outcome": "passed"}],
           "fault_checks_by_reset": 2}
    if bench is None:
        rec["evidence"] = {"suite": image, "config": config}
    else:
        rec["bench"] = bench
    stem = image.rsplit(".", 1)[0]
    (d / f"{stem}.json").write_text(json.dumps(rec))
    (d / f"{stem}.log").write_text("hil: board\n")


class Archive(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="hilarch-"))
        self.src = self.tmp / "src"
        self.src.mkdir()

    def run_tool(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = archive.main([str(self.src), *argv])
        return rc, out.getvalue(), err.getvalue()

    def test_one_run_is_filed_with_its_index(self):
        record(self.src, "test_smoke.elf")
        record(self.src, "test_sem.elf", outcome="failed")
        record(self.src, "bench_switch.elf", bench=[{"name": "x", "units": 5, "ops": 1}])
        dest = self.tmp / "runs" / "r1"
        rc, out, _ = self.run_tool("--to", str(dest), "--preset", "avr-uno-hil", "--build", "checked")
        self.assertEqual(rc, 0, out)
        index = json.loads((dest / "index.json").read_text())
        self.assertEqual((index["passed"], index["total"]), (2, 3))
        self.assertEqual(index["configuration"], "cfg1")
        self.assertEqual(index["date"], "2026-11-05")
        self.assertTrue((dest / "test_sem.log").is_file())
        self.assertEqual(sum(i["fault_checks_by_reset"] for i in index["images"]), 6)

    def test_records_of_two_commits_are_refused(self):
        record(self.src, "test_smoke.elf", commit="aaa")
        record(self.src, "test_sem.elf", commit="bbb")
        rc, _, err = self.run_tool("--to", str(self.tmp / "d"), "--preset", "p", "--build", "checked")
        self.assertEqual(rc, 1)
        self.assertIn("more than one commit", err)

    def test_update_adds_the_run_with_its_archive(self):
        record(self.src, "test_smoke.elf")
        eng = self.tmp / "eng"
        eng.mkdir()
        runs = eng / "hardware-runs.json"
        runs.write_text(json.dumps({"_comment": "x", "runs": []}))
        rc, _, err = self.run_tool("--to", str(eng / "hil-runs" / "r1"), "--preset", "avr-uno-hil",
                                   "--build", "checked", "--report", "r.md", "--update", str(runs))
        self.assertEqual(rc, 0, err)
        entry = json.loads(runs.read_text())["runs"][0]
        self.assertEqual(entry["archive"], "hil-runs/r1")
        self.assertEqual((entry["passed"], entry["total"]), (1, 1))
        rc, _, _ = self.run_tool("--to", str(eng / "hil-runs" / "r1"), "--preset", "avr-uno-hil",
                                 "--build", "checked", "--report", "r.md", "--update", str(runs))
        self.assertEqual(len(json.loads(runs.read_text())["runs"]), 1)  # filed again, not twice

    def test_empty_directory(self):
        rc, _, err = self.run_tool("--to", str(self.tmp / "d"), "--preset", "p", "--build", "b")
        self.assertEqual(rc, 1)
        self.assertIn("no emb-hil-run v1 records", err)


if __name__ == "__main__":
    unittest.main()
