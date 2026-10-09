#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""The differential bridge between the kernel and the reference model (TEST-008, SPEC-004 §13).

Every scenario of the catalogue (tests/scenarios.py) is run on the native port by the
differential runner (tests/differential/diff_runner.c) under a schedule of interrupt raises
and ticks; the runner prints the kernel's trace and every operation's result. The bridge
replays that trace through the model: at each step it applies the one enabled model action
(run a section, fire an ISR, tick) whose events match the next kernel events, so the kernel's
interleaving is followed exactly at section granularity, with the model's invariants checked
after every action. A kernel event no model action can produce, a model event the kernel did
not produce, or a different outcome at the end is a divergence, and a divergence is a kernel
bug unless the specification changes first.

    python3 -I tools/model/bridge.py --runner build/native-gcc/tests/differential/diff_runner
"""
from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from embmodel import (Cancel, Destroy, Exit, Give, Join, Lock, ModelError, NotifySet,  # noqa: E402
                      NotifyWait, Policy, Reason, Result, Resume, Scenario, SchedLock, SchedUnlock,
                      SetPrio, Sleep, Start, Suspend, Take, Unlock, Yield, build, FOREVER, NO_WAIT)
from tests import scenarios as catalogue  # noqa: E402

# ---- the kernel's trace vocabulary (include/emb/trace.h, M1 subset) ------------------------

TR_SWITCH, TR_THREAD_INIT, TR_THREAD_START, TR_THREAD_EXIT, TR_THREAD_JOIN = 1, 2, 3, 4, 5
TR_BLOCK, TR_WAKE, TR_TIMEOUT_ARM, TR_TIMEOUT_EXPIRE = 6, 7, 8, 9
TR_MUTEX_LOCK, TR_MUTEX_UNLOCK, TR_SEM_TAKE, TR_SEM_GIVE = 16, 17, 18, 19
TR_NOTIFY_SET, TR_NOTIFY_WAIT, TR_FAULT, TR_TICK = 20, 21, 25, 29
EV_ISR_MARK = 1000                     # the runner's own record: ISR body i starts
EV_INDEX_MARK = 1001                   # the runner's record: thread tid now has index i
IDLE_INDEX = 0xFE
KERNEL_START_FROM = 0xFF

# emb_status_t -> Result (status.h; the model's EPERM stands in for EBUSY at a second joiner)
STATUS_TO_RESULT = {0: Result.SATISFIED, -4: Result.TIMEOUT, -5: Result.CANCELED, -6: Result.DESTROYED,
                    -16: Result.STALE, -32: Result.DEADLOCK, -1: Result.EPERM, -3: Result.EPERM,
                    -2: Result.STALE}   # EINVAL on a wait: the pointer handle of a destroyed object
# EMB_WAIT_REASON_* (thread.h: NONE 0, START 1, SUSPEND 2, SLEEP 3, OBJECT 4, JOIN 5, NOTIFY 6) -> Reason
KREASON_TO_REASON = {1: Reason.START, 2: Reason.SUSPEND, 3: Reason.SLEEP, 4: Reason.OBJECT,
                     5: Reason.JOIN, 6: Reason.NOTIFY}
# EMBK_WAKE_* (SATISFIED 0, TIMEOUT 1, CANCELED 2, DESTROYED 3, STALE 4) -> Result
KWAKE_TO_RESULT = {0: Result.SATISFIED, 1: Result.TIMEOUT, 2: Result.CANCELED, 3: Result.DESTROYED,
                   4: Result.STALE}
STATE_BLOCKED, STATE_TERMINATED = 3, 4
REASON_SUSPEND_K = 2


class Divergence(Exception):
    pass


def fault_result(fault_class, code):
    """The model result a checked-build misuse fault stands for (fault.h classes)."""
    if fault_class == 3:                   # EMB_FAULT_API_HANDLE: a destroyed object's handle
        return Result.STALE
    return STATUS_TO_RESULT.get(-code)     # EMB_FAULT_API_OWNER etc.: the status the call carries


# ---- scenario encoding for the runner -------------------------------------------------------

def _t(t):
    return -1 if t is FOREVER else int(t)


def encode_op(op) -> str:
    if isinstance(op, Take):
        return f"take {op.sem} {_t(op.timeout)}"
    if isinstance(op, Give):
        return f"give {op.sem}"
    if isinstance(op, Lock):
        return f"lock {op.mutex} {_t(op.timeout)}"
    if isinstance(op, Unlock):
        return f"unlock {op.mutex}"
    if isinstance(op, NotifySet):
        return f"nset {op.tid} {op.bits}"
    if isinstance(op, NotifyWait):
        return f"nwait {op.mask} {int(op.all)} {int(op.clear)} {_t(op.timeout)}"
    if isinstance(op, Sleep):
        return f"sleep {_t(op.ticks)}"
    if isinstance(op, Yield):
        return "yield"
    if isinstance(op, SetPrio):
        return f"setprio {op.tid} {op.prio}"
    if isinstance(op, Suspend):
        return f"suspend {op.tid}"
    if isinstance(op, Resume):
        return f"resume {op.tid}"
    if isinstance(op, Cancel):
        return f"cancel {op.tid}"
    if isinstance(op, Destroy):
        return f"destroy {op.sem}"
    if isinstance(op, SchedLock):
        return "slock"
    if isinstance(op, SchedUnlock):
        return "sunlock"
    if isinstance(op, Exit):
        return f"exit {op.code}"
    if isinstance(op, Start):
        return f"start {op.tid}"
    if isinstance(op, Join):
        return f"join {op.tid} {_t(op.timeout)}"
    raise ValueError(f"unknown op {op!r}")


def encode_scenario(sc: Scenario) -> str:
    parts = []
    for tid, (prio, prog) in sc.threads.items():
        attrs = f"{prio}" + (",inactive" if tid in sc.inactive else "")
        parts.append(f"T{tid}={attrs}:" + ";".join(encode_op(op) for op in prog))
    for sid, cfg in sc.sems.items():
        attrs = [str(cfg.get("count", 0))]
        if cfg.get("policy") == Policy.FIFO:
            attrs.append("fifo")
        if cfg.get("binding") is not None:
            tid, bit = cfg["binding"]
            attrs.append(f"bind={tid}.{bit}")
        parts.append(f"S{sid}=" + ",".join(attrs))
    for mid, cfg in sc.mutexes.items():
        proto = int(cfg.get("protocol", 0))
        parts.append(f"M{mid}={proto},{cfg.get('ceiling', 0)},{int(bool(cfg.get('recursive', False)))}")
    for i, body in enumerate(sc.isrs):
        parts.append(f"I{i}:" + ";".join(encode_op(op) for op in body))
    return "|".join(parts)


# ---- running the kernel ---------------------------------------------------------------------

class Run:
    def __init__(self):
        self.config = {}
        self.index_to_tid = {}
        self.events = []        # (id, context, ticks, a0, a1, a2) from the first kernel switch on
        self.results = {}       # tid -> {op_index: (status, data)}
        self.states = {}        # tid -> (state, reason)
        self.sems = {}          # sid -> count
        self.end = None         # ("done",) or ("fault", class, code)
        self.events_lost = 0
        self.exit_code = 0
        self.stdout = ""
        self.stderr = ""


def run_kernel(runner: str, sc: Scenario, schedule: list) -> Run:
    args = [runner, encode_scenario(sc)]
    if schedule:
        args += ["--schedule", ",".join(f"{kind}{idx}@{at}" if kind == "isr" else f"tick@{at}"
                                        for kind, idx, at in schedule)]
    p = subprocess.run(args, capture_output=True, text=True, timeout=60)
    r = Run()
    r.exit_code, r.stdout, r.stderr = p.returncode, p.stdout, p.stderr
    raw_events = []
    for line in p.stdout.splitlines():
        f = line.split()
        if not f:
            continue
        if f[0] == "config":
            r.config = dict(kv.split("=", 1) for kv in f[1:])
        elif f[0] == "thread":
            r.index_to_tid[int(f[3])] = int(f[1])
        elif f[0] == "ev":
            raw_events.append(tuple(int(x) for x in f[1:7]))
        elif f[0] == "res":
            r.results.setdefault(int(f[1]), {})[int(f[2])] = (int(f[3]), int(f[4]))
        elif f[0] == "state":
            r.states[int(f[1])] = (int(f[2]), int(f[3]))
        elif f[0] == "sem":
            r.sems[int(f[1])] = int(f[2])
        elif f[0] == "events_lost":
            r.events_lost = int(f[1])
        elif f[0] == "end":
            r.end = (f[1], int(f[2]), int(f[3]))
    # the trace proper starts at the kernel's first switch; what precedes it is construction
    for i, e in enumerate(raw_events):
        if e[0] == TR_SWITCH and e[3] == KERNEL_START_FROM:
            r.events = raw_events[i:]
            break
    return r


# ---- canonical events ------------------------------------------------------------------------

def kernel_canonical(run: Run):
    """The kernel's events in the shared vocabulary: ("switch", tid|None), ("wait_begin", tid,
    reason, qkey), ("wake", tid, result), ("wait_end", tid, result), ("notify_set", tid, bits),
    ("mutex_unlock", tid, mkey, new_owner), ("thread_exit", tid, code), ("thread_start", tid),
    ("tick", ticks), ("isr", i). Events the model does not express are dropped."""
    out = []
    tid_of = dict(run.index_to_tid)

    idle = 0 if run.config.get("profile") == "tiny" else IDLE_INDEX   # the table slot of EMB_PRIORITY_IDLE

    def thr(idx):
        if idx == idle:
            return None
        if idx not in tid_of:
            raise Divergence(f"kernel event names an unknown thread index {idx}")
        return tid_of[idx]

    for (eid, _ctx, ticks, a0, a1, a2) in run.events:
        if eid == TR_SWITCH:
            out.append(("switch", thr(a1)))
        elif eid == TR_BLOCK:
            out.append(("wait_begin", thr(a0), KREASON_TO_REASON[a1], ("q", a2)))
        elif eid == TR_WAKE:
            out.append(("wake", thr(a0), KWAKE_TO_RESULT[a1]))
        elif eid in (TR_SEM_TAKE, TR_NOTIFY_WAIT, TR_MUTEX_LOCK):
            st = a1 if eid == TR_NOTIFY_WAIT else a2
            st = st - (1 << 64) if st >= (1 << 63) else st   # printed as unsigned long
            res = Result.SATISFIED if st == -7 else STATUS_TO_RESULT.get(st)
            if res is None:
                raise Divergence(f"kernel wait ended with unmapped status {st}")
            out.append(("wait_end", thr(a0), res))
        elif eid == TR_THREAD_JOIN:
            st = a2 - (1 << 64) if a2 >= (1 << 63) else a2
            out.append(("wait_end", thr(a0), STATUS_TO_RESULT[st]))
        elif eid == TR_NOTIFY_SET:
            out.append(("notify_set", thr(a0), a1))
        elif eid == TR_MUTEX_UNLOCK:
            out.append(("mutex_unlock", thr(a0), ("m", a1), None if a2 == 0xFF else thr(a2)))
        elif eid == TR_THREAD_EXIT:
            code = a1 - (1 << 64) if a1 >= (1 << 63) else a1
            out.append(("thread_exit", thr(a0), code))
        elif eid == TR_THREAD_START:
            out.append(("thread_start", thr(a0)))
        elif eid == TR_TICK:
            out.append(("tick", a0))
        elif eid == EV_ISR_MARK:
            out.append(("isr", a0))
        elif eid == EV_INDEX_MARK:
            for idx in [i for i, t in tid_of.items() if t == a0]:
                del tid_of[idx]
            tid_of[a1] = a0
        elif eid == TR_FAULT and run.end is not None and run.end[0] == "fault":
            out.append(("fault", a0, a1))             # a non-fatal fault record is dropped
        # THREAD_INIT, TIMEOUT_ARM/EXPIRE, SEM_GIVE, SCHED_LOCK/UNLOCK, RESCHED_PEND, PRIORITY,
        # SUSPEND, RESUME, CANCEL, SLEEP, IDLE_*: no model counterpart
    return out


def model_canonical(cur, trace, act) -> list:
    """The model's events of one action in the shared vocabulary; @cur is the thread that
    was running before the action."""
    out = []
    if act[0] == "isr":
        out.append(("isr", act[1]))
    elif act[0] == "tick":
        out.append(("tick",))
    i = 0
    while i < len(trace):
        ev = trace[i]
        name = ev[0]
        if name == "switch":
            if ev[1] != cur:                           # the kernel traces a change of thread only
                out.append(("switch", ev[1]))
                cur = ev[1]
        elif name == "wait_begin":
            out.append(("wait_begin", ev[1], Reason(ev[3]), ("q", ev[2])))
        elif name == "wake":
            nxt = trace[i + 1] if i + 1 < len(trace) else None
            if nxt is not None and nxt[0] == "thread_start" and nxt[1] == ev[1]:
                pass                                   # the kernel starts a thread without a wake
            else:
                out.append(("wake", ev[1], Result(ev[2])))
        elif name == "wait_end":
            if ev[3] != "Sleep":                       # the kernel has no sleep-end event
                out.append(("wait_end", ev[1], Result(ev[2])))
        elif name == "notify_set":
            out.append(("notify_set", ev[1], ev[2]))
        elif name == "mutex_unlock":
            out.append(("mutex_unlock", ev[1], ("m", ev[2]), ev[3]))
        elif name == "thread_exit":
            # the kernel traces the exit (SPEC-008 §5 step 3) before it wakes the joiner (step 4)
            if out and out[-1][0] == "wake":
                out.insert(len(out) - 1, ("thread_exit", ev[1], ev[2]))
            else:
                out.append(("thread_exit", ev[1], ev[2]))
        elif name == "thread_start":
            out.append(("thread_start", ev[1]))
        # mutex_lock, notify_dropped, stale_timeout: no kernel counterpart
        i += 1
    return out


# ---- replay -----------------------------------------------------------------------------------

class Replay:
    """Guided search: the model follows the kernel's event stream."""

    def __init__(self, sc: Scenario, run: Run):
        self.sc = sc
        self.run = run
        self.kevents = kernel_canonical(run)
        self.qmap = {}          # kernel queue address -> model qid
        self.mmap = {}          # kernel mutex address -> model mid
        self.steps = 0
        self.max_steps = 20000

    def match(self, kev, mev, qmap, mmap) -> bool:
        """One kernel event against one model event; learns the object maps on the way."""
        if kev[0] != mev[0]:
            return False
        if kev[0] == "wait_begin":
            if kev[1] != mev[1] or kev[2] != mev[2]:
                return False
            kq, mq = kev[3][1], mev[3][1]
            if kq in qmap:
                return qmap[kq] == mq
            if mq in qmap.values():
                return False
            qmap[kq] = mq
            return True
        if kev[0] == "mutex_unlock":
            if kev[1] != mev[1] or kev[3] != mev[3]:
                return False
            km, mm = kev[2][1], mev[2][1]
            if km in mmap:
                return mmap[km] == mm
            if mm in mmap.values():
                return False
            mmap[km] = mm
            return True
        if kev[0] == "tick":
            return True                     # the tick count is compared through the events it causes
        return kev[1:] == mev[1:]

    def replay(self):
        k = build(self.sc)
        k.trace = []
        kev = self.kevents
        if not kev or kev[0][0] != "switch":
            raise Divergence("the kernel trace does not start with the first switch")
        if kev[0][1] != k.current:
            raise Divergence(f"first thread: kernel {kev[0][1]}, model {k.current}")
        # Depth-first over the model's actions. The kernel's stream decides nearly every
        # step: an ISR fires only where the kernel recorded it, a tick only where the kernel
        # ticked; a section that emits nothing may come first, which is the one place the
        # search branches (a tick or an interrupt can land after silent sections).
        stack = [(k, 1, dict(self.qmap), dict(self.mmap), (), 0)]
        best = (0, None)
        last_error = ""
        while stack:
            k, pos, qmap, mmap, path, last_ticks = stack.pop()
            self.steps += 1
            if self.steps > self.max_steps:
                raise Divergence("replay step budget exceeded")
            if pos > best[0]:
                best = (pos, path)
            if pos == len(kev):
                self.finish(k, path)
                return k, path
            nxt = kev[pos]
            candidates = []
            prev = next((e for e in reversed(kev[:pos]) if e[0] != "tick"), None)
            if nxt[0] == "fault" and prev is not None and prev[0] == "wait_end" \
                    and prev[2] == fault_result(nxt[1], nxt[2]):
                # the kernel traced the misuse result before faulting on it; already matched
                candidates.append((k, pos + 1, qmap, mmap, path + ("fault",), last_ticks))
            for act in k.enabled():
                if act[0] == "tick" or (act[0] == "isr" and nxt[0] != "isr"):
                    continue
                n = k.clone()
                before = {tid: len(t.results) for tid, t in k.threads.items()}
                try:
                    n.apply(act)
                except ModelError as e:
                    last_error = str(e)
                    if nxt[0] == "fault":
                        candidates.append((n, pos + 1, qmap, mmap, path + (act, "fault"), last_ticks))
                    continue
                if nxt[0] == "fault":
                    # a checked build faults on misuse where the model records the error result
                    want = fault_result(nxt[1], nxt[2])
                    new = [res for tid, t in n.threads.items() for res in t.results[before[tid]:]]
                    if want is not None and any(res[1] == want for res in new):
                        candidates.append((n, pos + 1, qmap, mmap, path + (act, "fault"), last_ticks))
                    elif not model_canonical(k.current, n.trace, act):
                        candidates.append((n, pos, qmap, mmap, path + (act,), last_ticks))
                    continue
                mevs = model_canonical(k.current, n.trace, act)
                q2, m2 = dict(qmap), dict(mmap)
                ok = True
                for j, mev in enumerate(mevs):
                    if pos + j >= len(kev) or not self.match(kev[pos + j], mev, q2, m2):
                        ok = False
                        break
                if ok:
                    candidates.append((n, pos + len(mevs), q2, m2, path + (act,), last_ticks))
            if nxt[0] == "tick" and nxt[1] <= last_ticks:
                candidates.append((k, pos + 1, qmap, mmap, path + (("skip-tick",),), last_ticks))
            elif nxt[0] == "tick":
                # one kernel tick event covers the idle-time jump to the next deadline: as
                # many model ticks, the earlier ones silent; unobservable when nothing is armed
                delta = nxt[1] - last_ticks
                n = k.clone()
                ok = True
                consumed = pos + 1
                q2, m2 = dict(qmap), dict(mmap)
                steps = ()
                for i in range(delta):
                    if ("tick",) not in n.enabled():
                        n.now += delta - i            # nothing armed: time still passes
                        break
                    cur = n.current
                    n.trace = []
                    try:
                        n.apply(("tick",))
                    except ModelError as e:
                        last_error = str(e)
                        ok = False
                        break
                    steps += (("tick",),)
                    mevs = model_canonical(cur, n.trace, ("tick",))[1:]
                    if i < delta - 1:
                        if mevs:
                            ok = False
                            last_error = (f"the model expired something {delta - 1 - i} ticks "
                                          f"before the kernel's deadline: {mevs}")
                            break
                        continue
                    for j, mev in enumerate(mevs):
                        if consumed + j >= len(kev) or not self.match(kev[consumed + j], mev, q2, m2):
                            ok = False
                            break
                    if ok:
                        consumed += len(mevs)
                if ok:
                    candidates.append((n, consumed, q2, m2, path + (steps or (("skip-tick",),)), nxt[1]))
            # the last candidate is tried first: the tick, then the ISR, then silent runs
            for c in candidates:
                stack.append(c)
        pos, path = best
        nxt = kev[pos] if pos < len(kev) else "<end>"
        note = f"; last model refusal: {last_error}" if last_error else ""
        raise Divergence(f"no model action produces kernel event #{pos} {nxt}; "
                         f"matched {pos}/{len(kev)} events, path so far {path}{note}")

    def finish(self, k, path):
        """The kernel reached the end of its run: the model's state must agree."""
        r = self.run
        if r.end is None:
            raise Divergence(f"the runner did not report an end (exit code {r.exit_code}): {r.stderr.strip()}")
        if r.end[0] == "fault":
            if not (path and path[-1] == "fault"):
                # the model must refuse the next step as the kernel did
                for act in k.enabled():
                    n = k.clone()
                    try:
                        n.apply(act)
                    except ModelError:
                        return
                raise Divergence(f"the kernel faulted (class {r.end[1]} code {r.end[2]}) but the model accepts every next action")
            return
        if path and path[-1] == "fault":
            raise Divergence("the model faulted where the kernel did not")
        for tid, t in k.threads.items():
            kres = r.results.get(tid, {})
            prog = self.sc.threads[tid][1]
            for (op_index, result, data) in t.results:
                if op_index >= len(prog):
                    continue
                op = prog[op_index]
                if op_index not in kres:
                    raise Divergence(f"thread {tid} op {op_index} {op!r}: model {result.name}, kernel has no result")
                kstatus, kdata = kres[op_index]
                kres_mapped = STATUS_TO_RESULT.get(kstatus)
                if isinstance(op, Sleep) and kstatus == 0:
                    kres_mapped = Result.TIMEOUT          # a completed sleep is EMB_OK in the kernel
                if kres_mapped != result:
                    raise Divergence(f"thread {tid} op {op_index} {op!r}: model {result.name}, kernel status {kstatus}")
                if isinstance(op, (NotifyWait, Join, Lock)) and result == Result.SATISFIED and kdata != data:
                    raise Divergence(f"thread {tid} op {op_index} {op!r}: model data {data}, kernel data {kdata}")
            kstate = r.states.get(tid)
            if kstate is None:
                raise Divergence(f"thread {tid}: no final state from the kernel")
            k_blocked = kstate[0] == STATE_BLOCKED and kstate[1] != REASON_SUSPEND_K
            m_blocked = (t.wait_state == 2) and not t.suspended
            if k_blocked != m_blocked:
                raise Divergence(f"thread {tid}: kernel blocked={k_blocked} (state {kstate}), model blocked={m_blocked}")
            if (kstate[0] == STATE_TERMINATED) != t.terminated:
                raise Divergence(f"thread {tid}: kernel terminated={kstate[0] == STATE_TERMINATED}, model {t.terminated}")
        for sid, s in k.sems.items():
            if sid in r.sems and not s.destroyed and r.sems[sid] != s.count:
                raise Divergence(f"semaphore {sid}: kernel count {r.sems[sid]}, model {s.count}")
        if k.enabled() and any(a[0] == "run" for a in k.enabled()):
            raise Divergence("the kernel finished while the model still has a thread to run")


