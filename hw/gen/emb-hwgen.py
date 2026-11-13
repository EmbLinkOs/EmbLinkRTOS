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
    emb-hwgen.py validate --board boards/st/nucleo_f446re --format json   # for tools and IDEs

Exit status 0 on success, 1 when check finds a difference, 2 on a description error, 3 when the
generator itself fails; each error is printed as 'file:line: node.path: message' (HW-002). With
--format json the output is one JSON document instead (hw/schemas/diagnostics.schema.json): the
status, the exit code, each finding with its file, line, node path and message apart, and the
files written; nothing else goes to standard output.
"""
import argparse
import difflib
import os
import sys
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from emb_hwgen import HwError, diagnostics, facts, generate, writer  # noqa: E402


def _boards(root):
    return sorted(p.parent.parent.parent for p in root.glob("boards/*/*/generated/include/hw_config.h"))


CONFLICT = "an unresolved merge conflict (<<<<<<< yours ... >>>>>>> generated now)"

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


def _system_config(system):
    """An application's configuration folder, Harmony's src/config/ (A130)."""
    return system.parent / "src" / "config"


def _system_placements(system, per_board, out=None):
    """[(path, text, baseline path)] for a system's outputs: src/config/<board>/ for each board, the
    baseline under src/config/.base/<board>/ (A126, A130)."""
    gen = Path(out) if out else _system_config(system)
    out_list = []
    for board, files in sorted(per_board.items()):
        for name, text in sorted(files.items()):
            if board == generate.APP:  # the application's own files, beside system.yaml
                out_list.append((system.parent / name, text, gen / ".base" / board / name))
            else:
                out_list.append((gen / board / name, text, gen / ".base" / board / name))
    return out_list


def _target(args):
    """([(path, text, baseline path)], warnings, what) for --board or --system."""
    if args.system:
        system = Path(args.system)
        per_board, warnings = generate.generate_system(system, ROOT)
        # A126's folders, which A130 replaced: the application's edits there are its author's to move
        for old, now in (("generated", "src/config/<board>/"), ("src/tasks", "src/<task>.c and .h")):
            if (system.parent / old).is_dir():
                warnings.append(f"{os.path.relpath(system.parent / old, ROOT)}/: A130 moved these files to {now}; "
                                "move your edits there, then remove it")
        return _system_placements(system, per_board, getattr(args, "out", None)), warnings, system
    board = Path(args.board)
    files, warnings = generate.generate(board, ROOT)
    return _board_placements(board, files, getattr(args, "out", None)), warnings, board


def cmd_generate(args):
    """Each file written over what is there, edits kept (A126): created, unchanged, rewritten,
    merged with its edits, or a conflict for its author to resolve. With --format json, the files
    written and, for each conflict, an HW-003 finding (status stale, exit 1)."""
    placements, warnings, _ = _target(args)
    outcomes = {}
    for path, text, base in placements:
        outcome = writer.write(path, text, base)
        outcomes.setdefault(outcome, []).append(path)
    if args.format == "json":
        written = sorted(os.path.relpath(path, ROOT) for kind in ("created", "rewritten", "merged", "conflict")
                         for path in outcomes.get(kind, []))
        conflicts = [diagnostics.stale(os.path.relpath(path, ROOT), CONFLICT) for path in outcomes.get("conflict", [])]
        code = 1 if conflicts else 0
        return _emit(args, "stale" if code else "ok", code, _warnings(warnings) + conflicts, written)
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
    roots = [Path(r) for r in args.board_root] or [ROOT]
    found = []
    for r in roots:
        if not (r / "boards").is_dir():
            message = f"{r} has no boards/ directory (a board root holds boards/<vendor>/<board>/)"
            if args.format == "json":
                return _emit(args, "errors", 2, [diagnostics.finding(message, "error", None)])
            print(f"emb-hwgen: {message}", file=sys.stderr)
            return 2
    boards = [b for r in roots for b in _boards(r)]
    systems = [s for r in roots for s in _systems(r)]
    jobs = []  # (generated directory, placements, how, strict)
    for b in boards:
        jobs.append((b / "generated", _board_placements(b, generate.generate(b, ROOT)[0]),
                     f"--board {os.path.relpath(b, ROOT)}", args.strict))
    for s in systems:
        per_board, _ = generate.generate_system(s, ROOT)
        jobs.append((_system_config(s), _system_placements(s, per_board), f"--system {os.path.relpath(s, ROOT)}", False))
    for gen, placements, how, strict in jobs:
        expected = {path for path, _, _ in placements}
        for path, text, base in placements:
            rel = os.path.relpath(path, ROOT)
            have = path.read_text(encoding="utf-8") if path.is_file() else None
            baseline = base.read_text(encoding="utf-8") if base.is_file() else None
            if have is None:
                found.append(diagnostics.stale(rel, f"missing; run emb-hwgen.py generate {how}"))
            elif baseline != text:
                diff = "".join(difflib.unified_diff((baseline or "").splitlines(True), text.splitlines(True),
                                                    f"{rel} (generated then)", f"{rel} (generated now)"))
                found.append(diagnostics.stale(
                    rel, f"its description changed since it was generated; run emb-hwgen.py generate {how}", diff))
            elif writer.has_conflict(have):
                found.append(diagnostics.stale(rel, CONFLICT))
            elif strict and have != text:
                diff = "".join(difflib.unified_diff(text.splitlines(True), have.splitlines(True),
                                                    f"{rel} (generated)", f"{rel} (committed)"))
                found.append(diagnostics.stale(rel, "edited; a board's facts belong in its description (check --strict)",
                                               diff))
        if gen.is_dir():
            for path in sorted(p for p in gen.rglob("*") if p.is_file() and ".base" not in p.relative_to(gen).parts):
                if path not in expected:
                    found.append(diagnostics.stale(os.path.relpath(path, ROOT), "not an output of emb-hwgen; remove it"))
    bad = len(found)
    # HW-001 for the tree, or for each board root: its boards hold no hand-written files, and its
    # own SoC families' C no address-like constant (A84)
    problems = [p for r in roots for p in facts.check(r)]
    code = 1 if bad or problems else 0
    if args.format == "json":
        found += [diagnostics.finding(p, "error", "HW-001", with_path=False) for p in problems]
        return _emit(args, "stale" if code else "ok", code, found, boards=len(boards), systems=len(systems))
    for f in found:  # the file and what to do, then the diff where there is one
        print(f["text"])
        if f.get("diff"):
            sys.stdout.write(f["diff"])
    for p in problems:
        print(p)
    print(f"emb-hwgen check: {len(boards)} board(s), {len(systems)} system(s), {bad} stale file(s), "
          f"{len(problems)} hardware fact(s) outside a description")
    return code


def cmd_validate(args):
    _, warnings, what = _target(args)
    if args.format == "json":
        return _emit(args, "ok", 0, _warnings(warnings))
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    print(f"emb-hwgen: {what} is valid")
    return 0


def _warnings(warnings):
    return [diagnostics.finding(w, "warning", "HW-002") for w in warnings]


def _emit(args, status, code, findings=(), files=(), boards=None, error=None, systems=None):
    doc = diagnostics.document(args.cmd, status, code, findings, files, boards, error)
    if systems is not None:
        doc["systems"] = systems
    sys.stdout.write(diagnostics.dumps(doc))
    return code


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    fmt = argparse.ArgumentParser(add_help=False)
    fmt.add_argument("--format", choices=("text", "json"), default="text",
                     help="text (default), or one JSON document for tools (hw/schemas/diagnostics.schema.json)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate", parents=[fmt], help="write a board's or an application's generated outputs")
    which = g.add_mutually_exclusive_group(required=True)
    which.add_argument("--board", help="the board directory, holding board.yaml")
    which.add_argument("--system", help="an application's system.yaml (SPEC-014 §5, A125)")
    g.add_argument("--out", help="output directory (default: generated/ beside the board.yaml or system.yaml)")
    c = sub.add_parser("check", parents=[fmt], help="regenerate every board and compare with the committed outputs (HW-003), "
                       "and look for hardware facts outside the descriptions (HW-001)")
    c.add_argument("--strict", action="store_true",
                   help="a board's files must be exactly what was generated: no edits (the tree's boards, in CI)")
    c.add_argument("--board-root", action="append", default=[],
                   help="check the boards under DIR/boards/ instead of the tree's (repeatable)")
    v = sub.add_parser("validate", parents=[fmt],
                       help="validate a board and its SoC, or a system, without writing anything")
    which = v.add_mutually_exclusive_group(required=True)
    which.add_argument("--board")
    which.add_argument("--system")
    args = ap.parse_args(argv)
    try:
        return {"generate": cmd_generate, "check": cmd_check, "validate": cmd_validate}[args.cmd](args)
    except HwError as e:
        if args.format == "json":
            return _emit(args, "errors", 2, [diagnostics.finding(line, "error", "HW-002") for line in e.errors])
        for line in e.errors:
            print(line, file=sys.stderr)
        print(f"emb-hwgen: {len(e.errors)} error(s)", file=sys.stderr)
        return 2
    except Exception as e:  # the generator's own fault: a tool still gets a document, the trace goes to stderr
        if args.format != "json":
            raise
        traceback.print_exc()
        return _emit(args, "internal-error", 3, error={"type": type(e).__name__, "message": str(e)})


if __name__ == "__main__":
    sys.exit(main())
