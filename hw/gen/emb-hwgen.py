#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
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
from emb_hwgen import HwError, facts, generate  # noqa: E402


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


def _target(args):
    """([(output directory, files)], warnings, what) for --board or --system: a system writes
    generated/<board>/ for each of its boards (A125)."""
    if args.system:
        system = Path(args.system)
        per_board, warnings = generate.generate_system(system, ROOT)
        base = Path(args.out) if getattr(args, "out", None) else system.parent / "generated"
        return [(base / name, files) for name, files in sorted(per_board.items())], warnings, system
    board = Path(args.board)
    files, warnings = generate.generate(board, ROOT)
    out = Path(args.out) if getattr(args, "out", None) else board / "generated"
    return [(out, files)], warnings, board


def cmd_generate(args):
    outputs, warnings, _ = _target(args)
    for out, files in outputs:
        for name, text in files.items():  # config/, include/, src/, linker/, build/, docs/ (A126)
            (out / name).parent.mkdir(parents=True, exist_ok=True)
            (out / name).write_text(text, encoding="utf-8")
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    for out, files in outputs:
        print(f"emb-hwgen: wrote {len(files)} files to {os.path.relpath(out, ROOT)}")
    return 0


def cmd_check(args):
    bad = 0
    roots = [Path(r) for r in args.board_root] or [ROOT]
    for r in roots:
        if not (r / "boards").is_dir():
            print(f"emb-hwgen: {r} has no boards/ directory (a board root holds boards/<vendor>/<board>/)", file=sys.stderr)
            return 2
    boards = [b for r in roots for b in _boards(r)]
    systems = [s for r in roots for s in _systems(r)]
    jobs = [(b / "generated", generate.generate(b, ROOT)[0], f"--board {os.path.relpath(b, ROOT)}") for b in boards]
    for s in systems:
        per_board, _ = generate.generate_system(s, ROOT)
        how = f"--system {os.path.relpath(s, ROOT)}"
        jobs += [(s.parent / "generated" / name, files, how) for name, files in sorted(per_board.items())]
        base = s.parent / "generated"
        for extra in sorted(p for p in base.iterdir() if p.is_dir() and p.name not in per_board and p.name != ".base") if base.is_dir() else []:
            bad += 1
            print(f"{os.path.relpath(extra, ROOT)}: not a board of {os.path.relpath(s, ROOT)}; remove it")
    for gen, files, how in jobs:
        names = set(files) | ({p.relative_to(gen).as_posix() for p in gen.rglob("*")
                               if p.is_file() and ".base" not in p.relative_to(gen).parts} if gen.is_dir() else set())
        for name in sorted(names):
            path = gen / name
            have = path.read_text(encoding="utf-8") if path.is_file() else None
            want = files.get(name)
            if have == want:
                continue
            bad += 1
            rel = os.path.relpath(path, ROOT)
            if want is None:
                print(f"{rel}: not an output of emb-hwgen; remove it")
            elif have is None:
                print(f"{rel}: missing; run emb-hwgen.py generate {how}")
            else:
                sys.stdout.writelines(difflib.unified_diff(have.splitlines(True), want.splitlines(True),
                                                           f"{rel} (committed)", f"{rel} (generated)"))
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