# ---- schedules ---------------------------------------------------------------------------------

def schedules(sc: Scenario, runs: int, rng: random.Random, horizon: int = 40) -> list:
    """The natural run (ISRs at idle time), then seeded placements of ISRs and ticks."""
    out = [[]]
    n_isr = len(sc.isrs)
    for i in range(n_isr):
        out.append([("isr", i, 1)])                     # as early as possible
    while len(out) < runs:
        s = [("isr", i, rng.randint(1, horizon)) for i in range(n_isr)]
        for _ in range(rng.randint(0, 3)):
            s.append(("tick", 0, rng.randint(1, horizon)))
        s.sort(key=lambda x: x[2])
        out.append(s)
    return out[:max(runs, 1)]


def applicable(sc: Scenario, config: dict) -> tuple:
    profile = config.get("profile", "base")
    bitmap = any(cfg.get("policy") == Policy.BITMAP for cfg in sc.sems.values())
    prios = [p for p, _ in sc.threads.values()]
    if profile == "tiny":
        if len(set(prios)) != len(prios):
            return False, "shared priorities need the base profile"
        if any(cfg.get("policy") == Policy.FIFO for cfg in sc.sems.values()):
            return False, "FIFO queues need the base profile"
    elif bitmap:
        return False, "BITMAP queues need the tiny profile"
    return True, ""


