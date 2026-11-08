#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""clang-tidy over the product code of one or more builds (CODING-STANDARD §12, §13; 05 §5).

    python3 tools/analysis/clang_tidy.py [--clang-tidy clang-tidy-18] [-j N] BUILD_DIR...

Each BUILD_DIR is a configured and built tree with a compile_commands.json, from a preset that
compiles with Clang: clang-tidy reads the commands as Clang's, and GCC-only flags confuse it. Each
build contributes the C translation units under the product directories (SOURCES); tests,
samples and generated files are not analyzed, though the headers they include are, by
.clang-tidy's HeaderFilterRegex. The same code built in several configurations is analyzed in
each, since #if selects different code, and a finding reported by several is listed once.

The checks, their reasons and the naming rules are .clang-tidy's. A finding is fixed or recorded
at its site as a deviation (CS-12, CS-13.3); this tool has no allow list of its own.

Exit status 0 when there is no finding, 1 when there is one, 2 on a usage or tool error.
"""
import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# product code: what ships in an image (the coding standard's scope)
SOURCES = ("kernel/", "arch/", "boards/", "soc/", "drivers/", "subsys/", "compatibility/", "examples/")
DIAG = re.compile(r"^(?P<path>[^\s:][^:]*):(?P<line>\d+):(?P<col>\d+): (?P<sev>warning|error): "
                  r"(?P<msg>.*?)(?: \[(?P<check>[\w.,-]+)\])?$")


def finding(line, root=ROOT):
    """A diagnostic line as (path relative to root, line, column, check, message), or None."""
    m = DIAG.match(line.rstrip("\n"))
    if not m:
        return None
    path = Path(m["path"])
    try:
        rel = path.resolve().relative_to(root).as_posix() if path.is_absolute() else path.as_posix()
    except ValueError:
        rel = path.as_posix()  # outside the tree: a system header, reported as it is
    check = (m["check"] or "clang-diagnostic-error").split(",")[0]
    return rel, int(m["line"]), int(m["col"]), check, m["msg"]


def units(build, root=ROOT):
    """The product translation units of a build, relative to root, in the database's order."""
    db = Path(build) / "compile_commands.json"
    if not db.is_file():
        raise SystemExit(f"clang_tidy: {db}: not found (configure and build the tree first)")
    out = []
    for entry in json.loads(db.read_text(encoding="utf-8")):
        f = Path(entry["file"])
        if not f.is_absolute():
            f = Path(entry["directory"]) / f
        try:
            rel = f.resolve().relative_to(root).as_posix()
        except ValueError:
            continue
        if rel.startswith(SOURCES) and rel.endswith(".c") and rel not in out:
            out.append(rel)
    return out


def run_one(tidy, build, unit, root=ROOT):
    """clang-tidy on one unit of one build: (findings, the raw output when it failed to run)."""
    p = subprocess.run([tidy, "-p", str(build), "--quiet", str(root / unit)], cwd=root,
                       capture_output=True, text=True, check=False)
    found = [f for f in (finding(line, root) for line in p.stdout.splitlines()) if f]
    # a non-zero status with no parsed finding is a tool failure, not a clean unit
    broken = p.returncode != 0 and not found
    return found, (p.stdout + p.stderr) if broken else ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("builds", nargs="+", help="configured and built trees with compile_commands.json")
    ap.add_argument("--clang-tidy", default=os.environ.get("CLANG_TIDY", "clang-tidy-18"))
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    args = ap.parse_args(argv)
    tidy = shutil.which(args.clang_tidy) or (args.clang_tidy if Path(args.clang_tidy).is_file() else None)
    if not tidy:
        print(f"clang_tidy: {args.clang_tidy}: not found", file=sys.stderr)
        return 2
    version = subprocess.run([tidy, "--version"], capture_output=True, text=True, check=False).stdout
    print("clang_tidy: " + " ".join(version.split()[:6]))

    jobs = []
    for build in args.builds:
        us = units(build)
        print(f"clang_tidy: {build}: {len(us)} units")
        jobs += [(build, u) for u in us]
    seen = {}
    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {pool.submit(run_one, tidy, b, u): (b, u) for b, u in jobs}
        for fut in concurrent.futures.as_completed(futures):
            b, u = futures[fut]
            found, broken = fut.result()
            if broken:
                failed.append((b, u, broken))
            for f in found:
                seen.setdefault(f, set()).add(Path(b).name)

    for b, u, out in failed:
        print(f"clang_tidy: {u} in {b}: clang-tidy failed without a diagnostic:\n{out}", file=sys.stderr)
    for (path, line, col, check, msg), where in sorted(seen.items()):
        print(f"{path}:{line}:{col}: {msg} [{check}] (in {', '.join(sorted(where))})")
    checks = {}
    for (_, _, _, check, _) in seen:
        checks[check] = checks.get(check, 0) + 1
    summary = ", ".join(f"{c} {n}" for c, n in sorted(checks.items())) or "none"
    print(f"clang_tidy: {len(jobs)} analyses, {len(seen)} findings: {summary}")
    if failed:
        return 2
    return 1 if seen else 0


if __name__ == "__main__":
    sys.exit(main())
