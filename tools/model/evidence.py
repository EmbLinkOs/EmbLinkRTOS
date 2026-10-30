#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Capture differential failures, replay portable bundles, and reduce exact schedules."""
import argparse
from dataclasses import asdict, fields
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bridge

ROOT = Path(__file__).resolve().parents[2]
OPS = {op.__name__: op for op in (bridge.Take, bridge.Give, bridge.Lock, bridge.Unlock,
       bridge.NotifySet, bridge.NotifyWait, bridge.Sleep, bridge.Yield, bridge.SetPrio,
       bridge.Suspend, bridge.Resume, bridge.Cancel, bridge.Destroy, bridge.SchedLock,
       bridge.SchedUnlock, bridge.Exit, bridge.Start, bridge.Join,
       bridge.BindNotify, bridge.UnbindNotify, bridge.ThreadDestroy,
       bridge.EventSet, bridge.EventClear, bridge.EventWait, bridge.EventDestroy,
       bridge.CondWait, bridge.CondSignal, bridge.CondBroadcast, bridge.CondDestroy,
       bridge.MsgSend, bridge.MsgSendFront, bridge.MsgRecv, bridge.MsgPeek, bridge.MsgDestroy,
       bridge.Submit, bridge.WorkCancel, bridge.WorkCancelSync, bridge.WorkFlush, bridge.WorkqDestroy,
       bridge.TimerStart, bridge.TimerStop, bridge.TimerStopSync, bridge.DworkSchedule, bridge.DworkCancel)}


def scenario_data(sc):
    def program(ops):
        return [dict(op=type(op).__name__, args=asdict(op)) for op in ops]
    data = dict(name=sc.name, threads=[dict(tid=tid, priority=p, program=program(ops))
                for tid, (p, ops) in sc.threads.items()],
                sems=list(sc.sems.items()), mutexes=list(sc.mutexes.items()),
                isrs=[program(ops) for ops in sc.isrs], inactive=list(sc.inactive),
                owner_death=sc.owner_death, tickless=sc.tickless)
    if sc.events:                                    # absent from bundles that predate event flags
        data["events"] = list(sc.events.items())
    if sc.conds:                                     # and condition variables
        data["conds"] = list(sc.conds.items())
    if sc.msgqs:                                     # and message queues
        data["msgqs"] = list(sc.msgqs.items())
    if sc.workqs:                                    # and work queues with their items' handlers
        data["workqs"] = list(sc.workqs.items())
        data["works"] = [(w, dict({k: v for k, v in cfg.items() if k != "body"}, body=program(cfg.get("body", ()))))
                         for w, cfg in sc.works.items()]
    if sc.timers:                                    # and software timers with their callbacks
        data["timers"] = [(t, dict({k: v for k, v in cfg.items() if k != "body"},
                                   body=program(cfg.get("body", ())))) for t, cfg in sc.timers.items()]
    if sc.horizon is not None:
        data["horizon"] = sc.horizon
    return data


def scenario_from_data(raw):
    def program(ops):
        result = []
        for item in ops:
            cls = OPS.get(item["op"])
            if cls is None or set(item["args"]) != {f.name for f in fields(cls)}:
                raise ValueError("unknown operation or changed operation fields")
            if any(v is not None and type(v) not in (int, bool) for v in item["args"].values()):
                raise ValueError("operation arguments must be integers, booleans or null")
            result.append(cls(**item["args"]))
        return tuple(result)
    def objects(items, enum_field=None, enum_type=None):
        result = {}
        for key, value in items:
            if type(key) is not int or key in result:
                raise ValueError("object IDs must be unique integers")
            value = dict(value)
            if enum_field in value:
                value[enum_field] = enum_type(value[enum_field])
            if "binding" in value and value["binding"] is not None:
                value["binding"] = tuple(value["binding"])
            result[key] = value
        return result
    if type(raw["tickless"]) is not bool or raw["owner_death"] not in ("release", "fault"):
        raise ValueError("invalid clock mode or owner-death policy")
    threads = {}
    for item in raw["threads"]:
        tid, priority = item["tid"], item["priority"]
        if type(tid) is not int or tid in threads or type(priority) is not int:
            raise ValueError("thread IDs must be unique integers with integer priorities")
        threads[tid] = (priority, program(item["program"]))
    if not threads:
        raise ValueError("a scenario needs at least one thread")
    from embmodel import Protocol
    return bridge.Scenario(name=raw["name"], threads=threads,
                           sems=objects(raw["sems"], "policy", bridge.Policy),
                           mutexes=objects(raw["mutexes"], "protocol", Protocol),
                           events=objects(raw.get("events", []), "policy", bridge.Policy),
                           conds=objects(raw.get("conds", []), "policy", bridge.Policy),
                           msgqs=objects(raw.get("msgqs", []), "policy", bridge.Policy),
                           workqs=objects(raw.get("workqs", [])),
                           works={w: dict(cfg, body=program(cfg["body"])) for w, cfg in objects(raw.get("works", [])).items()},
                           timers={t: dict(cfg, body=program(cfg["body"]))
                                   for t, cfg in objects(raw.get("timers", [])).items()},
                           horizon=raw.get("horizon"),
                           isrs=tuple(program(ops) for ops in raw["isrs"]),
                           inactive=tuple(raw["inactive"]), owner_death=raw["owner_death"],
                           tickless=raw["tickless"])