def adapt(sc: Scenario, config: dict) -> Scenario:
    """The model's static configuration follows the runner's build."""
    if config.get("profile") == "tiny":
        sems = {sid: dict(cfg, policy=Policy.BITMAP) for sid, cfg in sc.sems.items()}
        sc = Scenario(threads=sc.threads, sems=sems, mutexes=sc.mutexes, isrs=sc.isrs,
                      owner_death=sc.owner_death, inactive=sc.inactive, name=sc.name)
    od = config.get("owner_death", "release")
    if od != sc.owner_death:
        sc = Scenario(threads=sc.threads, sems=sc.sems, mutexes=sc.mutexes, isrs=sc.isrs,
                      owner_death=od, inactive=sc.inactive, name=sc.name)
    return sc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--runner", required=True, help="path to the diff_runner executable")
    ap.add_argument("--runs", type=int, default=6, help="schedules per scenario (default 6)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--only", help="run the scenarios whose name contains this text")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    scenarios = [f() for f in catalogue.ALL]
    if args.only:
        scenarios = [s for s in scenarios if args.only in s.name]
    probe = run_kernel(args.runner, scenarios[0], [])
    if not probe.config:
        print("bridge: the runner printed no configuration line", file=sys.stderr)
        print(probe.stdout[-2000:], probe.stderr[-2000:], file=sys.stderr)
        return 2
    config = probe.config
    print(f"bridge: runner profile={config.get('profile')} owner_death={config.get('owner_death')} "
          f"checked={config.get('checked')}; {len(scenarios)} scenarios, {args.runs} runs each, seed {args.seed}")

    total = passed = skipped = 0
    failures = []
    for sc0 in scenarios:
        ok, why = applicable(sc0, config)
        if not ok:
            skipped += 1
            if args.verbose:
                print(f"  skip {sc0.name}: {why}")
            continue
        sc = adapt(sc0, config)
        for sched in schedules(sc, args.runs, rng):
            total += 1
            run = run_kernel(args.runner, sc, sched)
            try:
                if run.exit_code not in (0, 2):
                    raise Divergence(f"runner exit code {run.exit_code}: {run.stderr.strip()[-500:]}")
                if run.events_lost:
                    raise Divergence(f"{run.events_lost} events lost (buffer too small)")
                rp = Replay(sc, run)
                _k, path = rp.replay()
                passed += 1
                if args.verbose:
                    print(f"  ok   {sc.name} schedule={sched} steps={len(path)} events={len(rp.kevents)}")
            except Divergence as e:
                failures.append((sc.name, sched, str(e)))
                print(f"  FAIL {sc.name} schedule={sched}: {e}")
                if args.verbose:
                    for ev in kernel_canonical(run):
                        print("       ", ev)
    print(f"bridge: {passed}/{total} runs agree with the model, {skipped} scenarios skipped on this build, "
          f"{len(failures)} divergences")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
