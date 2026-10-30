#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The M2-10 differential campaign: every catalogue scenario on each runner, for a range of
seeds, plus the exact timer regressions, summarized as JSON.

Each (runner, seed) pair runs `bridge.py --runs N --seed S` as a separate process, so the
per-seed schedules are the same as a manual run. Failures are captured by the bridge as
usual (build/differential-failures/); the summary records counts and identities only.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
LINE = re.compile(r"bridge: (\d+)/(\d+) runs agree with the model, (\d+) scenarios skipped on this build, "
                  r"(\d+) divergences")
CONFIG = re.compile(r"bridge: runner (profile=\S+ owner_death=\S+ checked=\S+ tickless=\S+)")


def bridge(runner, *args, timeout):
    cmd = [sys.executable, "-I", str(HERE / "bridge.py"), "--runner", str(runner), *map(str, args)]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    m = LINE.search(out.stdout)
    if m is None:
        raise RuntimeError(f"no bridge summary from {cmd}: {out.stdout[-500:]} {out.stderr[-500:]}")
    agree, total, skipped, diverged = map(int, m.groups())
    c = CONFIG.search(out.stdout)
    return dict(agree=agree, runs=total, skipped=skipped, divergences=diverged,
                config=c.group(1) if c else None, exit=out.returncode)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--runner", action="append", required=True, type=Path, help="a diff_runner; repeatable")
    ap.add_argument("--seeds", default="1-10", help="first-last (default 1-10)")
    ap.add_argument("--runs", type=int, default=40)
    ap.add_argument("--timeout", type=float, default=7200, help="per bridge process, seconds")
    ap.add_argument("--out", type=Path, required=True, help="the summary JSON to write")
    args = ap.parse_args(argv)
    first, last = (int(x) for x in args.seeds.split("-"))
    git = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"],
                                capture_output=True, text=True).stdout.strip())
    result = dict(schema="emblink-campaign", version=1, started=datetime.now(timezone.utc).isoformat(timespec="seconds"),
                  git_sha=git, tracked_dirty=dirty, runs_per_scenario=args.runs, seeds=[first, last], runners=[])
    for runner in (r.resolve() for r in args.runner):
        entry = dict(runner=str(runner.relative_to(ROOT)) if runner.is_relative_to(ROOT) else str(runner),
                     runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(), seeds={})
        for seed in range(first, last + 1):
            entry["seeds"][seed] = bridge(runner, "--runs", args.runs, "--seed", seed, timeout=args.timeout)
            print(f"campaign: {entry['runner']} seed {seed}: {entry['seeds'][seed]['agree']}/"
                  f"{entry['seeds'][seed]['runs']}", flush=True)
        entry["regressions"] = bridge(runner, "--regressions", timeout=args.timeout)
        entry["config"] = entry["regressions"]["config"]
        entry["total"] = dict(runs=sum(s["runs"] for s in entry["seeds"].values()),
                              agree=sum(s["agree"] for s in entry["seeds"].values()),
                              divergences=sum(s["divergences"] for s in entry["seeds"].values()))
        result["runners"].append(entry)
    result["finished"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    result["divergences"] = sum(r["total"]["divergences"] + r["regressions"]["divergences"] for r in result["runners"])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(f"campaign: {result['divergences']} divergences in all; summary {args.out}")
    return 0 if result["divergences"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
