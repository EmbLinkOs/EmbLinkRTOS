#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""emb-hwgen: validates a board and its SoC description and writes the generated outputs
(SPEC-014, docs/engineering/hwgen.md).

    emb-hwgen.py generate --board boards/st/nucleo_f446re [--out DIR]
    emb-hwgen.py check              # every generated/ is up to date; no hardware facts in C (HW-001)
    emb-hwgen.py check --board-root ~/my-project   # the boards a project keeps outside the tree
    emb-hwgen.py validate --board boards/st/nucleo_f446re

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
    return sorted(p.parent.parent for p in root.glob("boards/*/*/generated/hw_config.h"))


def cmd_generate(args):
    board = Path(args.board)
    out = Path(args.out) if args.out else board / "generated"
    files, warnings = generate.generate(board, ROOT)
    out.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (out / name).write_text(text, encoding="utf-8")
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
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
    for board in boards:
        files, _ = generate.generate(board, ROOT)
        gen = board / "generated"
        names = set(files) | {p.name for p in gen.iterdir() if p.is_file()}
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
                print(f"{rel}: missing; run emb-hwgen.py generate --board {os.path.relpath(board, ROOT)}")
            else:
                sys.stdout.writelines(difflib.unified_diff(have.splitlines(True), want.splitlines(True),
                                                           f"{rel} (committed)", f"{rel} (generated)"))
    # the facts check reads the kernel's C, so it runs on the tree's own check only
    problems = [] if args.board_root else facts.check(ROOT)
    for p in problems:
        print(p)
    print(f"emb-hwgen check: {len(boards)} board(s), {bad} stale file(s), {len(problems)} hardware fact(s) outside a description")
    return 1 if bad or problems else 0


def cmd_validate(args):
    _, warnings = generate.generate(Path(args.board), ROOT)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    print(f"emb-hwgen: {args.board} is valid")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate", help="write a board's generated outputs")
    g.add_argument("--board", required=True, help="the board directory, holding board.yaml")
    g.add_argument("--out", help="output directory (default: <board>/generated)")
    c = sub.add_parser("check", help="regenerate every board and compare with the committed outputs (HW-003), "
                       "and look for hardware facts outside the descriptions (HW-001)")
    c.add_argument("--board-root", action="append", default=[],
                   help="check the boards under DIR/boards/ instead of the tree's (repeatable)")
    v = sub.add_parser("validate", help="validate a board and its SoC without writing anything")
    v.add_argument("--board", required=True)
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
