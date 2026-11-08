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

With --misra the tool also runs cppcheck's MISRA addon (MISRA C:2012 with its amendments) and
reports its findings by rule number: the rule texts are MISRA's and are not in the tree. MISRA
findings are gated against a baseline (--misra-baseline): a file that gains a finding of a rule
fails, a file that loses one is reported, so the baseline only shrinks (--write-misra-baseline
records it). The baseline counts findings per file and rule, not per line, so edits elsewhere in
a file do not move it. Findings of any other check are not baselined: each one fails.

Exit status 0 when there is no finding (or none beyond the baseline), 1 when there is one, 2 on a
usage or tool error.
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
DIAG = re.compile(r"^(?P<path>[^:]+):(?P<line>\d+):(?P<col>\d+): (?P<sev>\w+): (?P<msg>.*) \[(?P<id>[\w.-]+)\]$")
GENERATED = re.compile(r"^build/[^/]+/generated/")
MISRA = re.compile(r"^misra-c2012-(?P<rule>\d+\.\d+)$")
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
        if not re.fullmatch(r"[A-Za-z][\w.-]*", line):
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
    # a generated header is named by the build it is in; the same header in every build is one
    rel = GENERATED.sub("generated/", rel)
    msg = m["msg"]
    rule = MISRA.match(m["id"])
    if rule:  # without the licensed rule texts the addon prints a placeholder
        msg = f"MISRA C:2012 rule {rule['rule']}"
    return rel, int(m["line"]), int(m["col"]), m["id"], msg


def is_misra(ident):
    """Whether a finding comes from the MISRA addon (a rule, or its configuration report)."""
    return ident.startswith("misra-")


def misra_counts(findings):
    """{file: {rule id: number of distinct findings}} of the MISRA findings, sorted."""
    out = {}
    for (path, line, col, ident, _msg) in findings:
        if is_misra(ident):
            out.setdefault(path, {}).setdefault(ident, set()).add((line, col))
    return {f: {r: len(v) for r, v in sorted(rs.items())} for f, rs in sorted(out.items())}


def against_baseline(counts, baseline):
    """(gains, losses): [(file, rule, now, baseline)] where a count rose above or fell below."""
    gains, losses = [], []
    for f in sorted(set(counts) | set(baseline)):
        now, was = counts.get(f, {}), baseline.get(f, {})
        for r in sorted(set(now) | set(was)):
            a, b = now.get(r, 0), was.get(r, 0)
            if a > b:
                gains.append((f, r, a, b))
            elif a < b:
                losses.append((f, r, a, b))
    return gains, losses


def analyze(cppcheck, build, jobs, work, off=(), misra=False, root=ROOT):
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
                f"--template={TEMPLATE}"] + [f"--suppress={i}" for i in off] + \
            (["--addon=misra"] if misra else [])
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
    ap.add_argument("--misra", action="store_true", help="also run the MISRA addon")
    ap.add_argument("--misra-baseline", type=Path, help="gate MISRA findings against this baseline")
    ap.add_argument("--write-misra-baseline", type=Path, help="write the MISRA findings as a baseline")
    args = ap.parse_args(argv)
    if (args.misra_baseline or args.write_misra_baseline) and not args.misra:
        ap.error("--misra-baseline and --write-misra-baseline need --misra")
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
            found, broken = analyze(tool, build, max(1, args.jobs), work, off, args.misra)
            if broken:
                failed.append((build, broken))
            for f in found:
                seen.setdefault(f, set()).add(Path(build).name)

    for b, out in failed:
        print(f"cppcheck: {b}: cppcheck failed without a diagnostic:\n{out}", file=sys.stderr)
    gated = args.misra_baseline is not None
    other = {f: w for f, w in seen.items() if not is_misra(f[3])}
    shown = other if gated else seen  # under a baseline, MISRA findings are reported by count
    for (path, line, col, ident, msg), where in sorted(shown.items()):
        print(f"{path}:{line}:{col}: {msg} [{ident}] (in {', '.join(sorted(where))})")
    ids = {}
    for (_, _, _, ident, _) in seen:
        key = "misra" if is_misra(ident) and gated else ident
        ids[key] = ids.get(key, 0) + 1
    summary = ", ".join(f"{c} {n}" for c, n in sorted(ids.items())) or "none"
    print(f"cppcheck: {len(args.builds)} builds, {len(seen)} findings: {summary}")
    counts = misra_counts(seen)
    if args.write_misra_baseline:
        args.write_misra_baseline.write_text(json.dumps(counts, indent=1, sort_keys=True) + "\n", encoding="utf-8")
        print(f"cppcheck: wrote {args.write_misra_baseline}: "
              f"{sum(sum(r.values()) for r in counts.values())} MISRA findings in {len(counts)} files")
    status = 1 if other else 0
    if gated:
        baseline = json.loads(args.misra_baseline.read_text(encoding="utf-8"))
        gains, losses = against_baseline(counts, baseline)
        for f, r, now, was in gains:
            lines = sorted((ln, c) for (p, ln, c, i, _m) in seen if p == f and i == r)
            where = ", ".join(str(ln) for ln, _ in lines)
            print(f"{f}: {r}: {now} findings, the baseline has {was} (lines {where})")
        for f, r, now, was in losses:
            print(f"{f}: {r}: {now} findings, down from {was}: record it with --write-misra-baseline")
        print(f"cppcheck: MISRA against {args.misra_baseline.name}: {len(gains)} file and rule pairs above it, "
              f"{len(losses)} below it")
        status = 1 if (gains or other) else 0
    elif args.misra and not args.write_misra_baseline:
        status = 1 if seen else 0
    elif args.write_misra_baseline:
        status = 1 if other else 0
    if failed:
        return 2
    return status


if __name__ == "__main__":
    sys.exit(main())
