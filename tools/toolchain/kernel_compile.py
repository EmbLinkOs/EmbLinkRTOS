#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""CMake launcher for the explicit EmbCC kernel targets; never fall back to GCC."""
import os
import sys


# The host compiler's options EmbCC refuses rather than ignores: the reproducible-build prefix
# maps (cmake/emb_flags.cmake), which name directories in debug information. The EmbCC-built
# kernel is not part of the double build (BLD-004); its objects keep the absolute paths.
HOST_ONLY = ("-ffile-prefix-map=", "-fdebug-prefix-map=", "-fmacro-prefix-map=")


def embcc_arguments(arguments):
    """The host compiler's arguments as EmbCC takes them."""
    return [a for a in arguments if not a.startswith(HOST_ONLY)]


def main():
    # C_COMPILER_LAUNCHER appends the host compiler, then its compile arguments.
    if len(sys.argv) < 4:
        print("usage: kernel_compile.py EMBCC HOST_CC COMPILE_ARGUMENTS...", file=sys.stderr)
        return 2
    compiler, _host_compiler, *arguments = sys.argv[1:]
    try:
        os.execv(compiler, [compiler, "--target=x86_64-elf", *embcc_arguments(arguments)])
    except OSError as error:
        print(f"EmbCC kernel compiler: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
