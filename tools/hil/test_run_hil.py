# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""run_hil tests without a board: a pseudo-terminal stands in for the ST-LINK's serial port
and a stand-in flash command signals a thread that plays the board's console output."""
import io
import json
import os
import pty
import sys
import tempfile
import threading
import time
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_hil  # noqa: E402

HW = {"board": {"name": "nucleo_f446re", "debug_probe": {"openocd": "board/st_nucleo_f4.cfg"}},
      "soc": {"name": "stm32f446re", "part": "STM32F446RE"}, "derived": {"console": {"baud": 115200}}}


class Board:
    """A pseudo-terminal; after the flash command runs, the 'board' prints `output`."""

    def __init__(self, output, delay=0.05):
        self.master, self.slave = pty.openpty()
        self.port = os.ttyname(self.slave)
        self.dir = Path(tempfile.mkdtemp(prefix="hil-"))
        self.flag = self.dir / "flashed"
        self.image = self.dir / "test.elf"
        self.image.write_bytes(b"\x7fELF image")
        self.hw = self.dir / "hw.json"
        self.hw.write_text(json.dumps(HW))
        self.output = output
        self.delay = delay
        self.thread = threading.Thread(target=self.play, daemon=True)
        self.thread.start()

    def play(self):
        for _ in range(500):  # up to 5 s for the runner to flash
            if self.flag.exists():
                time.sleep(self.delay)
                os.write(self.master, self.output)
                return
            time.sleep(0.01)

    def flash_cmd(self, code=0):
        return f"{sys.executable} -c \"import sys; open({str(self.flag)!r}, 'w').close(); sys.exit({code})\""

    def run(self, *extra, code=0):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = run_hil.main([str(self.image), "--hw-json", str(self.hw), "--port", self.port,
                               "--flash-cmd", self.flash_cmd(code), "--timeout", "3", *extra])
        return rc, out.getvalue(), err.getvalue()

    def close(self):
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass


