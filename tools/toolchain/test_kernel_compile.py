# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The launcher preserves arguments/exit status and cannot fall back to the host."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

LAUNCHER = Path(__file__).with_name("kernel_compile.py")


class KernelCompiler(unittest.TestCase):
    def test_exact_arguments_and_compiler_failure_are_preserved(self):
        with tempfile.TemporaryDirectory(prefix="compiler with spaces ") as folder:
            compiler = Path(folder)/"compiler"
            compiler.write_text(f"#!{sys.executable}\nimport json, sys\n"
                                "print(json.dumps(sys.argv[1:]))\nsys.exit(7)\n")
            compiler.chmod(0o755)
            arguments = ["-c", "source with spaces.c", "-DVALUE=$(false);literal", "-o", "file.o"]
            result = subprocess.run([sys.executable, "-I", str(LAUNCHER), str(compiler),
                                     "/must/not/run/host-cc", *arguments],
                                    text=True, capture_output=True, check=False)
            self.assertEqual(result.returncode, 7)
            self.assertEqual(json.loads(result.stdout), ["--target=x86_64-elf", *arguments])

    def test_missing_kernel_compiler_fails_without_running_host(self):
        result = subprocess.run([sys.executable, "-I", str(LAUNCHER),
                                 "/missing/compiler/embcc", sys.executable, "-c", "pass"],
                                text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("EmbCC kernel compiler:", result.stderr)


if __name__ == "__main__":
    unittest.main()
