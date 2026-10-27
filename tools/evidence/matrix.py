#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Requirement evidence matrix from collected case evidence (M2-04, TEST-009).

Joins the requirement catalog (docs/requirements/*.md) with case evidence files written
by collect.py and the configuration inventory (configurations.json). For every
requirement and configuration it reports the four separate facts of
docs/engineering/timing-and-evidence.md: design status, evidence outcome, execution
scope and coverage type.

A requirement counts as passed for a configuration only when an executed case that cites
it at run time passed with at least one assertion. Header @req tags, Verification fields
and IDs that merely appear in test sources are traceability links, never evidence. A
configuration without an evidence file is reported as not run, with its stated reason.

Groups whose migration into docs/requirements/ is still pending (the README's group table)
have no catalog entries; cases that cite them keep their evidence in rows marked
"uncataloged" rather than being dropped. A citation outside every known group is an error.
"""
import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "docs"))
from build import requirement_catalog  # noqa: E402

OUTCOMES = ("failed", "passed", "skipped", "incomplete")


def pending_groups(readme: Path = ROOT / "docs" / "requirements" / "README.md") -> set:
    """Requirement groups the README's table lists as pending migration."""
    groups = set()
    for line in readme.read_text(encoding="utf-8").splitlines():
        cols = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cols) >= 3 and cols[2] == "pending":
            groups.update(g.strip() for g in cols[0].split(","))
    return groups


def group_of(req: str) -> str:
    return req.rsplit("-", 1)[0]


def cell(cases: list) -> str:
    """The evidence outcome of one requirement in one configuration."""
    if not cases:
        return "no case"
    if any(c["outcome"] in ("failed", "incomplete") for c in cases):
        return "failed"
    if any(c["outcome"] == "passed" and c["asserts"] > 0 for c in cases):
        return "passed"
    if any(c["outcome"] == "skipped" for c in cases):
        return "skipped"
    return "no assertion"                 # executed, but checked nothing


def build_matrix(records: list, evidence: list, configs: dict) -> list:
    by_preset = {e["preset"]: e for e in evidence}
    cites = {}                              # (preset, requirement) -> [case]
    for e in evidence:
        for t in e["targets"]:
            for c in t["cases"]:
                for req in c["reqs"]:
                    cites.setdefault((e["preset"], req), []).append(dict(c, executable=t["name"]))
    rows = []
    for r in records:
        for preset, cfg in sorted(configs.items()):
            if preset in by_preset:
                cases = cites.get((preset, r["id"]), [])
                outcome, scope = cell(cases), cfg["scope"]
            else:
                cases = []
                outcome = "not run"
                scope = f"unrun: {cfg.get('unrun', 'no evidence file for this configuration')}"
            rows.append({"requirement": r["id"], "design_status": r["design_status"], "configuration": preset,
                         "evidence_outcome": outcome, "execution_scope": scope,
                         "coverage_type": "conformance" if cases else "",
                         "cases": " ".join(sorted({f"{c['executable']}:{c['name']}" for c in cases}))})
    return rows


def summary(rows: list, configs: dict) -> dict:
    out = {}
    for preset in sorted(configs):
        mine = [r for r in rows if r["configuration"] == preset]
        out[preset] = {k: sum(1 for r in mine if r["evidence_outcome"] == k)
                       for k in ("passed", "failed", "skipped", "no assertion", "no case", "not run")}
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("evidence", nargs="*", type=Path, help="files written by collect.py")
    ap.add_argument("--configurations", type=Path, default=Path(__file__).with_name("configurations.json"))
    ap.add_argument("--csv", type=Path, help="write the matrix as CSV")
    ap.add_argument("--json", type=Path, help="write the matrix and its summary as JSON")
    args = ap.parse_args(argv)
    configs = json.loads(args.configurations.read_text(encoding="utf-8"))["configurations"]
    evidence = [json.loads(p.read_text(encoding="utf-8")) for p in args.evidence]
    unknown = sorted({e["preset"] for e in evidence} - set(configs))
    if unknown:
        print(f"matrix: evidence for unlisted configurations {unknown}", file=sys.stderr)
        return 2
    records = requirement_catalog()
    known = {r["id"] for r in records}
    cited = {req for e in evidence for t in e["targets"] for c in t["cases"] for req in c["reqs"]}
    pending = pending_groups()
    uncataloged = sorted(r for r in cited - known if group_of(r) in pending)
    unknown_ids = sorted(r for r in cited - known if group_of(r) not in pending)
    records = records + [{"id": r, "design_status": "uncataloged (migration pending)"} for r in uncataloged]
    rows = build_matrix(records, evidence, configs)
    totals = summary(rows, configs)
    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    if args.json:
        args.json.write_text(json.dumps({"summary": totals, "uncataloged": uncataloged,
                                         "unknown_citations": unknown_ids, "rows": rows}, indent=2) + "\n",
                             encoding="utf-8")
    for preset, t in totals.items():
        print(f"{preset}: " + ", ".join(f"{v} {k}" for k, v in t.items() if v))
    if uncataloged:
        print(f"matrix: {len(uncataloged)} cited IDs belong to groups pending migration: {' '.join(uncataloged)}")
    if unknown_ids:
        print(f"matrix: cases cite unknown requirements {unknown_ids}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