class Runner(unittest.TestCase):
    def board(self, output, **kw):
        b = Board(output, **kw)
        self.addCleanup(b.close)
        return b

    def test_passing_run(self):
        b = self.board(b"TEST a\nCASE a outcome=passed\n3 tests, 0 failed, 0 skipped, 12 ticks\nEMB_TEST_END\n")
        rc, out, _ = b.run()
        self.assertEqual(rc, 0)
        self.assertIn("hil: board nucleo_f446re, soc STM32F446RE", out)
        self.assertIn("sha256 ", out)

    def test_failing_run(self):
        rc, _, _ = self.board(b"3 tests, 1 failed, 0 skipped, 12 ticks\nEMB_TEST_END\n").run()
        self.assertEqual(rc, 1)

    def test_benchmark_marker(self):
        rc, out, _ = self.board(b"bench notify: total 100\nBENCH_END\n").run("--until", "BENCH_END")
        self.assertEqual(rc, 0)
        self.assertIn("bench notify", out)

    def test_benchmark_values_are_required_with_bench(self):
        good = (b"BENCH v1 name=notify_handover units=1234 ops=20 per_op_x100=6170 unit_hz=250000 checked=1\n"
                b"BENCH v1 name=sem units=5 ops=1 unit_hz=1 os=freertos\nBENCH_END\n")
        rc, _, _ = self.board(good).run("--until", "BENCH_END", "--bench")
        self.assertEqual(rc, 0)
        rc, _, err = self.board(b"bench: total 100\nBENCH_END\n").run("--until", "BENCH_END", "--bench")
        self.assertEqual(rc, 1)
        self.assertIn("no BENCH v1 result line", err)
        zero = b"BENCH v1 name=irq min=0 max=9 avg_x100=400 ops=100 unit_hz=1 checked=0\nBENCH_END\n"
        rc, _, err = self.board(zero).run("--until", "BENCH_END", "--bench")
        self.assertEqual(rc, 1)
        self.assertIn("zero measurement", err)
        bad = b"BENCH v1 name=irq min=-3 ops=1\nBENCH_END\n"
        rc, _, err = self.board(bad).run("--until", "BENCH_END", "--bench")
        self.assertEqual(rc, 1)
        self.assertIn("unsigned integer", err)

    def test_archive_keeps_the_record(self):
        out = (b"EVIDENCE v1 suite=test_x config=abc profile=base board=b checked=1\nTEST a\n"
               b"  fault checks that reset the board: 3\n  -> ok\n"
               b"CASE a outcome=passed asserts=3 skips=0\nTEST c\nCASE c outcome=skipped asserts=0 skips=1\n"
               b"ISR_STACK size=2048 used=312\n2 tests, 0 failed, 1 skipped, 7 ticks\nEMB_TEST_END\n")
        b = self.board(out)
        arch = b.dir / "archive"
        rc, _, _ = b.run("--archive", str(arch))
        self.assertEqual(rc, 0)
        rec = json.loads((arch / "test.json").read_text())
        self.assertEqual(rec["format"], "emb-hil-run v1")
        self.assertEqual(rec["outcome"], "passed")
        self.assertEqual(rec["summary"], {"tests": 2, "failed": 0, "skipped": 1})
        self.assertEqual([c["name"] for c in rec["cases"]], ["a", "c"])
        self.assertEqual(rec["fault_checks_by_reset"], 3)
        self.assertEqual(rec["isr_stack"], {"size": 2048, "used": 312})
        self.assertEqual(rec["evidence"]["suite"], "test_x")
        self.assertEqual(len(rec["image_sha256"]), 64)
        self.assertEqual(rec["flashed_sha256"], {})  # the stand-in command names no file
        log = (arch / "test.log").read_bytes()
        self.assertTrue(log.startswith(b"hil: board nucleo_f446re"))
        self.assertIn(b"EMB_TEST_END", log)

    def test_archive_hashes_what_the_command_flashes(self):
        b = self.board(b"1 tests, 0 failed, 0 skipped, 1 ticks\nEMB_TEST_END\n")
        hexfile = b.dir / "test.hex"
        hexfile.write_bytes(b":00000001FF\n")
        arch = b.dir / "archive"
        flag = str(b.flag)
        cmd = (f"{sys.executable} -c \"import sys; open({flag!r}, 'w').close()\" "
               f"-Uflash:w:{hexfile}:i")
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = run_hil.main([str(b.image), "--hw-json", str(b.hw), "--port", b.port, "--flash-cmd", cmd,
                               "--timeout", "3", "--archive", str(arch)])
        self.assertEqual(rc, 0)
        rec = json.loads((arch / "test.json").read_text())
        self.assertEqual(set(rec["flashed_sha256"]), {"test.hex"})

    def test_archive_records_a_timeout(self):
        b = self.board(b"TEST a\n")
        arch = b.dir / "archive"
        rc, _, _ = b.run("--archive", str(arch))
        self.assertEqual(rc, 2)
        self.assertEqual(json.loads((arch / "test.json").read_text())["outcome"], "error")

    def test_flash_failure(self):
        rc, _, err = self.board(b"").run(code=3)
        self.assertEqual(rc, 2)
        self.assertIn("the flash command exited 3", err)

    def test_timeout_without_summary(self):
        rc, _, err = self.board(b"TEST a\n").run()
        self.assertEqual(rc, 2)
        self.assertIn("timeout", err)

    def test_log(self):
        b = self.board(b"1 tests, 0 failed, 0 skipped, 1 ticks\nEMB_TEST_END\n")
        log = b.dir / "console.log"
        rc, _, _ = b.run("--log", str(log))
        self.assertEqual(rc, 0)
        self.assertIn(b"EMB_TEST_END", log.read_bytes())

    def test_default_flash_command_from_hw_json(self):
        cmd = run_hil.flash_command(HW, Path("/x/test.elf"), None)
        self.assertEqual(cmd, ["openocd", "-f", "board/st_nucleo_f4.cfg", "-c", "program /x/test.elf verify reset exit"])
        with self.assertRaises(ValueError):
            run_hil.flash_command({"board": {}}, Path("t.elf"), None)

    def test_shared_port_flashes_before_opening(self):
        """The Uno's case: the bootloader has the port while flashing; the console opens after."""
        b = self.board(b"2 tests, 0 failed, 0 skipped, 5 ticks\nEMB_TEST_END\n", delay=0.5)
        hw = dict(HW, board={"name": "arduino_uno", "debug_probe": {"avrdude": {"part": "atmega328p",
                                                                               "programmer": "arduino", "baud": 115200}}})
        b.hw.write_text(json.dumps(hw))
        rc, out, _ = b.run()
        self.assertEqual(rc, 0)
        self.assertTrue(b.flag.exists())

    def test_avrdude_command_from_the_committed_uno(self):
        hw = json.loads((HERE.parent.parent / "boards/arduino/arduino_uno/generated/hw.json").read_text())
        self.assertTrue(run_hil.shares_port(hw))
        cmd = run_hil.flash_command(hw, Path("/b/test_sem.elf"), None, "/dev/cu.usbmodem1101", "avrdude")
        self.assertEqual(cmd, ["avrdude", "-q", "-p", "atmega328p", "-c", "arduino", "-P", "/dev/cu.usbmodem1101",
                               "-b", "115200", "-D", "-U", "flash:w:/b/test_sem.hex:i"])
        cmd = run_hil.flash_command(hw, Path("/b/test_sem.elf"), None, "/dev/cu.usbmodem1101", "embflash")
        self.assertEqual(cmd, ["embflash", "/b/test_sem.hex", "--stk500", "serial:/dev/cu.usbmodem1101@115200",
                               "--verify"])

    def test_the_committed_board_has_the_hints(self):
        hw = json.loads((HERE.parent.parent / "boards/st/nucleo_f446re/generated/hw.json").read_text())
        self.assertEqual(run_hil.flash_command(hw, Path("t.elf"), None)[:3], ["openocd", "-f", "board/st_nucleo_f4.cfg"])
        self.assertEqual(hw["derived"]["console"]["baud"], 115200)


if __name__ == "__main__":
    unittest.main()
