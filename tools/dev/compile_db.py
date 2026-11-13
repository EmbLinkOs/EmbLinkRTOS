#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""One compilation database for the editor: every configured build's compile_commands.json,
merged into build/compile_commands.json, which .clangd names. Each file keeps the flags of the
first build that compiles it, Clang builds first, since clangd reads their flags as they are;
GCC builds then add the files only they compile (AVR, Xtensa), whose system headers clangd
asks the compiler for (--query-driver, .vscode/settings.json). Run it again after configuring
another preset: `python3 tools/dev/compile_db.py`."""
import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def compiler(entry):
    words = entry.get("arguments") or entry.get("command", "").split()
    return pathlib.Path(words[0]).name if words else ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", type=pathlib.Path, default=ROOT / "build", help="the builds' parent directory")
    a = ap.parse_args(argv)
    dbs = []
    for path in sorted(a.build.glob("*/compile_commands.json")):
        entries = json.loads(path.read_text(encoding="utf-8"))
        if entries:
            dbs.append((0 if "clang" in compiler(entries[0]) else 1, path.parent.name, entries))
    dbs.sort(key=lambda d: (d[0], d[1]))
    seen = set()
    merged = []
    for _, _, entries in dbs:
        for e in entries:
            f = str((pathlib.Path(e["directory"]) / e["file"]).resolve())
            if f not in seen:
                seen.add(f)
                merged.append(e)
    if not merged:
        print("compile_db: no build has a compile_commands.json yet: configure a preset first", file=sys.stderr)
        return 1
    out = a.build / "compile_commands.json"
    out.write_text(json.dumps(merged, indent=1) + "\n", encoding="utf-8")
    print(f"compile_db: {len(merged)} files from {len(dbs)} builds into {out.relative_to(ROOT) if out.is_relative_to(ROOT) else out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
