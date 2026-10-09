"""Kernel state and operations of the reference model.

Specification references are given as SPEC-004 §n (wait protocol) and SPEC-005 §n
(synchronization) unless another document is named. The model is deliberately literal:
`_block_section`, `wake`, `wake_timeout`, `_op_lock`, `_op_unlock`, `recompute`, and
`propagate` are the pseudocode of the specifications with the sections made explicit.
"""
from __future__ import annotations

import copy
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional


class WaitState(IntEnum):
    READY = 0
    INTEND_TO_BLOCK = 1
    BLOCKED = 2


class Result(IntEnum):
    """Wait results (SPEC-004 §2.3) plus the non-wait outcomes a lock can produce (SPEC-005 §3.1)."""
    SATISFIED = 0
    TIMEOUT = 1
    CANCELED = 2
    DESTROYED = 3
    STALE = 4
    DEADLOCK = 10        # EMB_EDEADLK: self-relock of a non-recursive mutex, or a detected cycle
    EPERM = 11           # EMB_EPERM: unlock by a non-owner, ceiling violation (release-build outcome)


OWNERDEAD_FLAG = 2       # hand-off word flag: the previous owner died (EMB_EOWNERDEAD)


class Reason(IntEnum):          # 03 §1.1
    START = 0
    SUSPEND = 1
    SLEEP = 2
    OBJECT = 3
    JOIN = 4
    NOTIFY = 5


class Policy(IntEnum):          # SPEC-004 §3 and §11
    PRIORITY_FIFO = 0
    FIFO = 1
    BITMAP = 2                  # tiny profile: unique priorities, one bit per thread


class Protocol(IntEnum):        # SPEC-005 §3
    INHERIT = 0
    CEILING = 1
    NONE = 2


FOREVER = None                  # EMB_WAIT_FOREVER
NO_WAIT = 0                     # EMB_NO_WAIT
CANCELABLE = frozenset({Reason.SLEEP, Reason.OBJECT, Reason.JOIN, Reason.NOTIFY})   # KRN-WAIT-015
PI_MAX_DEPTH = 8                # CONFIG_EMB_PI_MAX_DEPTH default
MUTEX_QID_BASE = 100            # queue ids: semaphores 0..99, mutexes 100.., thread pseudo-queues negative


class ModelError(AssertionError):
    """A checked-build fault or a violated invariant."""


# ----------------------------------------------------------------------------------------
# Static program descriptions. A thread's program is a tuple of these.
# ----------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Take:
    sem: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class Give:
    sem: int


@dataclass(frozen=True)
class Lock:
    mutex: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class Unlock:
    mutex: int


@dataclass(frozen=True)
class NotifySet:
    """emb_notify_set(thread, bits): thread or ISR context (SPEC-006 §2)."""
    tid: int
    bits: int


@dataclass(frozen=True)
class NotifyWait:
    """emb_notify_wait(mask, mode, timeout): ANY unless all=True; clear applies to satisfied bits."""
    mask: int
    all: bool = False
    clear: bool = True
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class Sleep:
    ticks: Optional[int]


@dataclass(frozen=True)
class Yield:
    pass


@dataclass(frozen=True)
class SetPrio:
    tid: int
    prio: int


@dataclass(frozen=True)
class Suspend:
    tid: int


@dataclass(frozen=True)
class Resume:
    tid: int


@dataclass(frozen=True)
class Cancel:
    tid: int


@dataclass(frozen=True)
class Destroy:
    sem: int


@dataclass(frozen=True)
class SchedLock:
    pass


@dataclass(frozen=True)
class SchedUnlock:
    pass


@dataclass(frozen=True)
class Exit:
    code: int = 0


@dataclass(frozen=True)
class Start:
    """emb_thread_start(tid): wake an INACTIVE thread (reason START); thread or ISR context."""
    tid: int


@dataclass(frozen=True)
class Join:
    """emb_thread_join(tid, timeout): one joiner; the exit code arrives by hand-off."""
    tid: int
    timeout: Optional[int] = FOREVER


ISR_SAFE_OPS = (Give, Resume, NotifySet, Start)   # SPEC-001 §5.2: signal-type operations are `thread isr`


# ----------------------------------------------------------------------------------------
# Dynamic state
# ----------------------------------------------------------------------------------------

@dataclass
class Thread:
    tid: int
    prio: int                                 # effective priority (SPEC-005 §2)
    base_prio: int = 0
    wait_state: WaitState = WaitState.READY
    wait_gen: int = 0
    wait_reason: Optional[Reason] = None
    wait_queue: Optional[int] = None
    wake_result: Optional[Result] = None
    wake_data: int = 0
    suspended: bool = False
    cancel_pending: bool = False
    terminated: bool = False
    op_index: int = 0
    pc: int = 0
    saved_gen: int = 0
    deadline: Optional[int] = None            # absolute, fixed when the call is made (SPEC-003 §5.2)
    results: tuple = ()                       # ((op_index, Result, data), ...)
    notify_bits: int = 0
    notify_mask: int = 0                      # mask of the NOTIFY wait in progress
    notify_all: bool = False
    notify_clear: bool = True
    owned: list = field(default_factory=list)  # mutex ids in acquisition order (KRN-SYNC-014)
    inactive: bool = False                    # INACTIVE: initialized, not started (SPEC-008 §4)
    exit_code: Optional[int] = None           # set at termination (SPEC-008 §5)
    joiner: Optional[int] = None              # the one joiner's tid, while blocked (SPEC-008 §6)

    def key(self):
        return (self.tid, self.prio, self.base_prio, int(self.wait_state), self.wait_gen,
                None if self.wait_reason is None else int(self.wait_reason), self.wait_queue,
                None if self.wake_result is None else int(self.wake_result), self.wake_data,
                self.suspended, self.cancel_pending, self.terminated, self.op_index, self.pc,
                self.saved_gen, self.deadline, self.results, self.notify_bits, self.notify_mask, self.notify_all,
                self.notify_clear, tuple(self.owned), self.inactive, self.exit_code, self.joiner)


