#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.

"""emb-hwgen: validates a board and its SoC description and writes the generated outputs
(SPEC-014, docs/engineering/hwgen.md).

    emb-hwgen.py generate --board boards/st/nucleo_f446re [--out DIR]
    emb-hwgen.py check              # every generated/ is up to date; no hardware facts in C (HW-001)
    emb-hwgen.py check --board-root ~/my-project   # the boards a project keeps outside the tree
    emb-hwgen.py validate --board boards/st/nucleo_f446re
    emb-hwgen.py generate --system examples/controller/system.yaml   # an application (A125)

Exit status 0 on success, 1 when check finds a difference, 2 on a description error; each
error is printed as 'file:line: node.path: message' (HW-002).
"""
import argparse
import difflib
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from emb_hwgen import HwError, facts, generate, writer  # noqa: E402


def _boards(root):
    return sorted(p.parent.parent.parent for p in root.glob("boards/*/*/generated/include/hw_config.h"))


_SKIP = {"build", "third_party", ".git", "node_modules"}


def _systems(root):
    """Every system description under the root (A125), but none in build trees or vendored code."""
    out = []
    for p in sorted(root.rglob("system.yaml")):
        parts = p.relative_to(root).parts
        if not any(x in _SKIP or x.startswith(".venv") for x in parts):
            out.append(p)
    return out


def _board_placements(board, files, out=None):
    """[(path, text, baseline path)] for a board's outputs: generated/ and generated/.base/ (A126)."""
    gen = Path(out) if out else board / "generated"
    return [(gen / name, text, gen / ".base" / name) for name, text in sorted(files.items())]


def _system_placements(system, per_board, out=None):
    """[(path, text, baseline path)] for a system's outputs: generated/<board>/ for each board, the
    baseline under generated/.base/<board>/ (A126)."""
    gen = Path(out) if out else system.parent / "generated"
    return [(gen / board / name, text, gen / ".base" / board / name)
            for board, files in sorted(per_board.items()) for name, text in sorted(files.items())]


def _target(args):
    """([(path, text, baseline path)], warnings, what) for --board or --system."""
    if args.system:
        system = Path(args.system)
        per_board, warnings = generate.generate_system(system, ROOT)
        return _system_placements(system, per_board, getattr(args, "out", None)), warnings, system
    board = Path(args.board)
    files, warnings = generate.generate(board, ROOT)
    return _board_placements(board, files, getattr(args, "out", None)), warnings, board


def cmd_generate(args):
    """Each file written over what is there, edits kept (A126): created, unchanged, rewritten,
    merged with its edits, or a conflict for its author to resolve."""
    placements, warnings, _ = _target(args)
    outcomes = {}
    for path, text, base in placements:
        outcome = writer.write(path, text, base)
        outcomes.setdefault(outcome, []).append(path)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    for outcome in ("merged", "conflict"):
        for path in outcomes.get(outcome, []):
            print(f"  {outcome:9} {os.path.relpath(path, ROOT)}")
    counts = ", ".join(f"{len(v)} {k}" for k, v in sorted(outcomes.items()))
    print(f"emb-hwgen: {len(placements)} files: {counts}")
    if outcomes.get("conflict"):
        print("emb-hwgen: resolve the conflicts (between <<<<<<< yours and >>>>>>> generated now), then "
              "check again", file=sys.stderr)
        return 1
    return 0


