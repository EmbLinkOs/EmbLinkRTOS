#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Collect individual conformance case outcomes from a build's CTest log (M2-04).

Reads <build>/Testing/Temporary/LastTest.log, which keeps every test executable's output,
whether it ran natively, under QEMU or on EmbSIM, and extracts the framework's evidence
lines (docs/engineering/case-evidence.md):

    EVIDENCE v1 suite=<executable> config=<hash> profile=<p> board=<b> checked=<0|1>
    TEST <case>
      skip: <reason>
      req <ID> <ID> ...
    CASE <case> outcome=<passed|failed|skipped> asserts=<n> skips=<k>

and writes one JSON record per build: the source identity, the configuration, and per
executable its CTest result and its cases. A case that printed TEST without a CASE line
(the executable crashed or timed out inside it) is reported as "incomplete".
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "emblink-case-evidence"
VERSION = 1

BLOCK_START = re.compile(r"^\d+/\d+ Testing: (\S+)\s*$")
BLOCK_RESULT = re.compile(r"^Test (Passed|Failed)\.?\s*$")
EVIDENCE = re.compile(r"^EVIDENCE v1 (.*)$")
TEST = re.compile(r"^TEST (\S+)\s*$")
SKIP = re.compile(r"^  skip: (.*)$")
REQ = re.compile(r"^  req (.*)$")
CASE = re.compile(r"^CASE (\S+) outcome=(passed|failed|skipped) asserts=(\d+) skips=(\d+)\s*$")
FIELD = re.compile(r"(\w+)=(\S*)")


def parse_output(text: str) -> dict:
    """The evidence of one executable's output."""
    ident, cases, current = {}, [], None
    for line in text.splitlines():
        m = EVIDENCE.match(line)
        if m:
            ident = dict(FIELD.findall(m.group(1)))
            continue
        m = TEST.match(line)
        if m:
            if current is not None:
                cases.append(current)            # the previous case never reported its end
            current = {"name": m.group(1), "outcome": "incomplete", "asserts": 0, "skips": 0,
                       "skip_reasons": [], "reqs": []}
            continue
        if current is None:
            continue
        m = SKIP.match(line)
        if m:
            current["skip_reasons"].append(m.group(1).strip())
            continue
        m = REQ.match(line)
        if m:
            current["reqs"] = [r for r in m.group(1).split() if not r.startswith("+")]
            continue
        m = CASE.match(line)
        if m and m.group(1) == current["name"]:
            current.update(outcome=m.group(2), asserts=int(m.group(3)), skips=int(m.group(4)))
            cases.append(current)
            current = None
    if current is not None:
        cases.append(current)
    return {"identity": ident, "cases": cases}


def parse_log(text: str) -> list:
    """Split LastTest.log into executables with their CTest result and output."""
    targets, name, lines, in_output = [], None, [], False
    for line in text.splitlines():
        m = BLOCK_START.match(line)
        if m:
            name, lines, in_output = m.group(1), [], False
            continue
        if name is None:
            continue
        if line.startswith("Output:"):
            in_output = True
            continue
        if line.startswith("<end of output>"):
            in_output = False
            continue
        m = BLOCK_RESULT.match(line)
        if m and not in_output:
            ev = parse_output("\n".join(lines))
            targets.append({"name": name, "ctest": m.group(1).lower(), "suite": ev["identity"].get("suite"),
                            "identity": ev["identity"], "cases": ev["cases"]})
            name = None
            continue
        if in_output and not line.startswith("----------"):
            lines.append(line)
    return targets


def git_identity(root: Path) -> dict:
    def git(*args):
        return subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True,
                              check=False).stdout.strip()
    sha = git("rev-parse", "HEAD")
    return {"sha": sha or None, "dirty": bool(git("status", "--porcelain", "--untracked-files=no"))}


def collect(build: Path, preset: str, root: Path = ROOT) -> dict:
    log = build / "Testing" / "Temporary" / "LastTest.log"
    targets = parse_log(log.read_text(encoding="utf-8", errors="replace"))
    idents = {(t["identity"].get("config"), t["identity"].get("profile"), t["identity"].get("board"),
               t["identity"].get("checked")) for t in targets if t["identity"]}
    config = sorted(idents)[0] if len(idents) == 1 else None
    return {"schema": SCHEMA, "version": VERSION, "preset": preset, "source": git_identity(root),
            "config_hash": config[0] if config else None, "profile": config[1] if config else None,
            "board": config[2] if config else None, "checked": config[3] == "1" if config else None,
            "consistent_identity": len(idents) <= 1, "targets": targets}


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", required=True, type=Path, help="the build tree ctest ran in")
    ap.add_argument("--preset", required=True, help="the configuration's name, e.g. native-gcc")
    ap.add_argument("--out", required=True, type=Path, help="the JSON file to write")
    args = ap.parse_args(argv)
    try:
        record = collect(args.build, args.preset)
    except OSError as e:
        print(f"collect: {e}", file=sys.stderr)
        return 2
    args.out.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    cases = [c for t in record["targets"] for c in t["cases"]]
    counts = {k: sum(1 for c in cases if c["outcome"] == k) for k in ("passed", "failed", "skipped", "incomplete")}
    print(f"collect: {args.preset}: {len(record['targets'])} executables, {len(cases)} cases "
          + ", ".join(f"{v} {k}" for k, v in counts.items()))
    if not record["consistent_identity"]:
        print("collect: executables report different configurations", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
