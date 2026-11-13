# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""run_embsim.py's project mode: the project it writes for a board and its wires, and the
console it reads back from EmbSIM's report, through a stand-in for EmbSIM."""
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_embsim  # noqa: E402


class Project(unittest.TestCase):
    def test_nets_join_the_boards_ports(self):
        p = run_embsim.project("/tmp/x.elf", "emblink/nucleo_f446re", ["A0,A1"])
        self.assertEqual(p["boards"][0]["board"], "emblink/nucleo_f446re")
        self.assertEqual(p["boards"][0]["firmware"], {"image": str(Path("/tmp/x.elf").resolve()), "format": "elf"})
        self.assertEqual(p["nets"], {"W0": ["B1.A0", "B1.A1"]})
        self.assertEqual(p["policy"], "functional")

    def test_reads_the_console_and_stops_at_the_first_end(self):
        console = "TEST a\n  -> ok\n1 tests, 0 failed, 0 skipped, 3 ticks\nEMB_TEST_END\nTEST a\n"
        report = {"serial": {"B1": {"console": "USART2", "USART2": console}}}
        with tempfile.TemporaryDirectory() as d:
            fake = Path(d) / "embsim"
            fake.write_text("#!/bin/sh\ncat <<'EOF'\n" + json.dumps(report) + "\nEOF\n", encoding="utf-8")
            fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
            cwd = os.getcwd()
            os.chdir(d)
            try:
                rc = run_embsim.main([str(Path(d) / "t.elf"), "--embsim", str(fake), "--target", "b",
                                      "--net", "A0,A1", "--until", "1s", "--quiet"])
                written = json.loads((Path(d) / "t.elf.embsim.json").read_text(encoding="utf-8"))
            finally:
                os.chdir(cwd)
        self.assertEqual(rc, 0)
        self.assertEqual(written["nets"], {"W0": ["B1.A0", "B1.A1"]})


if __name__ == "__main__":
    unittest.main()
