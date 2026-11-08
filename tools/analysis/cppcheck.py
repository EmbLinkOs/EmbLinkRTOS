#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""cppcheck over the product code of one or more builds (CODING-STANDARD §12; 05 §5).

    python3 tools/analysis/cppcheck.py [--cppcheck cppcheck] [-j N] BUILD_DIR...

Each BUILD_DIR is a configured and built tree with a compile_commands.json. cppcheck reads each
product translation unit (the same SOURCES as clang_tidy.py) with the unit's own -D and -I flags.

cppcheck does not emulate a compiler: by itself it defines none of __GNUC__, __clang__,
__x86_64__ or __ARM_ARCH, so the compiler layer would take its generic header and a port its
#error. The tool therefore asks each build's compiler, with that build's target flags, for its
predefined macros (`cc -dM -E`), and gives them to cppcheck as a forced include. cppcheck then
analyzes the code each build really compiles, in the configuration that build selects.

The enabled check groups are CHECKS. A check turned off for the whole tree is listed, with its
reason, in the root .cppcheck-suppressions, as .clang-tidy does for clang-tidy (CS-13.3). A finding
at one site is fixed or recorded there as a deviation: a MISRA Dev CS-12 comment with
`/* cppcheck-suppress <id> */` (CS-12). The tool refuses a suppressions entry that names a file.

Exit status 0 when there is no finding, 1 when there is one, 2 on a usage or tool error.
"""
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from clang_tidy import ROOT, SOURCES  # noqa: E402  (one definition of the product code)

CHECKS = "warning,style,performance,portability"
SUPPRESSIONS = ROOT / ".cppcheck-suppressions"
# Macros a compiler predefines that cppcheck's preprocessor cannot take: function-like
# feature tests it implements itself.
SKIP_PREDEF = re.compile(r"^#define (__has_include|__has_include_next)\b")
# Flags that select the target, the language or the predefined macros; everything else in a
# compile command (outputs, dependency files, warnings, -I and -D) is left out of the probe.
PROBE_FLAG = re.compile(r"^(-m\S+|-std=\S+|--target=\S+|-target|-f(no-)?(short-enums|short-wchar|"
                        r"signed-char|unsigned-char|pic|PIC|pie|PIE|exceptions)|-O\S*|"
                        r"-march=\S+|-mcpu=\S+|-mabi=\S+|--sysroot=\S+|-isysroot)$")
TAKES_VALUE = {"-target", "-isysroot"}
DIAG = re.compile(r"^(?P<path>[^:]+):(?P<line>\d+):(?P<col>\d+): (?P<sev>\w+): (?P<msg>.*) \[(?P<id>\w+)\]$")
TEMPLATE = "{file}:{line}:{column}: {severity}: {message} [{id}]"


def tree_wide(path=SUPPRESSIONS):
    """The check ids .cppcheck-suppressions turns off. Each must follow a comment giving its reason
    and name no file: a site's finding is recorded at the site."""
    ids = []
    reason = False
    for n, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line:
            reason = False
            continue
        if line.startswith("#"):
            reason = True
            continue
        if not re.fullmatch(r"[A-Za-z]\w*", line):
            raise SystemExit(f"cppcheck: {path.name}:{n}: '{line}': only a check id, with no file; "
                             "record a site's finding at the site")
        if not reason:
            raise SystemExit(f"cppcheck: {path.name}:{n}: '{line}' has no comment giving its reason")
        ids.append(line)
    return ids


def command(entry):
    """A compile_commands entry's arguments."""
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def compiler_argv(args):
    """The real compiler and its target-selecting flags, from a compile command.

    A launcher (ccache, or tools/toolchain/kernel_compile.py in front of EmbCC) comes first; the
    compiler is the first argument that is not a launcher or an interpreter."""
    i = 0
    while i < len(args):
        name = Path(args[i]).name
        if name in ("ccache", "sccache") or args[i].endswith(".py"):
            i += 1
        elif name.startswith("python"):
            i += 1
            while i < len(args) and args[i].startswith("-"):  # the interpreter's own flags
                i += 1
        else:
            break
    if i >= len(args):
        return None
    out = [args[i]]
    rest = args[i + 1:]
    j = 0
    while j < len(rest):
        a = rest[j]
        if a in TAKES_VALUE and j + 1 < len(rest):
            out += [a, rest[j + 1]]
            j += 2
            continue
        if PROBE_FLAG.match(a):
            out.append(a)
        j += 1
    return out


def predefines(cc, cwd):
    """The compiler's predefined macros with these flags, as header text cppcheck can read."""
    p = subprocess.run(cc + ["-dM", "-E", "-x", "c", os.devnull], cwd=cwd, capture_output=True,
                       text=True, check=False)
    if p.returncode != 0:
        raise SystemExit(f"cppcheck: {' '.join(cc)} -dM -E failed:\n{p.stderr}")
    lines = sorted(line for line in p.stdout.splitlines() if line and not SKIP_PREDEF.match(line))
    return "\n".join(lines) + "\n"


