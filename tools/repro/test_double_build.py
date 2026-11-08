# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The double build's choice of images, its targets and its ELF section comparison, without a build."""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import double_build as db  # noqa: E402


def elf32(sections):
    """A minimal little-endian ELF32 file with the named sections ({name: (type, bytes)})."""
    names = b"\0" + b"".join(n.encode() + b"\0" for n in sections) + b".shstrtab\0"
    body = b""
    heads = [(0, 0, 0, 0)]
    pos = 52
    for i, (n, (typ, data)) in enumerate(sections.items()):
        heads.append((names.index(n.encode() + b"\0"), typ, pos, len(data)))
        body += data
        pos += len(data)
    heads.append((names.index(b".shstrtab\0"), 3, pos, len(names)))
    body += names
    pos += len(names)
    hdr = bytearray(52)
    hdr[0:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<I", hdr, 0x20, pos)  # e_shoff
    struct.pack_into("<HHH", hdr, 0x2E, 40, len(heads), len(heads) - 1)
    table = b"".join(struct.pack("<IIIIII", n, t, 0, 0, o, s) + bytes(16) for n, t, o, s in heads)
    return bytes(hdr) + body + table


class Images(unittest.TestCase):
    def test_images_by_suffix_and_native_programs(self):
        with tempfile.TemporaryDirectory() as d:
            b = Path(d)
            (b / "app.elf").write_bytes(b"\x7fELF")
            (b / "app.hex").write_text(":00000001FF\n")
            (b / "sub").mkdir()
            (b / "sub/app.uf2").write_bytes(b"UF2")
            prog = b / "sub/test_smoke"
            prog.write_bytes(b"\x7fELF....")
            os.chmod(prog, 0o755)
            script = b / "run.sh"
            script.write_text("#!/bin/sh\n")
            os.chmod(script, 0o755)  # executable, but not a program the build linked
            (b / "app.elf.map").write_text("map")
            (b / "CMakeFiles/4.4.3").mkdir(parents=True)
            (b / "CMakeFiles/4.4.3/CMakeDetermineCompilerABI_C.bin").write_bytes(b"x")
            self.assertEqual([p.as_posix() for p in db.images(b)],
                             ["app.elf", "app.hex", "sub/app.uf2", "sub/test_smoke"])


class Sections(unittest.TestCase):
    def test_the_differing_section_is_named(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a.elf", Path(d) / "b.elf"
            a.write_bytes(elf32({".text": (1, b"\x01\x02"), ".debug_str": (1, b"/one/path\0")}))
            b.write_bytes(elf32({".text": (1, b"\x01\x02"), ".debug_str": (1, b"/two/path\0")}))
            self.assertEqual(db.differing_sections(a, b), [".debug_str"])

    def test_nobits_sections_compare_by_header_only(self):
        with tempfile.TemporaryDirectory() as d:
            a = Path(d) / "a.elf"
            a.write_bytes(elf32({".bss": (8, b""), ".text": (1, b"\x00")}))
            self.assertIn(".bss", db.elf_sections(a))

    def test_not_elf(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.hex"
            p.write_text(":00000001FF\n")
            self.assertEqual(db.differing_sections(p, p), [])


class Targets(unittest.TestCase):
    def test_presets_and_apps(self):
        args = db.argparse.Namespace(preset=["cm4-an386"], app=["examples/controller:nucleo_f446re",
                                                               "examples/controller:nucleo_f446re:cmake/toolchains/arm-none-eabi-clang.cmake"])
        t = db.targets(args)
        self.assertEqual([n for n, _, _ in t], ["cm4-an386", "controller-nucleo_f446re",
                                                "controller-nucleo_f446re-arm-none-eabi-clang"])
        self.assertEqual(t[0][2], Path("build/cm4-an386"))

    def test_an_app_with_cmake_options(self):
        t = db.targets(db.argparse.Namespace(preset=None, app=["examples/controller:nucleo_f446re+EMB_LTO=ON"]))
        name, how, bdir = t[0]
        self.assertEqual(name, "controller-nucleo_f446re-emb_lto-on")
        self.assertEqual(how, ("app", "examples/controller", "nucleo_f446re", None, ("EMB_LTO=ON",)))
        self.assertEqual(bdir, Path("build/repro/controller-nucleo_f446re-emb_lto-on"))

    def test_a_bad_app_spec(self):
        with self.assertRaises(SystemExit):
            db.targets(db.argparse.Namespace(preset=None, app=["examples/controller"]))
        with self.assertRaises(SystemExit):  # an option without a value
            db.targets(db.argparse.Namespace(preset=None, app=["examples/controller:nucleo_f446re+EMB_LTO"]))


if __name__ == "__main__":
    unittest.main()