def cmd_check(args):
    """Every baseline is what the descriptions give now (HW-003), no conflict is left, and nothing
    in a generated directory is an output of no description. With --strict, a board's files are
    exactly their baseline: a board's facts belong in its description (A126)."""
    bad = 0
    roots = [Path(r) for r in args.board_root] or [ROOT]
    for r in roots:
        if not (r / "boards").is_dir():
            print(f"emb-hwgen: {r} has no boards/ directory (a board root holds boards/<vendor>/<board>/)", file=sys.stderr)
            return 2
    boards = [b for r in roots for b in _boards(r)]
    systems = [s for r in roots for s in _systems(r)]
    jobs = []  # (generated directory, placements, how, strict)
    for b in boards:
        jobs.append((b / "generated", _board_placements(b, generate.generate(b, ROOT)[0]),
                     f"--board {os.path.relpath(b, ROOT)}", args.strict))
    for s in systems:
        per_board, _ = generate.generate_system(s, ROOT)
        jobs.append((s.parent / "generated", _system_placements(s, per_board), f"--system {os.path.relpath(s, ROOT)}", False))
    for gen, placements, how, strict in jobs:
        expected = {path for path, _, _ in placements}
        for path, text, base in placements:
            rel = os.path.relpath(path, ROOT)
            have = path.read_text(encoding="utf-8") if path.is_file() else None
            baseline = base.read_text(encoding="utf-8") if base.is_file() else None
            if have is None:
                bad += 1
                print(f"{rel}: missing; run emb-hwgen.py generate {how}")
            elif baseline != text:
                bad += 1
                print(f"{rel}: its description changed since it was generated; run emb-hwgen.py generate {how}")
                sys.stdout.writelines(difflib.unified_diff((baseline or "").splitlines(True), text.splitlines(True),
                                                           f"{rel} (generated then)", f"{rel} (generated now)"))
            elif writer.has_conflict(have):
                bad += 1
                print(f"{rel}: an unresolved merge conflict (<<<<<<< yours ... >>>>>>> generated now)")
            elif strict and have != text:
                bad += 1
                print(f"{rel}: edited; a board's facts belong in its description (check --strict)")
                sys.stdout.writelines(difflib.unified_diff(text.splitlines(True), have.splitlines(True),
                                                           f"{rel} (generated)", f"{rel} (committed)"))
        if gen.is_dir():
            for path in sorted(p for p in gen.rglob("*") if p.is_file() and ".base" not in p.relative_to(gen).parts):
                if path not in expected:
                    bad += 1
                    print(f"{os.path.relpath(path, ROOT)}: not an output of emb-hwgen; remove it")
    # HW-001 for the tree, or for each board root: its boards hold no hand-written files, and its
    # own SoC families' C no address-like constant (A84)
    problems = [p for r in roots for p in facts.check(r)]
    for p in problems:
        print(p)
    print(f"emb-hwgen check: {len(boards)} board(s), {len(systems)} system(s), {bad} stale file(s), "
          f"{len(problems)} hardware fact(s) outside a description")
    return 1 if bad or problems else 0


def cmd_validate(args):
    _, warnings, what = _target(args)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    print(f"emb-hwgen: {what} is valid")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate", help="write a board's or an application's generated outputs")
    which = g.add_mutually_exclusive_group(required=True)
    which.add_argument("--board", help="the board directory, holding board.yaml")
    which.add_argument("--system", help="an application's system.yaml (SPEC-014 §5, A125)")
    g.add_argument("--out", help="output directory (default: generated/ beside the board.yaml or system.yaml)")
    c = sub.add_parser("check", help="regenerate every board and compare with the committed outputs (HW-003), "
                       "and look for hardware facts outside the descriptions (HW-001)")
    c.add_argument("--strict", action="store_true",
                   help="a board's files must be exactly what was generated: no edits (the tree's boards, in CI)")
    c.add_argument("--board-root", action="append", default=[],
                   help="check the boards under DIR/boards/ instead of the tree's (repeatable)")
    v = sub.add_parser("validate", help="validate a board and its SoC, or a system, without writing anything")
    which = v.add_mutually_exclusive_group(required=True)
    which.add_argument("--board")
    which.add_argument("--system")
    args = ap.parse_args(argv)
    try:
        return {"generate": cmd_generate, "check": cmd_check, "validate": cmd_validate}[args.cmd](args)
    except HwError as e:
        for line in e.errors:
            print(line, file=sys.stderr)
        print(f"emb-hwgen: {len(e.errors)} error(s)", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