def units(build, root=ROOT):
    """(unit relative to root, compile_commands entry) for each product C unit of a build."""
    db = Path(build) / "compile_commands.json"
    if not db.is_file():
        raise SystemExit(f"cppcheck: {db}: not found (configure and build the tree first)")
    out = {}
    for entry in json.loads(db.read_text(encoding="utf-8")):
        f = Path(entry["file"])
        if not f.is_absolute():
            f = Path(entry["directory"]) / f
        try:
            rel = f.resolve().relative_to(root).as_posix()
        except ValueError:
            continue
        if rel.startswith(SOURCES) and rel.endswith(".c") and rel not in out:
            out[rel] = entry
    return out


def finding(line, root=ROOT):
    """A diagnostic line as (path relative to root, line, column, id, message), or None."""
    m = DIAG.match(line.rstrip("\n"))
    if not m:
        return None
    path = Path(m["path"])
    try:
        rel = path.resolve().relative_to(root).as_posix() if path.is_absolute() else path.as_posix()
    except ValueError:
        rel = path.as_posix()
    return rel, int(m["line"]), int(m["col"]), m["id"], m["msg"]


def analyze(cppcheck, build, jobs, work, off=(), root=ROOT):
    """cppcheck over one build: (findings, the raw output when it failed to run)."""
    us = units(build, root)
    if not us:
        return [], ""
    groups = {}  # the same compiler and target flags share one predefines header
    for rel, entry in us.items():
        cc = compiler_argv(command(entry))
        if cc is None:
            return [], f"{rel}: no compiler in its compile command"
        groups.setdefault(tuple(cc), []).append((rel, entry))
    found = []
    for n, (cc, members) in enumerate(sorted(groups.items())):
        cwd = members[0][1]["directory"]
        pre = Path(work) / f"predefs-{n}.h"
        pre.write_text(predefines(list(cc), cwd), encoding="utf-8")
        db = Path(work) / f"compile_commands-{n}.json"
        db.write_text(json.dumps([e for _, e in members]), encoding="utf-8")
        argv = [cppcheck, f"--project={db}", f"--include={pre}", f"--enable={CHECKS}",
                "--inline-suppr", "--error-exitcode=0", "--quiet", f"-j{jobs}",
                f"--cppcheck-build-dir={Path(work) / f'cache-{n}'}", "--library=gnu",
                f"--suppress=*:{pre}", "--suppress=missingIncludeSystem",
                "--suppress=unmatchedSuppression", "--suppress=checkersReport",
                f"--template={TEMPLATE}"] + [f"--suppress={i}" for i in off]
        (Path(work) / f"cache-{n}").mkdir(exist_ok=True)
        p = subprocess.run(argv, cwd=root, capture_output=True, text=True, check=False)
        lines = (p.stdout + p.stderr).splitlines()
        parsed = [f for f in (finding(line, root) for line in lines) if f]
        if p.returncode != 0 and not parsed:
            return found, p.stdout + p.stderr
        found += parsed
    return found, ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("builds", nargs="+", help="configured and built trees with compile_commands.json")
    ap.add_argument("--cppcheck", default=os.environ.get("CPPCHECK", "cppcheck"))
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    args = ap.parse_args(argv)
    tool = shutil.which(args.cppcheck) or (args.cppcheck if Path(args.cppcheck).is_file() else None)
    if not tool:
        print(f"cppcheck: {args.cppcheck}: not found", file=sys.stderr)
        return 2
    version = subprocess.run([tool, "--version"], capture_output=True, text=True, check=False).stdout.strip()
    print(f"cppcheck: {version}")

    off = tree_wide()
    print(f"cppcheck: off tree-wide ({SUPPRESSIONS.name}): {', '.join(off) or 'none'}")
    seen = {}
    failed = []
    with tempfile.TemporaryDirectory(prefix="emb-cppcheck-") as tmp:
        for i, build in enumerate(args.builds):
            work = Path(tmp) / str(i)
            work.mkdir()
            print(f"cppcheck: {build}: {len(units(build))} units")
            found, broken = analyze(tool, build, max(1, args.jobs), work, off)
            if broken:
                failed.append((build, broken))
            for f in found:
                seen.setdefault(f, set()).add(Path(build).name)

    for b, out in failed:
        print(f"cppcheck: {b}: cppcheck failed without a diagnostic:\n{out}", file=sys.stderr)
    for (path, line, col, ident, msg), where in sorted(seen.items()):
        print(f"{path}:{line}:{col}: {msg} [{ident}] (in {', '.join(sorted(where))})")
    ids = {}
    for (_, _, _, ident, _) in seen:
        ids[ident] = ids.get(ident, 0) + 1
    summary = ", ".join(f"{c} {n}" for c, n in sorted(ids.items())) or "none"
    print(f"cppcheck: {len(args.builds)} builds, {len(seen)} findings: {summary}")
    if failed:
        return 2
    return 1 if seen else 0


if __name__ == "__main__":
    sys.exit(main())
