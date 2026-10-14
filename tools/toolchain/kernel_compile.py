#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""CMake launcher for the explicit EmbCC kernel targets; never fall back to GCC."""
import os
import sys


def main():
    # C_COMPILER_LAUNCHER appends the host compiler, then its compile arguments.
    if len(sys.argv) < 4:
        print("usage: kernel_compile.py EMBCC HOST_CC COMPILE_ARGUMENTS...", file=sys.stderr)
        return 2
    compiler, _host_compiler, *arguments = sys.argv[1:]
    try:
        os.execv(compiler, [compiler, "--target=x86_64-elf", *arguments])
    except OSError as error:
        print(f"EmbCC kernel compiler: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