def sha256(path):
    try:
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()
    except OSError:
        return None


def configuration_values_sha256(config):
    """Compare effective values, excluding checkout-dependent origin paths."""
    values = {name: {key: symbol[key] for key in ("type", "value")}
              for name, symbol in config["symbols"].items()}
    return hashlib.sha256(json.dumps(values, sort_keys=True).encode()).hexdigest()


def identity(runner):
    def git(*args):
        try:
            return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True,
                                           stderr=subprocess.DEVNULL).strip()
        except (OSError, subprocess.CalledProcessError):
            return None
    sources = [ROOT/"tools/model/bridge.py", ROOT/"tools/model/evidence.py",
               ROOT/"tools/model/tests/scenarios.py", ROOT/"tests/differential/diff_runner.c",
               ROOT/"tools/toolchain/kernel_compile.py", ROOT/"tools/toolchain/native_abi.c"]
    sources += sorted((ROOT/"tools/model/embmodel").glob("*.py"))
    tree = hashlib.sha256()
    for folder in ("kernel", "arch", "boards", "include", "configs", "cmake"):
        for path in sorted((ROOT/folder).rglob("*")):
            if path.is_file():
                tree.update(str(path.relative_to(ROOT)).encode())
                tree.update((sha256(path) or "unreadable").encode())
    status = git("status", "--porcelain", "--untracked-files=no")
    result = dict(git_sha=git("rev-parse", "HEAD"),
                  tracked_dirty=None if status is None else bool(status),
                  source_sha256={str(p.relative_to(ROOT)): sha256(p) for p in sources},
                  kernel_tree_sha256=tree.hexdigest(), python=sys.version, platform=platform.platform(),
                  runner_path=str(Path(runner).resolve()), runner_sha256=sha256(runner), build=None)
    for directory in Path(runner).resolve().parents:
        config = directory/"generated/config.json"
        if not config.is_file():
            continue
        # Read build metadata; never execute compiler/command paths found in a capture/cache.
        cache = directory/"CMakeCache.txt"
        cache_text = cache.read_text() if cache.is_file() else ""
        metadata = (r"CMAKE_(?:C_COMPILER|CACHE_(?:MAJOR|MINOR|PATCH)_VERSION)"
                    r"|EMB_KERNEL_COMPILER(?:_VERSION|_SHA256)?")
        values = dict(re.findall(rf"^({metadata}):[^=]+=(.*)$", cache_text, re.M))
        compiler_versions = []
        for compiler in sorted((directory/"CMakeFiles").glob("*/CMakeCCompiler.cmake")):
            compiler_versions += re.findall(r'set\(CMAKE_C_COMPILER_VERSION "([^"]+)"\)', compiler.read_text())
        resolved = json.loads(config.read_text())
        result["build"] = dict(config_path=str(config), config_sha256=sha256(config),
                              config=resolved, configuration_values_sha256=configuration_values_sha256(resolved),
                              cmake_cache=values,
                              compiler_versions=compiler_versions)
        break
    return result


def make_bundle(sc, schedule, run, error, runner, discovery=None, model_config=None,
                timeout=60, max_steps=20000, fingerprint=None):
    after = identity(runner)
    before = fingerprint or after
    consistent = all(before[field] == after[field] for field in
                     ("source_sha256", "kernel_tree_sha256", "runner_sha256", "build"))
    return dict(format="emblink-differential", version=1,
                captured_at=datetime.now(timezone.utc).isoformat(),
                scenario=scenario_data(sc), encoded_scenario=bridge.encode_scenario(sc),
                schedule=schedule, runner_args=bridge.runner_args(sc, schedule),
                model_config=model_config if model_config is not None else run.config,
                limits=dict(runner_timeout=timeout, replay_steps=max_steps),
                discovery=discovery or {}, identity=before, identity_consistent=consistent,
                identity_after=None if consistent else after,
                execution=dict(stdout=run.stdout, stderr=run.stderr, exit_code=run.exit_code,
                               error=run.execution_error),
                failure=dict(kind=error.kind, signature=error.signature, message=str(error)))


