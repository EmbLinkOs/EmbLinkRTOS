#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Tree-wide check for constructs the coding standard bans (CODING-STANDARD CS-1.3, CS-1.5,
CS-1.6, CS-11.4): compiler thread-local storage, alloca, setjmp, computed goto, the
cleanup attribute, inline assembly outside the ports and compiler layer, compiler
detection macros outside include/emb/compiler/, TODO and FIXME markers."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ["include", "kernel", "arch", "boards", "samples", "tests", "soc", "drivers", "subsys"]
RULES = [
    (re.compile(r"\b_Thread_local\b|\b__thread\b"), "compiler thread-local storage (CS-1.3)"),
    (re.compile(r"\balloca\s*\("), "alloca (CS-1.3)"),
    (re.compile(r"\b(setjmp|longjmp)\s*\("), "setjmp/longjmp (CS-1.3)"),
    (re.compile(r"\bgoto\s*\*"), "computed goto (CS-1.3)"),
    (re.compile(r"__attribute__\s*\(\(\s*cleanup"), "cleanup attribute (CS-1.3)"),
    (re.compile(r"\b(TODO|FIXME)\b"), "TODO/FIXME marker (CS-11.4)"),
]
ASM = re.compile(r"\b__asm__\b|\basm\b\s*(volatile)?\s*\(")
COMPILER_MACRO = re.compile(r"defined\s*\(\s*__(GNUC|clang|EMBCC)__\s*\)|#\s*if(n?def)?\s+__(GNUC|clang|EMBCC)__")


def main():
    bad = 0
    for d in DIRS:
        base = os.path.join(ROOT, d)
        for dirpath, _, files in os.walk(base):
            for fn in files:
                if not fn.endswith((".c", ".h", ".S")):
                    continue
                path = os.path.join(dirpath, fn)
                rel = os.path.relpath(path, ROOT)
                with open(path, encoding="utf-8", errors="replace") as f:
                    for n, line in enumerate(f, 1):
                        for rx, what in RULES:
                            if rx.search(line):
                                print(f"{rel}:{n}: {what}")
                                bad += 1
                        if ASM.search(line) and not rel.startswith(("arch/", "include/emb/compiler/")):
                            print(f"{rel}:{n}: inline assembly outside arch/ (CS-1.5)")
                            bad += 1
                        if COMPILER_MACRO.search(line) and not rel.startswith("include/emb/compiler"):
                            print(f"{rel}:{n}: compiler detection outside the portability layer (CS-1.6)")
                            bad += 1
    print(f"check-banned: {bad} finding(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
