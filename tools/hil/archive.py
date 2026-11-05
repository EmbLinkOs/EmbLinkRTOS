#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""File a hardware run's records in the repository (A89, 05 §4.4).

    archive.py BUILD/hil-archive --to docs/engineering/hil-runs/2026-11-05-arduino_uno-checked \\
               --preset avr-uno-hil --build checked [--report hil-uno-2026-11-05.md] \\
               [--update docs/engineering/hardware-runs.json]

run_hil.py --archive leaves, for each image it ran, <image>.log (the "hil:" line and the
console) and <image>.json (the record: board, port, flash command, SHA-256 of the image and of
what was flashed, git commit, times, outcome, summary, cases, BENCH results). This tool checks
that the records belong to one run (one board, one commit, one configuration), copies them to
--to, and writes index.json there: the run's totals and one line per image. With --update it
adds the run to hardware-runs.json, naming the directory as the entry's "archive", which
tools/support/matrix.py then checks against index.json.

Exit status 0, 1 when the records disagree or are missing, 2 on a usage error.
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

FORMAT = "emb-hil-run v1"


def load(src):
    recs = []
    for p in sorted(Path(src).glob("*.json")):
        rec = json.loads(p.read_text(encoding="utf-8"))
        if rec.get("format") != FORMAT:
            continue
        log = p.with_suffix(".log")
        if not log.is_file():
            raise ValueError(f"{p.name}: no {log.name} beside it")
        recs.append((p, log, rec))
    if not recs:
        raise ValueError(f"{src}: no {FORMAT} records")
    return recs


def summarize(recs, preset, build):
    """index.json's content; ValueError when the records are not one run."""
    boards = {r["board"] for _, _, r in recs}
    commits = {(r.get("git") or {}).get("commit") for _, _, r in recs}
    configs = {r["evidence"]["config"] for _, _, r in recs if "evidence" in r}
    if len(boards) != 1:
        raise ValueError(f"records from more than one board: {sorted(boards)}")
    if len(commits) != 1:
        raise ValueError(f"records from more than one commit: {sorted(c or '?' for c in commits)}")
    if len(configs) > 1:
        raise ValueError(f"records from more than one configuration: {sorted(configs)}")
    dirty = any((r.get("git") or {}).get("dirty") for _, _, r in recs)
    images = []
    for _, log, r in recs:
        images.append({"image": r["image"], "outcome": r["outcome"], "image_sha256": r["image_sha256"],
                       "flashed_sha256": r.get("flashed_sha256", {}), "log": log.name,
                       "cases": len(r.get("cases", [])),
                       "cases_passed": sum(1 for c in r.get("cases", []) if c["outcome"] == "passed"),
                       "fault_checks_by_reset": r.get("fault_checks_by_reset", 0),
                       "bench": r.get("bench", []), "started": r["started"]})
    return {
        "format": "emb-hil-archive v1",
        "board": boards.pop(),
        "preset": preset,
        "build": build,
        "date": min(r["started"] for _, _, r in recs)[:10],
        "configuration": configs.pop() if configs else None,
        "git": {"commit": commits.pop(), "dirty": dirty},
        "passed": sum(1 for i in images if i["outcome"] == "passed"),
        "total": len(images),
        "images": images,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", help="run_hil.py's --archive directory")
    ap.add_argument("--to", required=True, help="the run's directory under docs/engineering/hil-runs/")
    ap.add_argument("--preset", required=True)
    ap.add_argument("--build", required=True, help="checked, release (...), T1 (...)")
    ap.add_argument("--report", help="the narrative page in docs/engineering/ for hardware-runs.json")
    ap.add_argument("--update", help="hardware-runs.json to add the run to (needs --report)")
    args = ap.parse_args(argv)
    if args.update and not args.report:
        print("archive: --update needs --report", file=sys.stderr)
        return 2
    try:
        recs = load(args.src)
        index = summarize(recs, args.preset, args.build)
    except (ValueError, KeyError, OSError) as e:
        print(f"archive: {e}", file=sys.stderr)
        return 1
    dest = Path(args.to)
    dest.mkdir(parents=True, exist_ok=True)
    for rec_path, log, _ in recs:
        shutil.copyfile(rec_path, dest / rec_path.name)
        shutil.copyfile(log, dest / log.name)
    (dest / "index.json").write_text(json.dumps(index, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"archive: {index['board']} {args.build}: {index['passed']}/{index['total']} at "
          f"{index['git']['commit'][:12]}{' (dirty)' if index['git']['dirty'] else ''} -> {dest}")
    if args.update:
        runs_path = Path(args.update)
        doc = json.loads(runs_path.read_text(encoding="utf-8"))
        rel = dest.resolve().relative_to(runs_path.resolve().parent).as_posix()
        entry = {"board": index["board"], "preset": args.preset, "build": args.build, "date": index["date"],
                 "configuration": index["configuration"], "passed": index["passed"], "total": index["total"],
                 "report": args.report, "archive": rel}
        doc["runs"] = [r for r in doc["runs"] if r.get("archive") != rel] + [entry]
        runs_path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
        print(f"archive: added to {runs_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