def write_bundle(bundle, path):
    """Exclusive creation: a replay/reduction cannot overwrite its original evidence."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x") as stream:
        stream.write(json.dumps(bundle, indent=2, sort_keys=True) + "\n")
    return path


def capture_failure(directory, *args, **kwargs):
    bundle = make_bundle(*args, **kwargs)
    name = re.sub(r"[^A-Za-z0-9_-]", "_", bundle["scenario"]["name"])[:64] or "probe"
    return write_bundle(bundle, Path(directory)/f"{name}-{uuid.uuid4().hex}.json")


def read_bundle(path):
    bundle = json.loads(Path(path).read_text())
    if bundle.get("format") != "emblink-differential" or bundle.get("version") != 1:
        raise ValueError("unsupported bundle format/version (manual M2-01 archives use the documented regression tests)")
    sc = scenario_from_data(bundle["scenario"])
    schedule = []
    for kind, index, at in bundle["schedule"]:
        if kind not in ("tick", "isr") or type(index) is not int or type(at) is not int or at < 1:
            raise ValueError("invalid schedule placement")
        if (kind == "tick" and index != 0) or (kind == "isr" and not 0 <= index < len(sc.isrs)):
            raise ValueError("invalid schedule interrupt index")
        schedule.append((kind, index, at))
    if bridge.encode_scenario(sc) != bundle["encoded_scenario"] or \
            bridge.runner_args(sc, schedule) != bundle["runner_args"]:
        raise ValueError("scenario/schedule disagree with the recorded runner arguments")
    if not math.isfinite(bundle["limits"]["runner_timeout"]) or \
            bundle["limits"]["runner_timeout"] <= 0 or \
            type(bundle["limits"]["replay_steps"]) is not int or bundle["limits"]["replay_steps"] <= 0:
        raise ValueError("bundle execution limits must be positive")
    return bundle, sc, schedule


def verdict(sc, run, config, max_steps):
    try:
        bridge.check_run(sc, run, config, max_steps)
    except bridge.Divergence as error:
        return error
    return None


def build_configuration_error(bundle, current):
    expected = bundle["identity"]["build"]
    if expected is None:                     # bootstrap/custom-runner captures may lack build metadata
        return None
    actual = current["build"]
    if actual is None and current["runner_sha256"] is not None and \
            current["runner_sha256"] == bundle["identity"]["runner_sha256"]:
        return None                         # a byte-identical binary can be moved outside its build tree
    old = expected.get("configuration_values_sha256") or configuration_values_sha256(expected["config"])
    new = actual["configuration_values_sha256"] if actual is not None else None
    if old != new:
        return bridge.Divergence("resolved build configuration changed or is unavailable",
                                 "build_config", ["build_config", old, new])
    return None


def replay(bundle, sc, schedule, runner=None, timeout=None):
    if runner:
        configuration_error = build_configuration_error(bundle, identity(runner))
        run = bridge.run_kernel(runner, sc, schedule, timeout or bundle["limits"]["runner_timeout"])
        if configuration_error:
            return run, configuration_error
    else:
        output = bundle["execution"]
        run = bridge.execution_run(output["stdout"], output["stderr"], output["exit_code"], output["error"])
    return run, verdict(sc, run, bundle["model_config"], bundle["limits"]["replay_steps"])


class ReductionBudget(Exception):
    pass


def reduce_schedule(bundle, sc, schedule, runner, max_attempts=100, max_seconds=120, timeout=None):
    """Greedy deletion with two-run confirmation. Scenario/ordinal values remain fixed."""
    if not bundle["identity_consistent"]:
        raise ValueError("capture source/runner/build changed during execution; reduction refused")
    deadline = time.monotonic() + max_seconds
    attempts = 0
    timeout = timeout or bundle["limits"]["runner_timeout"]
    fingerprint = identity(runner)
    if build_configuration_error(bundle, fingerprint):
        raise ValueError("resolved build configuration changed or is unavailable; reduction refused")
    signature = bundle["failure"]["signature"]
    def evaluate(candidate):
        nonlocal attempts
        remaining = deadline - time.monotonic()
        if attempts >= max_attempts or remaining <= 0:
            raise ReductionBudget()
        attempts += 1
        effective_timeout = min(timeout, remaining)
        run = bridge.run_kernel(runner, sc, candidate, effective_timeout)
        if run.execution_error and run.execution_error["kind"] == "runner_timeout" and effective_timeout < timeout:
            raise ReductionBudget()  # a shortened tool budget is not a reproduced runner timeout
        return run, verdict(sc, run, bundle["model_config"], bundle["limits"]["replay_steps"])
    for _ in range(2):
        run, error = evaluate(schedule)
        if error is None or error.signature != signature:
            raise ValueError("original failure signature did not reproduce twice; reduction refused")
    current, last_run, last_error = list(schedule), run, error
    index = 0
    exhausted = False
    try:
        while index < len(current):
            candidate = current[:index] + current[index+1:]
            run, error = evaluate(candidate)
            if error is not None and error.signature == signature:
                confirmed_run, confirmed_error = evaluate(candidate)
                if confirmed_error is not None and confirmed_error.signature == signature:
                    current, last_run, last_error = candidate, confirmed_run, confirmed_error
                    index = 0
                    continue
            index += 1
    except ReductionBudget:
        exhausted = True
    after = identity(runner)
    for field in ("source_sha256", "kernel_tree_sha256", "runner_sha256", "build"):
        if fingerprint[field] != after[field]:
            raise ValueError("source/runner/build identity changed during reduction")
    result = make_bundle(sc, current, last_run, last_error, runner, model_config=bundle["model_config"],
                         timeout=timeout, max_steps=bundle["limits"]["replay_steps"], fingerprint=fingerprint)
    result["reduction"] = dict(original_schedule=schedule, attempts=attempts,
                               confirmations=2, max_attempts=max_attempts, max_seconds=max_seconds,
                               outcome="budget_exhausted" if exhausted else "one_entry_minimal")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("replay", "reduce"):
        command = subparsers.add_parser(name)
        command.add_argument("bundle", type=Path)
        command.add_argument("--runner", required=name == "reduce")
        command.add_argument("--timeout", type=float)
        if name == "reduce":
            command.add_argument("--output", type=Path, required=True)
            command.add_argument("--max-attempts", type=int, default=100)
            command.add_argument("--max-seconds", type=float, default=120)
    args = parser.parse_args()
    try:
        if args.timeout is not None and (not math.isfinite(args.timeout) or args.timeout <= 0):
            raise ValueError("timeout must be positive")
        bundle, sc, schedule = read_bundle(args.bundle)
        if args.command == "reduce":
            if args.max_attempts < 2 or not math.isfinite(args.max_seconds) or args.max_seconds <= 0:
                raise ValueError("reduction needs at least two attempts and a positive time budget")
            result = reduce_schedule(bundle, sc, schedule, args.runner, args.max_attempts,
                                     args.max_seconds, args.timeout)
            result["reduction"]["parent_sha256"] = sha256(args.bundle)
            write_bundle(result, args.output)
            reduced = result["reduction"]
            print(f"reduction: {len(schedule)} -> {len(result['schedule'])} placements; "
                  f"{reduced['attempts']} attempts; {reduced['outcome']}; saved {args.output}")
            return 1 if reduced["outcome"] == "budget_exhausted" else 0
        current = identity(args.runner or bundle["identity"]["runner_path"])
        if not bundle["identity_consistent"]:
            print("replay: source/runner/build changed during the captured execution; provenance is mixed")
        print(f"replay: captured source={bundle['identity']['git_sha']} current={current['git_sha']}; "
              f"model sources {'match' if current['source_sha256'] == bundle['identity']['source_sha256'] else 'changed'}")
        if args.runner:
            print(f"replay: runner hash {'matches' if current['runner_sha256'] == bundle['identity']['runner_sha256'] else 'changed'}")
        run, error = replay(bundle, sc, schedule, args.runner, args.timeout)
        if error is None:
            print("replay: AGREES with the current model; assess changed identities before declaring a fix")
            return 0
        matches = error.signature == bundle["failure"]["signature"]
        print(f"replay: {error.kind}; signature {'matches' if matches else 'changed'}: {error}")
        return 1
    except (OSError, ValueError, TypeError, KeyError, IndexError, ReductionBudget) as error:
        print(f"evidence: {error or 'budget exhausted before baseline confirmation'}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