@dataclass
class WaitQueue:
    qid: int
    policy: Policy
    waiters: list = field(default_factory=list)   # [(tid, seq)], head first

    def key(self):
        return (self.qid, int(self.policy), tuple(self.waiters))


@dataclass
class Semaphore:
    sid: int
    count: int
    qid: int
    initial: int = 0
    destroyed: bool = False
    gen: int = 0
    gives: int = 0
    handed: int = 0
    immediate: int = 0
    binding: Optional[tuple] = None

    def key(self):
        return (self.sid, self.count, self.qid, self.initial, self.destroyed, self.gen, self.gives,
                self.handed, self.immediate, self.binding)


@dataclass
class Mutex:
    mid: int
    qid: int
    protocol: Protocol = Protocol.INHERIT
    ceiling: int = 0
    recursive: bool = False
    owner: Optional[int] = None
    count: int = 0
    inconsistent: bool = False

    def key(self):
        return (self.mid, self.qid, int(self.protocol), self.ceiling, self.recursive, self.owner,
                self.count, self.inconsistent)


@dataclass
class Scenario:
    """Static description of a test configuration."""
    threads: dict                              # tid -> (prio, program tuple)
    sems: dict = field(default_factory=dict)   # sid -> dict(count=, policy=, binding=)
    mutexes: dict = field(default_factory=dict)  # mid -> dict(protocol=, ceiling=, recursive=)
    isrs: tuple = ()
    owner_death: str = "release"               # CONFIG_EMB_MUTEX_OWNER_DEATH: "release" | "fault"
    inactive: tuple = ()                       # tids created INACTIVE (must be started by another thread or an ISR)
    name: str = ""


def build(sc: Scenario) -> "Kernel":
    k = Kernel(programs={tid: tuple(p) for tid, (_, p) in sc.threads.items()}, isr_bodies=tuple(sc.isrs),
               owner_death=sc.owner_death)
    for tid, (prio, _) in sc.threads.items():
        k.threads[tid] = Thread(tid=tid, prio=prio, base_prio=prio, inactive=(tid in sc.inactive))
        k.queues[-(tid + 1)] = WaitQueue(qid=-(tid + 1), policy=Policy.FIFO)        # SLEEP / NOTIFY / START
        k.queues[-(1000 + tid)] = WaitQueue(qid=-(1000 + tid), policy=Policy.FIFO)  # join slot
    bitmap = any(cfg.get("policy") == Policy.BITMAP for cfg in sc.sems.values())
    for sid, cfg in sc.sems.items():
        if sid >= MUTEX_QID_BASE:
            raise ModelError("semaphore ids must be below 100")
        k.queues[sid] = WaitQueue(qid=sid, policy=cfg.get("policy", Policy.PRIORITY_FIFO))
        k.sems[sid] = Semaphore(sid=sid, count=cfg.get("count", 0), initial=cfg.get("count", 0), qid=sid,
                                binding=cfg.get("binding"))
    for mid, cfg in sc.mutexes.items():
        qid = MUTEX_QID_BASE + mid
        k.queues[qid] = WaitQueue(qid=qid, policy=Policy.BITMAP if bitmap else Policy.PRIORITY_FIFO)
        k.mutexes[mid] = Mutex(mid=mid, qid=qid, protocol=cfg.get("protocol", Protocol.INHERIT),
                               ceiling=cfg.get("ceiling", 0), recursive=cfg.get("recursive", False))
        k.qid_mutex[qid] = mid
    if bitmap:
        prios = [t.prio for t in k.threads.values()]
        if len(set(prios)) != len(prios):
            raise ModelError("BITMAP wait queues require unique priorities (ADR-036)")
    for t in k.threads.values():
        if t.inactive:
            q = k.queues[-(t.tid + 1)]
            t.wait_reason = Reason.START
            t.wait_queue = q.qid
            k._enqueue(q, t)
            t.wait_state = WaitState.BLOCKED
        else:
            k._make_ready(t, preempted=False)
    k.reschedule_pending = False
    k.dispatch()
    return k


