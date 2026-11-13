#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""Changelog entries as one file per change, folded into CHANGELOG.md at release.

    python3 tools/release/changes.py check      # CI: every entry in changes/ is well formed
    python3 tools/release/changes.py collect    # at release: fold them into CHANGELOG.md's Unreleased

A pull request with a user-visible change adds its own file, `changes/<name>.md`, instead of
editing CHANGELOG.md: two pull requests then never edit the same lines, and neither has to be
updated when the other merges. A file holds one or more entries in the changelog's form:

    ### Fixed (the memory report for Clang, 2026-11-08)

    - `tools/footprint/memory_report.py` reads LLD's map as well as GNU ld's.

`collect` puts the entries under `## Unreleased`, newest first by the date in their heading, then
by file name, and deletes the files. The release checklist (docs/engineering/releases.md) then
moves Unreleased under the version.

Exit status 0, 1 when an entry is malformed, 2 on a usage error.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHANGES = ROOT / "changes"
CHANGELOG = ROOT / "CHANGELOG.md"
KINDS = ("Added", "Changed", "Fixed", "Removed", "Deprecated", "Security", "Known limits")
HEADING = re.compile(r"^### (" + "|".join(KINDS) + r") \((?P<topic>.+), (?P<date>\d{4}-\d{2}-\d{2})\)$")
NAME = re.compile(r"^[a-z0-9][a-z0-9._-]*\.md$")
UNRELEASED = "## Unreleased\n\n"


def entries(folder=CHANGES):
    """The entry files, without the folder's README."""
    if not folder.is_dir():
        return []
    return sorted(p for p in folder.iterdir() if p.suffix == ".md" and p.name != "README.md")


def problems(path):
    """What is wrong with one entry file, as messages; empty when it is well formed."""
    out = []
    if not NAME.match(path.name):
        out.append(f"{path.name}: name it in lower case, letters, digits, '.', '_' and '-' (the branch's name works)")
    text = path.read_text(encoding="utf-8")
    lines = [line for line in text.splitlines() if line.strip()]
    if not lines:
        return out + [f"{path.name}: empty"]
    if not HEADING.match(lines[0]):
        out.append(f"{path.name}: the first line must be '### <{'|'.join(KINDS)}> (<topic>, YYYY-MM-DD)', "
                   f"not {lines[0]!r}")
    for line in lines:
        if line.startswith("#") and not HEADING.match(line):
            out.append(f"{path.name}: {line!r}: only '### <kind> (<topic>, <date>)' headings")
        if line.startswith("## "):
            out.append(f"{path.name}: {line!r}: an entry has no release heading; collect places it")
    if not any(line.lstrip().startswith("- ") for line in lines):
        out.append(f"{path.name}: no '- ' item under the heading")
    return out


def date_of(path):
    m = HEADING.match(next((line for line in path.read_text(encoding="utf-8").splitlines() if line.strip()), ""))
    return m["date"] if m else "0000-00-00"


def collect(folder=CHANGES, changelog=CHANGELOG):
    """Fold the entries into the changelog's Unreleased section and delete them; their names."""
    files = entries(folder)
    bad = [p for f in files for p in problems(f)]
    if bad:
        raise ValueError("\n".join(bad))
    text = changelog.read_text(encoding="utf-8")
    if UNRELEASED not in text:
        raise ValueError(f"{changelog.name}: no '## Unreleased' section")
    ordered = sorted(files, key=lambda p: p.name)
    ordered.sort(key=date_of, reverse=True)  # newest first; one day's entries by name
    block = "".join(p.read_text(encoding="utf-8").strip() + "\n\n" for p in ordered)
    changelog.write_text(text.replace(UNRELEASED, UNRELEASED + block, 1), encoding="utf-8")
    for p in files:
        p.unlink()
    return [p.name for p in ordered]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("command", choices=["check", "collect"])
    args = ap.parse_args(argv)
    if args.command == "check":
        files = entries()
        bad = [p for f in files for p in problems(f)]
        for message in bad:
            print(f"changes: {message}", file=sys.stderr)
        print(f"changes: {len(files)} entries, {len(bad)} problem(s)")
        return 1 if bad else 0
    try:
        names = collect()
    except ValueError as e:
        print(f"changes: {e}", file=sys.stderr)
        return 1
    print(f"changes: {len(names)} entries folded into {CHANGELOG.name}: {', '.join(names) or 'none'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
