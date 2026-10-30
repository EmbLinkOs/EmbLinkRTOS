#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Statement and branch coverage of the kernel from a gcov-instrumented build (M2-10).

Runs gcov (or `llvm-cov gcov`) on every kernel translation unit of a build made with the
native-coverage preset after its tests ran, and reports per-file and total line and
branch coverage. Only kernel .c files count: header inline functions are reported per
translation unit by gcov and cannot be merged from its summaries. Files listed in the
exclusions file are reported with their reason and left out of the totals.
"""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BLOCK = re.compile(r"File '([^']+)'\n(?:Lines executed:([\d.]+)% of (\d+)\n)?"
                   r"(?:Branches executed:[\d.]+% of \d+\nTaken at least once:([\d.]+)% of (\d+)\n)?")


def gcov_summary(gcov, gcno):
    """{source: (lines, lines_hit, branches, branches_taken)} for one translation unit."""
    out = subprocess.run(gcov + ["-b", "-c", "-n", gcno.name], cwd=gcno.parent, capture_output=True,
                         text=True)
    result = {}
    for m in BLOCK.finditer(out.stdout):
        src, lpct, lines, bpct, branches = m.groups()
        lines, branches = int(lines or 0), int(branches or 0)
        hit = round(float(lpct or 0) * lines / 100)
        taken = round(float(bpct or 0) * branches / 100)
        result[str(Path(src).resolve())] = (lines, hit, branches, taken)
    return result


def collect(build, gcov):
    files = {}
    for gcno in sorted((build / "kernel").rglob("*.gcno")):
        for src, counts in gcov_summary(gcov, gcno).items():
            path = Path(src)
            if path.suffix != ".c" or not path.is_relative_to(ROOT / "kernel"):
                continue
            rel = str(path.relative_to(ROOT))
            files[rel] = dict(lines=counts[0], lines_hit=counts[1], branches=counts[2],
                              branches_taken=counts[3], executed=gcno.with_suffix(".gcda").exists())
    return files


def pct(n, d):
    return 100.0 * n / d if d else 100.0


def render(files, exclusions, title):
    rows = ["| file | lines | branches | note |", "|---|---:|---:|---|"]
    tl = th = tb = tt = 0
    for rel, f in sorted(files.items()):
        note = exclusions.get(rel, "")
        if not note:
            tl += f["lines"]
            th += f["lines_hit"]
            tb += f["branches"]
            tt += f["branches_taken"]
        elif not f["executed"]:
            note = note or "not executed"
        rows.append(f"| `{rel}` | {pct(f['lines_hit'], f['lines']):.1f}% of {f['lines']} | "
                    f"{pct(f['branches_taken'], f['branches']):.1f}% of {f['branches']} | "
                    f"{'excluded: ' + note if note else ''} |")
    total = dict(lines=tl, lines_hit=th, branches=tb, branches_taken=tt,
                 line_pct=round(pct(th, tl), 1), branch_pct=round(pct(tt, tb), 1))
    md = [f"# {title}", "", f"Kernel statement (line) coverage **{total['line_pct']}%** of {tl} lines; "
          f"branch coverage (taken at least once) **{total['branch_pct']}%** of {tb} branches, "
          "over the files not excluded.", ""] + rows + [""]
    return "\n".join(md), total


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", type=Path, default=ROOT / "build/native-coverage")
    ap.add_argument("--gcov", default="gcov", help='the gcov command, e.g. "xcrun llvm-cov gcov"')
    ap.add_argument("--exclusions", type=Path, default=ROOT / "tools/coverage/exclusions.json")
    ap.add_argument("--json", type=Path)
    ap.add_argument("--markdown", type=Path)
    ap.add_argument("--title", default="Kernel coverage")
    args = ap.parse_args(argv)
    exclusions = json.loads(args.exclusions.read_text())["files"] if args.exclusions.is_file() else {}
    files = collect(args.build.resolve(), shlex.split(args.gcov))
    if not files:
        print("coverage: no kernel coverage data; build and test the native-coverage preset first",
              file=sys.stderr)
        return 2
    md, total = render(files, exclusions, args.title)
    if args.json:
        args.json.write_text(json.dumps(dict(total=total, files=files, exclusions=exclusions), indent=2) + "\n")
    if args.markdown:
        args.markdown.write_text(md)
    print(f"coverage: lines {total['line_pct']}% of {total['lines']}, branches {total['branch_pct']}% "
          f"of {total['branches']} ({len(files)} kernel files, {len(exclusions)} excluded)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