@dataclass
class Kernel:
    programs: dict
    isr_bodies: tuple = ()
    owner_death: str = "release"
    threads: dict = field(default_factory=dict)
    queues: dict = field(default_factory=dict)
    sems: dict = field(default_factory=dict)
    mutexes: dict = field(default_factory=dict)
    qid_mutex: dict = field(default_factory=dict)   # static map, not part of the key
    ready: dict = field(default_factory=dict)
    current: Optional[int] = None
    now: int = 0
    irq_nesting: int = 0
    sched_lock_depth: int = 0
    reschedule_pending: bool = False
    timeouts: list = field(default_factory=list)
    seq: int = 0
    dequeued: frozenset = frozenset()
    stale_timeouts: int = 0
    isr_fired: frozenset = frozenset()
    pi_depth_max: int = 0
    trace: list = field(default_factory=list)

    # ------------------------------------------------------------------ state identity
    def key(self):
        return (tuple(t.key() for _, t in sorted(self.threads.items())),
                tuple(q.key() for _, q in sorted(self.queues.items())),
                tuple(s.key() for _, s in sorted(self.sems.items())),
                tuple(m.key() for _, m in sorted(self.mutexes.items())),
                tuple((p, tuple(v)) for p, v in sorted(self.ready.items()) if v),
                self.current, self.now, self.irq_nesting, self.sched_lock_depth,
                self.reschedule_pending, tuple(self.timeouts), self.dequeued,
                self.stale_timeouts, self.isr_fired, self.pi_depth_max)

    def clone(self) -> "Kernel":
        k = copy.copy(self)
        k.threads = {i: copy.copy(t) for i, t in self.threads.items()}
        for t in k.threads.values():
            t.owned = list(t.owned)
        k.queues = {i: WaitQueue(q.qid, q.policy, list(q.waiters)) for i, q in self.queues.items()}
        k.sems = {i: copy.copy(s) for i, s in self.sems.items()}
        k.mutexes = {i: copy.copy(m) for i, m in self.mutexes.items()}
        k.ready = {p: list(v) for p, v in self.ready.items()}
        k.timeouts = list(self.timeouts)
        k.trace = []
        return k

    def _next_seq(self) -> int:
        self.seq += 1
        return self.seq

    def _event(self, *ev):
        self.trace.append(ev)

    # ------------------------------------------------------------------ scheduler helpers
    def runnable(self, t: Thread) -> bool:
        return t.wait_state != WaitState.BLOCKED and not t.suspended and not t.terminated

    def in_ready(self, tid: int) -> bool:
        return any(tid in v for v in self.ready.values())

    def _make_ready(self, t: Thread, preempted: bool):
        lst = self.ready.setdefault(t.prio, [])
        if preempted:
            lst.insert(0, t.tid)                 # KRN-SCH-041
        else:
            lst.append(t.tid)
        cur = self.threads.get(self.current) if self.current is not None else None
        if cur is None or t.prio > cur.prio:
            self.reschedule_pending = True

    def _remove_ready(self, tid: int):
        for v in self.ready.values():
            if tid in v:
                v.remove(tid)
                return

    def _best_ready(self) -> Optional[int]:
        for p in sorted(self.ready, reverse=True):
            if self.ready[p]:
                return self.ready[p][0]
        return None

    def dispatch(self):
        cur = self.threads.get(self.current) if self.current is not None else None
        best_tid = self._best_ready()
        if cur is not None and self.runnable(cur):
            if best_tid is None or self.threads[best_tid].prio <= cur.prio:
                self.reschedule_pending = False
                return
            self._make_ready(cur, preempted=True)
        if best_tid is None:
            self.current = None
        else:
            self.ready[self.threads[best_tid].prio].pop(0)
            self.current = best_tid
        self.reschedule_pending = False
        self._event("switch", self.current)

    def reschedule_if_needed(self):
        if self.reschedule_pending and self.sched_lock_depth == 0:
            self.dispatch()

    def _check_preempt_current(self):
        best = self._best_ready()
        cur = self.threads.get(self.current) if self.current is not None else None
        if cur is not None and best is not None and self.threads[best].prio > cur.prio:
            self.reschedule_pending = True

    # ------------------------------------------------------------------ wait queues
    def _insert_by_prio(self, q: WaitQueue, t: Thread):
        s = self._next_seq()
        i = len(q.waiters)
        while i > 0 and self.threads[q.waiters[i - 1][0]].prio < t.prio:
            i -= 1
        q.waiters.insert(i, (t.tid, s))

    def _enqueue(self, q: WaitQueue, t: Thread):
        if q.policy == Policy.FIFO:
            q.waiters.append((t.tid, self._next_seq()))
        else:
            self._insert_by_prio(q, t)

    def _dequeue(self, q: WaitQueue, t: Thread):
        for i, (tid, _) in enumerate(q.waiters):
            if tid == t.tid:
                del q.waiters[i]
                return
        raise ModelError(f"thread {t.tid} not in queue {q.qid}")

    def wait_first(self, q: WaitQueue) -> Optional[Thread]:
        return self.threads[q.waiters[0][0]] if q.waiters else None

    def _queue_mutex(self, qid: Optional[int]) -> Optional[Mutex]:
        if qid is None or qid not in self.qid_mutex:
            return None
        return self.mutexes[self.qid_mutex[qid]]

    def wait_requeue(self, t: Thread):                                   # SPEC-004 §6.4
        if t.wait_queue is None:
            return
        q = self.queues[t.wait_queue]
        if q.policy == Policy.FIFO:
            return
        self._dequeue(q, t)
        self._insert_by_prio(q, t)
        self._event("wait_requeue", t.tid, t.prio)

    # ------------------------------------------------------------------ effective priority (SPEC-005 §2)
    def effective(self, t: Thread) -> int:
        eff = t.base_prio
        for mid in t.owned:
            m = self.mutexes[mid]
            if m.protocol == Protocol.CEILING:
                eff = max(eff, m.ceiling)
            elif m.protocol == Protocol.INHERIT:
                w = self.wait_first(self.queues[m.qid])
                if w is not None:
                    eff = max(eff, w.prio)
        return eff

    def _set_eff_prio(self, t: Thread, new: int) -> bool:
        """Apply a new effective priority (SPEC-005 §2.1). Returns True when it changed."""
        if new == t.prio:
            return False
        old = t.prio
        if self.in_ready(t.tid):
            self._remove_ready(t.tid)
            t.prio = new
            self._make_ready(t, preempted=False)
        else:
            t.prio = new
            if self.current == t.tid:
                self._check_preempt_current()
        if t.wait_queue is not None:
            self.wait_requeue(t)                                         # KRN-WAIT-003
        self._event("prio_change", t.tid, old, new)
        return True

    def propagate(self, t: Thread):
        """SPEC-005 §2.2: recompute along the owner chain, both directions, bounded depth."""
        depth = 0
        while True:
            if not self._set_eff_prio(t, self.effective(t)):
                return
            m = self._queue_mutex(t.wait_queue)
            if m is None or m.protocol != Protocol.INHERIT or m.owner is None:
                return
            depth += 1
            self.pi_depth_max = max(self.pi_depth_max, depth)
            if depth > PI_MAX_DEPTH:
                raise ModelError(f"inheritance depth {depth} exceeds CONFIG_EMB_PI_MAX_DEPTH (KRN-SYNC-009)")
            t = self.threads[m.owner]

    def _chain_reaches(self, start: Optional[int], target: int) -> bool:
        """Follow owner -> mutex it waits on -> owner ...; True if `target` is on the chain (SPEC-005 §3.5)."""
        depth = 0
        tid = start
        while tid is not None:
            if tid == target:
                return True
            m = self._queue_mutex(self.threads[tid].wait_queue)
            if m is None or m.protocol != Protocol.INHERIT:
                return False
            depth += 1
            if depth > PI_MAX_DEPTH:
                return False
            tid = m.owner
        return False

    # ------------------------------------------------------------------ timeouts (SPEC-003 §5)
    def timeout_arm(self, t: Thread, deadline: int, gen: int):
        if any(n[2] == t.tid for n in self.timeouts):
            raise ModelError(f"thread {t.tid} already has an armed timeout (SPEC-004 §16.9)")
        self.timeouts.append((deadline, self._next_seq(), t.tid, gen))
        self.timeouts.sort()

    def timeout_disarm(self, t: Thread):
        self.timeouts = [n for n in self.timeouts if n[2] != t.tid]

    def timeout_armed(self, tid: int) -> bool:
        return any(n[2] == tid for n in self.timeouts)

    # ------------------------------------------------------------------ wake (SPEC-004 §5.2)
    def wake(self, t: Thread, result: Result, data: int = 0, q: Optional[WaitQueue] = None,
             handoff: bool = False) -> bool:
        if q is None or t.wait_queue != q.qid:
            return False
        self._dequeue(q, t)
        t.wait_queue = None
        t.wake_result = result
        t.wake_data = data
        pair = (t.tid, t.wait_gen)
        if pair in self.dequeued:
            raise ModelError(f"second dequeue for {pair} (KRN-WAIT-004)")
        self.dequeued = self.dequeued | {pair}
        t.wait_gen += 1
        if t.wait_state == WaitState.INTEND_TO_BLOCK:
            t.wait_state = WaitState.READY
            self._event("wake", t.tid, int(result), "intend")
        elif t.wait_state == WaitState.BLOCKED:
            t.wait_state = WaitState.READY
            self.timeout_disarm(t)
            if not t.suspended:
                self._make_ready(t, preempted=False)
            self._event("wake", t.tid, int(result), "blocked")
        else:
            raise ModelError(f"wake of thread {t.tid} in state {t.wait_state}")
        if q.qid <= -1000:                                                 # join slot (SPEC-008 §6)
            target = self.threads[-(q.qid + 1000)]
            if target.joiner == t.tid:
                target.joiner = None
        # SPEC-005 §3.3 / KRN-SYNC-022: a waiter left a mutex queue for a reason other than hand-off
        m = self._queue_mutex(q.qid)
        if not handoff and m is not None and m.protocol == Protocol.INHERIT and m.owner is not None:
            self.propagate(self.threads[m.owner])
        return True

    def wake_timeout(self, tid: int, gen: int):
        t = self.threads[tid]
        if t.wait_gen != gen or t.wait_queue is None:
            self.stale_timeouts += 1
            self._event("stale_timeout", tid, gen)
            return
        self.wake(t, Result.TIMEOUT, 0, self.queues[t.wait_queue])

    def wake_all(self, q: WaitQueue, result: Result, data: int = 0) -> int:
        n = 0
        while q.waiters:
            self.wake(self.threads[q.waiters[0][0]], result, data, q)
            n += 1
        return n

    # ------------------------------------------------------------------ blocking (SPEC-004 §5.1)
    def _current_thread(self) -> Thread:
        return self.threads[self.current]

    def _finish(self, t: Thread, result: Result, data: int = 0):
        prog = self.programs[t.tid]
        op = prog[t.op_index] if t.op_index < len(prog) else Exit()
        t.results = t.results + ((t.op_index, result, data),)
        t.op_index += 1
        t.pc = 0
        t.wait_reason = None
        t.wake_result = None
        t.wake_data = 0
        self._event("wait_end", t.tid, int(result), type(op).__name__)

    def _advance(self, t: Thread):
        t.op_index += 1
        t.pc = 0

    def _block_section(self, t: Thread, q: WaitQueue, reason: Reason, timeout: Optional[int],
                       satisfied, consume, pre_nowait=None, pre_block=None, on_enqueue=None) -> bool:
        """Sections 1 to 3 of SPEC-004 §5.1 driven by `t.pc`. `pre_nowait` runs before the
        EMB_NO_WAIT check and `pre_block` after it; either may return a Result to finish with
        (SPEC-005 §3.2: self-relock, then cycle detection). `on_enqueue` runs after the enqueue
        inside section 1 (the inheritance walk). Returns True when the op completed."""
        if t.pc == 0:
            if t.cancel_pending:
                t.cancel_pending = False
                self._finish(t, Result.CANCELED)
                return True
            if satisfied():
                self._finish(t, Result.SATISFIED, consume())
                return True
            if pre_nowait is not None:
                r = pre_nowait()
                if r is not None:
                    self._finish(t, r)
                    return True
            if timeout == NO_WAIT:
                self._finish(t, Result.TIMEOUT)
                return True
            if pre_block is not None:
                r = pre_block()
                if r is not None:
                    self._finish(t, r)
                    return True
            if self.irq_nesting > 0 or self.sched_lock_depth > 0:
                raise ModelError("blocking with a nonzero timeout from ISR or under the scheduler lock (SPEC-001 §5.3)")
            if t.wait_state != WaitState.READY:
                raise ModelError(f"block while wait_state is {t.wait_state} (invariant)")
            t.saved_gen = t.wait_gen
            t.deadline = None if timeout is FOREVER else self.now + timeout   # fixed at the call
            t.wait_reason = reason
            t.wait_queue = q.qid
            self._enqueue(q, t)
            t.wait_state = WaitState.INTEND_TO_BLOCK
            if on_enqueue is not None:
                on_enqueue()
            t.pc = 1
            self._event("wait_begin", t.tid, q.qid, int(reason))
            return False
        if t.pc == 1:
            if t.deadline is not None:
                self.timeout_arm(t, t.deadline, t.saved_gen)
                if t.deadline <= self.now:
                    self._expire_due()        # already due: the timer fires at once (SPEC-003 §4.2)
            t.pc = 2
            return False
        if t.pc == 2:
            if t.wait_state == WaitState.INTEND_TO_BLOCK:
                t.wait_state = WaitState.BLOCKED
                t.pc = 3
                self.dispatch()
                return False
            self.timeout_disarm(t)
            t.pc = 3
        if t.wait_state != WaitState.READY or t.wake_result is None:
            raise ModelError(f"thread {t.tid} resumed without a result")
        self._finish(t, t.wake_result, t.wake_data)
        return True

    # ------------------------------------------------------------------ semaphore (SPEC-005 §4)
    def _op_take(self, t: Thread, op: Take) -> bool:
        sem = self.sems[op.sem]
        if sem.destroyed and t.pc == 0:
            self._finish(t, Result.STALE)
            return True
        q = self.queues[sem.qid]

        def consume():
            sem.count -= 1
            sem.immediate += 1
            return 1
        return self._block_section(t, q, Reason.OBJECT, op.timeout, lambda: sem.count > 0, consume)

    def _op_give(self, op: Give):
        sem = self.sems[op.sem]
        if sem.destroyed:
            return
        q = self.queues[sem.qid]
        sem.gives += 1
        w = self.wait_first(q)
        if w is not None:
            self.wake(w, Result.SATISFIED, 1, q, handoff=True)
            sem.handed += 1
            return
        sem.count += 1
        if sem.binding is not None and sem.count == 1:                    # ready transition (SPEC-004 §6.3)
            tid, bit = sem.binding
            self._notify_set(self.threads[tid], 1 << bit)

    # ------------------------------------------------------------------ notifications (SPEC-006 §2)
    @staticmethod
    def _notify_satisfied(t: Thread) -> bool:
        got = t.notify_bits & t.notify_mask
        return (got == t.notify_mask) if t.notify_all else (got != 0)

    def _notify_set(self, t: Thread, bits: int):
        """emb_notify_set: OR the bits; hand over and wake the owner if its wait is now satisfied.
        A set to a terminated thread is dropped (SPEC-006 §3.2, SPEC-008 §5)."""
        if t.terminated:
            self._event("notify_dropped", t.tid, bits)
            return
        t.notify_bits |= bits
        self._event("notify_set", t.tid, bits)
        q = self.queues[-(t.tid + 1)]
        if t.wait_state != WaitState.READY and t.wait_reason == Reason.NOTIFY and t.wait_queue == q.qid \
                and self._notify_satisfied(t):
            got = t.notify_bits & t.notify_mask
            if t.notify_clear:
                t.notify_bits &= ~got
            self.wake(t, Result.SATISFIED, got, q, handoff=True)

    def _op_notify_wait(self, t: Thread, op: NotifyWait) -> bool:
        q = self.queues[-(t.tid + 1)]
        if t.pc == 0:
            t.notify_mask, t.notify_all, t.notify_clear = op.mask, op.all, op.clear

        def satisfied():
            return self._notify_satisfied(t)

        def consume():
            got = t.notify_bits & t.notify_mask
            if t.notify_clear:
                t.notify_bits &= ~got
            return got
        return self._block_section(t, q, Reason.NOTIFY, op.timeout, satisfied, consume)

    # ------------------------------------------------------------------ mutex (SPEC-005 §3)
    def _acquire(self, t: Thread, m: Mutex) -> int:
        m.owner = t.tid
        m.count = 1
        t.owned.append(m.mid)
        self.propagate(t)                                                 # ceiling raise, if any
        self._event("mutex_lock", t.tid, m.mid)
        return OWNERDEAD_FLAG if m.inconsistent else 0

    def _op_lock(self, t: Thread, op: Lock) -> bool:
        m = self.mutexes[op.mutex]
        q = self.queues[m.qid]

        def satisfied():
            return m.owner is None or (m.owner == t.tid and m.recursive)

        def consume():
            if m.owner == t.tid:
                m.count += 1                                              # recursive re-entry
                return 0
            if m.protocol == Protocol.CEILING and t.base_prio > m.ceiling:
                return None                                               # handled by pre check below
            return self._acquire(t, m)

        def pre_nowait():
            if m.owner == t.tid:
                return Result.DEADLOCK                                    # non-recursive self-relock
            return None

        def pre_block():
            if m.protocol == Protocol.INHERIT and self._chain_reaches(m.owner, t.tid):
                return Result.DEADLOCK                                    # SPEC-005 §3.5
            return None

        def on_enqueue():
            if m.protocol == Protocol.INHERIT:
                self.propagate(self.threads[m.owner])                     # raise the owner's chain

        if t.pc == 0 and m.owner is None and m.protocol == Protocol.CEILING and t.base_prio > m.ceiling:
            self._finish(t, Result.EPERM)                                  # ceiling violation (release build)
            return True
        return self._block_section(t, q, Reason.OBJECT, op.timeout, satisfied, consume,
                                   pre_nowait, pre_block, on_enqueue)

    def _release(self, t: Thread, m: Mutex, dead: bool):
        """Give `m` up on behalf of `t`: hand-off to the highest waiter or free it (SPEC-005 §3.3, §3.6)."""
        t.owned.remove(m.mid)
        q = self.queues[m.qid]
        w = self.wait_first(q)
        if w is not None:
            m.owner = w.tid
            m.count = 1
            w.owned.append(m.mid)
            flag = OWNERDEAD_FLAG if (dead or m.inconsistent) else 0
            self.wake(w, Result.SATISFIED, flag, q, handoff=True)
            self._event("mutex_unlock", t.tid, m.mid, w.tid)
            if not dead:
                m.inconsistent = False
            self.propagate(w)                                             # inherit from the remaining waiters
        else:
            m.owner = None
            m.count = 0
            m.inconsistent = dead                                         # RELEASE policy without a waiter
            self._event("mutex_unlock", t.tid, m.mid, None)
        if not t.terminated:
            self.propagate(t)                                             # KRN-SYNC-010 over the still-owned list

    def _op_unlock(self, t: Thread, op: Unlock):
        m = self.mutexes[op.mutex]
        if m.owner != t.tid:
            t.results = t.results + ((t.op_index, Result.EPERM, 0),)      # KRN-SYNC-008 (release build)
            return
        if m.count > 1:
            m.count -= 1
            return
        self._release(t, m, dead=False)

    def _exit(self, t: Thread, code: int = 0):
        """SPEC-008 §5: release mutexes, store the code, wake the joiner by hand-off, terminate, switch."""
        if t.owned:
            if self.owner_death == "fault":
                raise ModelError(f"thread {t.tid} exits owning mutexes {t.owned} (CONFIG_EMB_MUTEX_OWNER_DEATH=FAULT)")
            for mid in list(t.owned):                                     # acquisition order
                self._release(t, self.mutexes[mid], dead=True)
        t.exit_code = code
        jq = self.queues[-(1000 + t.tid)]
        w = self.wait_first(jq)
        if w is not None:
            self.wake(w, Result.SATISFIED, code, jq, handoff=True)
            t.joiner = None
        t.notify_bits = 0
        t.cancel_pending = False
        t.terminated = True
        self._event("thread_exit", t.tid, code)
        self.current = None
        self.dispatch()

    def _op_start(self, op: Start):
        t = self.threads[op.tid]
        if not t.inactive:
            return                                                        # EMB_ESTATE in the kernel
        t.inactive = False
        self.wake(t, Result.SATISFIED, 0, self.queues[-(t.tid + 1)], handoff=True)
        self._event("thread_start", t.tid)

    def _op_join(self, t: Thread, op: Join) -> bool:
        target = self.threads[op.tid]
        if t.pc == 0:
            if op.tid == t.tid:
                self._finish(t, Result.DEADLOCK)                          # EMB_EDEADLK
                return True
            if target.joiner is not None and target.joiner != t.tid:
                self._finish(t, Result.EPERM)                             # stands in for EMB_EBUSY: second joiner
                return True
        jq = self.queues[-(1000 + op.tid)]

        def satisfied():
            return target.terminated

        def consume():
            return target.exit_code if target.exit_code is not None else 0

        def on_enqueue():
            target.joiner = t.tid
        done = self._block_section(t, jq, Reason.JOIN, op.timeout, satisfied, consume, on_enqueue=on_enqueue)
        if done and target.joiner == t.tid:
            target.joiner = None
        return done

    # ------------------------------------------------------------------ other operations
    def _op_sleep(self, t: Thread, op: Sleep) -> bool:
        if op.ticks == 0 and t.pc == 0:
            self._op_yield(t)
            return True
        q = self.queues[-(t.tid + 1)]
        return self._block_section(t, q, Reason.SLEEP, op.ticks, lambda: False, lambda: 0)

    def _op_yield(self, t: Thread):
        self._make_ready(t, preempted=False)
        self.current = None
        self.dispatch()
        self._advance(t)

    def _op_setprio(self, op: SetPrio):
        t = self.threads[op.tid]
        t.base_prio = op.prio
        self.propagate(t)                                                 # SPEC-005 §10

    def _op_suspend(self, op: Suspend):
        t = self.threads[op.tid]
        if t.terminated or t.suspended:
            return
        t.suspended = True
        if self.in_ready(t.tid):
            self._remove_ready(t.tid)
        if self.current == t.tid:
            self.reschedule_pending = True

    def _op_resume(self, op: Resume):
        t = self.threads[op.tid]
        if not t.suspended:
            return
        t.suspended = False
        if t.wait_state != WaitState.BLOCKED and not t.terminated and self.current != t.tid:
            self._make_ready(t, preempted=False)

    def _op_cancel(self, op: Cancel):
        t = self.threads[op.tid]
        if t.terminated:
            return
        if t.wait_state != WaitState.READY and t.wait_reason in CANCELABLE and t.wait_queue is not None:
            self.wake(t, Result.CANCELED, 0, self.queues[t.wait_queue])
            return
        t.cancel_pending = True

    def _op_destroy(self, op: Destroy):
        sem = self.sems[op.sem]
        if sem.destroyed:
            return
        self.wake_all(self.queues[sem.qid], Result.DESTROYED)
        sem.destroyed = True
        sem.gen += 1

    def step_thread(self) -> bool:
        if self.current is None:
            return False
        t = self._current_thread()
        prog = self.programs[t.tid]
        op = prog[t.op_index] if t.op_index < len(prog) else Exit()
        if isinstance(op, Take):
            self._op_take(t, op)
        elif isinstance(op, Lock):
            self._op_lock(t, op)
        elif isinstance(op, Unlock):
            self._op_unlock(t, op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Sleep):
            self._op_sleep(t, op)
        elif isinstance(op, NotifyWait):
            self._op_notify_wait(t, op)
        elif isinstance(op, NotifySet):
            self._notify_set(self.threads[op.tid], op.bits)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Give):
            self._op_give(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Yield):
            self._op_yield(t)
        elif isinstance(op, SetPrio):
            self._op_setprio(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Suspend):
            self._op_suspend(op)
            self._advance(t)
            if op.tid == t.tid:
                self.current = None
                self.dispatch()
            else:
                self.reschedule_if_needed()
        elif isinstance(op, Resume):
            self._op_resume(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Cancel):
            self._op_cancel(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Destroy):
            self._op_destroy(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, SchedLock):
            self.sched_lock_depth += 1
            self._advance(t)
        elif isinstance(op, SchedUnlock):
            if self.sched_lock_depth == 0:
                raise ModelError("scheduler unlock without lock")
            self.sched_lock_depth -= 1
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Start):
            self._op_start(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, Join):
            self._op_join(t, op)
        elif isinstance(op, Exit):
            if self.sched_lock_depth:
                raise ModelError("thread exit with the scheduler locked")
            self._exit(t, op.code)
        else:
            raise ModelError(f"unknown op {op!r}")
        return True

    # ------------------------------------------------------------------ interrupts and time
    def fire_isr(self, index: int):
        if index in self.isr_fired:
            raise ModelError("ISR already fired")
        self.isr_fired = self.isr_fired | {index}
        self.irq_nesting += 1
        for op in self.isr_bodies[index]:
            if not isinstance(op, ISR_SAFE_OPS):
                raise ModelError(f"{op!r} is not ISR-safe (SPEC-001 §5.2)")
            if isinstance(op, Give):
                self._op_give(op)
            elif isinstance(op, Resume):
                self._op_resume(op)
            elif isinstance(op, NotifySet):
                self._notify_set(self.threads[op.tid], op.bits)
            elif isinstance(op, Start):
                self._op_start(op)
        self.irq_nesting -= 1
        self._p1()

    def tick(self):
        self.now += 1
        self._expire_due()

    def _expire_due(self):
        """The timer interrupt: every deadline at or before now, in deadline order."""
        self.irq_nesting += 1
        while self.timeouts and self.timeouts[0][0] <= self.now:
            _, _, tid, gen = self.timeouts.pop(0)
            self.wake_timeout(tid, gen)
        self.irq_nesting -= 1
        self._p1()

    def _p1(self):
        if self.irq_nesting == 0 and self.reschedule_pending and self.sched_lock_depth == 0:
            self.dispatch()

    # ------------------------------------------------------------------ invariants
    def check_invariants(self):
        for t in self.threads.values():
            membership = [q.qid for q in self.queues.values() if any(tid == t.tid for tid, _ in q.waiters)]
            if t.wait_state != WaitState.READY:
                if t.wait_queue is None or membership != [t.wait_queue]:
                    raise ModelError(f"invariant 1: thread {t.tid} state {t.wait_state} queues {membership}")
            elif membership or t.wait_queue is not None:
                raise ModelError(f"invariant 1: READY thread {t.tid} still in {membership}")
            ready_count = sum(v.count(t.tid) for v in self.ready.values())
            if ready_count > 1:
                raise ModelError(f"thread {t.tid} twice in the ready structure")
            should_be_ready = self.runnable(t) and self.current != t.tid
            if should_be_ready != (ready_count == 1):
                raise ModelError(f"invariant 5: thread {t.tid} ready={ready_count} runnable={self.runnable(t)} current={self.current}")
            if self.timeout_armed(t.tid) and t.wait_state == WaitState.READY and t.pc != 2:
                raise ModelError(f"thread {t.tid} has an armed timeout while READY outside the window")
            # SPEC-005 §2.1 / KRN-SYNC-018
            if not t.terminated and t.prio != self.effective(t):
                raise ModelError(f"effective priority of thread {t.tid} is {t.prio}, expected {self.effective(t)}")
            if t.terminated and (membership or t.owned or t.wait_state != WaitState.READY):
                raise ModelError(f"terminated thread {t.tid} still in queues {membership} or owning {t.owned}")
            jq = self.queues[-(1000 + t.tid)]
            if len(jq.waiters) > 1:
                raise ModelError(f"thread {t.tid} has {len(jq.waiters)} joiners (KRN-THR-018)")
            if t.terminated and jq.waiters:
                raise ModelError(f"terminated thread {t.tid} still has a blocked joiner")
            if t.wait_state == WaitState.BLOCKED and t.wait_reason == Reason.NOTIFY and self._notify_satisfied(t):
                raise ModelError(f"thread {t.tid} is blocked on notifications that are already satisfied (KRN-NOTIF-008)")
            if len(set(t.owned)) != len(t.owned):
                raise ModelError(f"thread {t.tid} owned list has duplicates: {t.owned}")
            for mid in t.owned:
                if self.mutexes[mid].owner != t.tid:
                    raise ModelError(f"thread {t.tid} lists mutex {mid} but its owner is {self.mutexes[mid].owner}")
        for q in self.queues.values():
            if q.policy == Policy.FIFO:
                continue
            for a, b in zip(q.waiters, q.waiters[1:]):
                pa, pb = self.threads[a[0]].prio, self.threads[b[0]].prio
                if pa < pb or (pa == pb and a[1] > b[1]):
                    raise ModelError(f"invariant 3: queue {q.qid} out of order: {q.waiters}")
        for s in self.sems.values():
            if s.initial + s.gives != s.count + s.handed + s.immediate:
                raise ModelError(f"unit conservation violated on semaphore {s.sid}: {s}")
        for m in self.mutexes.values():
            q = self.queues[m.qid]
            if (m.owner is None) != (m.count == 0):
                raise ModelError(f"mutex {m.mid}: owner {m.owner} count {m.count}")
            if m.owner is not None:
                o = self.threads[m.owner]
                if m.mid not in o.owned:
                    raise ModelError(f"mutex {m.mid} owner {m.owner} does not list it")
                if any(tid == m.owner for tid, _ in q.waiters):
                    raise ModelError(f"mutex {m.mid}: owner {m.owner} is in its own queue")
                if m.protocol == Protocol.INHERIT:
                    for tid, _ in q.waiters:
                        if o.prio < self.threads[tid].prio:
                            raise ModelError(f"mutex {m.mid}: owner {m.owner} prio {o.prio} below waiter {tid} prio {self.threads[tid].prio}")
                if m.protocol == Protocol.CEILING and o.prio < m.ceiling:
                    raise ModelError(f"mutex {m.mid}: owner {m.owner} below ceiling {m.ceiling}")
            elif q.waiters:
                raise ModelError(f"mutex {m.mid} is free but has waiters {q.waiters}")
        if self.current is not None and not self.runnable(self._current_thread()):
            raise ModelError(f"current thread {self.current} is not runnable")
        if self.irq_nesting != 0:
            raise ModelError("irq_nesting nonzero between steps")

    def check_terminal(self):
        for t in self.threads.values():
            if t.wait_state == WaitState.READY and not t.suspended and not t.terminated:
                raise ModelError(f"terminal state with runnable thread {t.tid} not dispatched")
            if t.wait_state == WaitState.BLOCKED and self.timeout_armed(t.tid):
                raise ModelError(f"terminal state with an armed timeout for {t.tid}")
            m = self._queue_mutex(t.wait_queue)
            if t.wait_state == WaitState.BLOCKED and m is not None and m.protocol == Protocol.INHERIT \
                    and self._chain_reaches(m.owner, t.tid):
                raise ModelError(f"terminal state with an undetected deadlock involving thread {t.tid}")

    # ------------------------------------------------------------------ enabled actions
    def enabled(self) -> list:
        acts = []
        if self.current is not None:
            acts.append(("run",))
        for i in range(len(self.isr_bodies)):
            if i not in self.isr_fired:
                acts.append(("isr", i))
        if self.timeouts:
            acts.append(("tick",))
        return acts

    def apply(self, act):
        if act[0] == "run":
            self.step_thread()
        elif act[0] == "isr":
            self.fire_isr(act[1])
        elif act[0] == "tick":
            self.tick()
        else:
            raise ModelError(f"unknown action {act}")
        self.check_invariants()

    def outcome(self) -> tuple:
        """Hashable summary of a terminal state."""
        return (("threads", tuple((tid, t.results) for tid, t in sorted(self.threads.items()))),
                ("sems", tuple((sid, s.count) for sid, s in sorted(self.sems.items()))),
                ("blocked", tuple(tid for tid, t in sorted(self.threads.items()) if t.wait_state == WaitState.BLOCKED)),
                ("mutex_owners", tuple((mid, m.owner) for mid, m in sorted(self.mutexes.items()))),
                ("notify_bits", tuple((tid, t.notify_bits) for tid, t in sorted(self.threads.items()))))
