#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Compile the EmbCC header's marker and independently check its generated storage.

GCC exercises the selected portability header in CI. Set EMB_STORAGE_TEST_CC to an
EmbCC executable for the actual compiler check; no firmware is linked or executed.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LayoutMarkerTest(unittest.TestCase):
    def test_embcc_marker_generates_correct_storage(self):
        compiler = os.environ.get("EMB_STORAGE_TEST_CC", "gcc")
        with tempfile.TemporaryDirectory(prefix="emb-layout-") as temporary:
            folder = Path(temporary)
            (folder / "emb").mkdir()
            (folder / "emb/config.h").write_text("#define CONFIG_EMB_TICK_32BIT 0\n")
            objects = folder / "objects.h"
            objects.write_text("""#include <stdalign.h>
struct thread_object { char state; _Alignas(16) unsigned long payload; };
struct sem_object { unsigned short count; unsigned char state; };
struct mutex_object { void *owner; unsigned flags; char name[19]; };
""")
            probe = folder / "probe.c"
            probe.write_text("""#include <emb/compiler.h>
#include "objects.h"
void probe(void);
void probe(void)
{
    EMB_LAYOUT_MARKER(thread, sizeof(struct thread_object), alignof(struct thread_object));
    EMB_LAYOUT_MARKER(sem, sizeof(struct sem_object), alignof(struct sem_object));
    EMB_LAYOUT_MARKER(mutex, sizeof(struct mutex_object), alignof(struct mutex_object));
}
""")
            includes = ["-I", str(ROOT / "include"), "-I", str(folder)]
            # Select the actual EmbCC header even when GCC is the assembler driver.
            flags = ["-std=c11", "-D__EMBCC__=1", *includes]
            def run(*command):
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run(compiler, *flags, "-c", str(probe), "-o", str(folder / "probe.o"))
            run(sys.executable, "-I", str(ROOT / "tools/storage/gen_storage.py"),
                "--out", str(folder / "emb/storage.h"), str(folder / "probe.o"))
            assertions = folder / "assertions.c"
            checks = ['#include <emb/storage.h>', '#include "objects.h"']
            for name in ("thread", "sem", "mutex"):
                prefix = "EMB_" + name.upper() + "_STORAGE_"
                checks += [
                    f'_Static_assert({prefix}SIZE == sizeof(struct {name}_object), "size");',
                    f'_Static_assert({prefix}ALIGN == alignof(struct {name}_object), "alignment");',
                    f'_Static_assert(sizeof(emb_{name}_storage_t) >= sizeof(struct {name}_object), "fits");',
                    f'_Static_assert(alignof(emb_{name}_storage_t) >= alignof(struct {name}_object), "aligned");',
                ]
            assertions.write_text("\n".join(checks) + "\n")
            run(compiler, *flags, "-c", str(assertions), "-o", str(folder / "assertions.o"))


if __name__ == "__main__":
    unittest.main()
