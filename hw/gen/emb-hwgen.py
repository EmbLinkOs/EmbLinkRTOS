#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""emb-hwgen: validates a board and its SoC description and writes the generated outputs
(SPEC-014, docs/engineering/hwgen.md).

    emb-hwgen.py generate --board boards/st/nucleo_f446re [--out DIR]
    emb-hwgen.py check              # every generated/ is up to date; no hardware facts in C (HW-001)
    emb-hwgen.py check --board-root ~/my-project   # the boards a project keeps outside the tree
    emb-hwgen.py validate --board boards/st/nucleo_f446re
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
from emb_hwgen import HwError, diagnostics, facts, generate  # noqa: E402


def _boards(root):
    return sorted(p.parent.parent for p in root.glob("boards/*/*/generated/hw_config.h"))


def cmd_generate(args):
    board = Path(args.board)
    out = Path(args.out) if args.out else board / "generated"
    files, warnings = generate.generate(board, ROOT)
    out.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (out / name).write_text(text, encoding="utf-8")
    if args.format == "json":
        written = [os.path.relpath(out / name, ROOT) for name in files]
        return _emit(args, "ok", 0, _warnings(warnings), written)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    print(f"emb-hwgen: wrote {len(files)} files to {os.path.relpath(out, ROOT)}")
    return 0


def cmd_check(args):
    bad = 0
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
                found.append(diagnostics.stale(rel, "not an output of emb-hwgen; remove it"))
            elif have is None:
                found.append(diagnostics.stale(rel, f"missing; run emb-hwgen.py generate --board {os.path.relpath(board, ROOT)}"))
            else:
                diff = "".join(difflib.unified_diff(have.splitlines(True), want.splitlines(True),
                                                    f"{rel} (committed)", f"{rel} (generated)"))
                found.append(diagnostics.stale(rel, "differs from what emb-hwgen generates now", diff))
    # HW-001 for the tree, or for each board root: its boards hold no hand-written files, and its
    # own SoC families' C no address-like constant (A84)
    problems = [p for r in roots for p in facts.check(r)]
    code = 1 if bad or problems else 0
    if args.format == "json":
        found += [diagnostics.finding(p, "error", "HW-001", with_path=False) for p in problems]
        return _emit(args, "stale" if code else "ok", code, found, boards=len(boards))
    for f in found:  # as before: the diff itself, or the file and what to do
        sys.stdout.write(f["diff"] if f["diff"] else f"{f['text']}\n")
    for p in problems:
        print(p)
    print(f"emb-hwgen check: {len(boards)} board(s), {bad} stale file(s), {len(problems)} hardware fact(s) outside a description")
    return code


def cmd_validate(args):
    _, warnings = generate.generate(Path(args.board), ROOT)
    if args.format == "json":
        return _emit(args, "ok", 0, _warnings(warnings))
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)
    print(f"emb-hwgen: {args.board} is valid")
    return 0


def _warnings(warnings):
    return [diagnostics.finding(w, "warning", "HW-002") for w in warnings]


def _emit(args, status, code, findings=(), files=(), boards=None, error=None):
    sys.stdout.write(diagnostics.dumps(diagnostics.document(args.cmd, status, code, findings, files, boards, error)))
    return code


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    fmt = argparse.ArgumentParser(add_help=False)
    fmt.add_argument("--format", choices=("text", "json"), default="text",
                     help="text (default), or one JSON document for tools (hw/schemas/diagnostics.schema.json)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate", parents=[fmt], help="write a board's generated outputs")
    g.add_argument("--board", required=True, help="the board directory, holding board.yaml")
    g.add_argument("--out", help="output directory (default: <board>/generated)")
    c = sub.add_parser("check", parents=[fmt], help="regenerate every board and compare with the committed outputs (HW-003), "
                       "and look for hardware facts outside the descriptions (HW-001)")
    c.add_argument("--board-root", action="append", default=[],
                   help="check the boards under DIR/boards/ instead of the tree's (repeatable)")
    v = sub.add_parser("validate", parents=[fmt], help="validate a board and its SoC without writing anything")
    v.add_argument("--board", required=True)
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
