# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""tools/emb: the console's decoding, the build's commands and what a build directory says
(BLD-005, A127)."""
import io
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "transport"))
import emb  # noqa: E402
import frame  # noqa: E402


class ConsoleDecoding(unittest.TestCase):
    def test_a_plain_console_shows_at_once(self):
        c = emb.Console()
        self.assertEqual(c.feed(b"hello "), [("text", b"hello ")])
        self.assertEqual(c.feed(b"world\n"), [("text", b"world\n")])

    def test_frames_after_text(self):
        c = emb.Console()
        out = c.feed(b"ROM boot\n" + b"\x00" + frame.encode(3, b"hi\n") + b"\x00" + frame.encode(0, b"\x08\x00"))
        self.assertEqual(out, [("text", b"ROM boot\n"), ("frame", 3, b"hi\n"), ("frame", 0, b"\x08\x00")])
        self.assertEqual(c.frames, 2)

    def test_a_frame_split_across_reads(self):
        c = emb.Console()
        wire = b"\x00" + frame.encode(3, b"split")
        self.assertEqual(c.feed(wire[:4]), [])
        self.assertEqual(c.feed(wire[4:]), [("frame", 3, b"split")])

    def test_damage_is_counted_and_text_kept(self):
        c = emb.Console()
        bad = bytearray(frame.encode(0, b"\x01\x02\x03"))
        bad[1] ^= 0x55
        out = c.feed(b"\x00" + bytes(bad) + b"cut text\x00")
        self.assertEqual(out, [("text", b"cut text")])
        self.assertEqual(c.errors, 1)


class Monitor(unittest.TestCase):
    def test_channels(self):
        buf = io.StringIO()
        m = emb.Monitor(write=buf.write)
        m.show(("frame", 3, b"shell text\n"))
        m.show(("frame", 0, b"\x08\x00\x00\x00\x00\x00"))
        m.show(("text", b"raw\n"))
        m.show(("frame", 1, b"x" * 16))
        self.assertEqual(buf.getvalue(), "shell text\n(log records, 6 bytes: give --elf to decode them)\nraw\n")
        self.assertEqual(m.trace_packets, 1)


class BuildCommands(unittest.TestCase):
    def args(self, **kw):
        base = dict(board="nucleo_f446re", source=str(emb.ROOT / "examples" / "logging"), build=None,
                    clang=False, framed=False, hil=False, port=None, set=None, define=None, dry_run=True)
        base.update(kw)
        return type("A", (), base)()

    def test_an_application(self):
        (conf, build), out = emb.build_command(self.args(framed=True, set=["EMB_TRACE=y"]))
        self.assertEqual(out, Path("build/logging-nucleo_f446re"))
        self.assertIn("-DEMB_BOARD=nucleo_f446re", conf)
        self.assertIn("-DEMB_SET=EMB_TRACE=y;EMB_CONSOLE_FRAMED=y", conf)
        self.assertTrue(any(c.startswith("-DEmbLinkRTOS_DIR=") for c in conf))
        self.assertEqual(build, ["cmake", "--build", "build/logging-nucleo_f446re"])

    def test_clang_and_the_board_suite(self):
        (conf, _), _ = emb.build_command(self.args(source=str(emb.ROOT), clang=True, hil=True,
                                                   port="/dev/cu.usbmodem1", build="build/x"))
        self.assertIn("-DEMB_TEST_EMULATOR=hil", conf)
        self.assertIn("-DEMB_HIL_PORT=/dev/cu.usbmodem1", conf)
        self.assertTrue(any(c.endswith("arm-none-eabi-clang.cmake") for c in conf))
        self.assertFalse(any(c.startswith("-DEmbLinkRTOS_DIR=") for c in conf))

    def test_an_unknown_board(self):
        with self.assertRaises(emb.EmbError):
            emb.build_command(self.args(board="no_such_board", clang=True))


class BuildDirectory(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp(prefix="emb-"))
        self.addCleanup(shutil.rmtree, self.d)

    def test_cache_image_and_port(self):
        (self.d / "CMakeCache.txt").write_text(
            "# comment\n//help\nCMAKE_PROJECT_NAME:STATIC=logging\nEMB_HIL_PORT:STRING=/dev/cu.x\n"
            "EMB_BOARD:STRING=nucleo_f446re\n")
        (self.d / "logging.elf").write_bytes(b"\x7fELF")
        self.assertEqual(emb.cache(self.d)["EMB_BOARD"], "nucleo_f446re")
        self.assertEqual(emb.image(self.d), self.d / "logging.elf")
        self.assertEqual(emb.console_port(self.d), "/dev/cu.x")
        self.assertEqual(emb.console_port(self.d, "/dev/given"), "/dev/given")
        self.assertEqual(emb.hw_json(self.d)["board"]["console"]["baud"], 115200)

    def test_not_a_build(self):
        with self.assertRaises(emb.EmbError):
            emb.cache(self.d)


if __name__ == "__main__":
    unittest.main()
