"""Kernel state and operations of the reference model.

Specification references are given as SPEC-004 §n unless another document is named.
The model is deliberately literal: each `_take_*`, `_wake`, `_wake_timeout` function
is the pseudocode of SPEC-004 §5 with the sections made explicit through `pc`.
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


class Result(IntEnum):          # SPEC-004 §2.3 (INTERRUPTED and BUDGET are reserved)
    SATISFIED = 0
    TIMEOUT = 1
    CANCELED = 2
    DESTROYED = 3
    STALE = 4


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


FOREVER = None                  # EMB_WAIT_FOREVER
NO_WAIT = 0                     # EMB_NO_WAIT
CANCELABLE = frozenset({Reason.SLEEP, Reason.OBJECT, Reason.JOIN, Reason.NOTIFY})   # KRN-WAIT-015


class ModelError(AssertionError):
    """A checked-build fault or a violated invariant."""


# ----------------------------------------------------------------------------------------
# Static program descriptions. A thread's program is a tuple of these.
# ----------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Take:
    """emb_sem_take(sem, timeout): the canonical blocking primitive over the protocol."""
    sem: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class Give:
    """emb_sem_give(sem): thread or ISR context (@ctx thread isr)."""
    sem: int


@dataclass(frozen=True)
class Sleep:
    """emb_thread_sleep(ticks): the protocol on the thread's pseudo-queue, reason SLEEP."""
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
    """emb_sem_destroy on an object created with ABORT_WAITERS (ADR-020)."""
    sem: int


@dataclass(frozen=True)
class SchedLock:
    pass


@dataclass(frozen=True)
class SchedUnlock:
    pass


@dataclass(frozen=True)
class Exit:
    pass


ISR_SAFE_OPS = (Give, Resume)   # SPEC-001 §5.2: signal-type operations are `thread isr`


# ----------------------------------------------------------------------------------------
# Dynamic state
# ----------------------------------------------------------------------------------------

@dataclass
class Thread:
    tid: int
    prio: int
    wait_state: WaitState = WaitState.READY
    wait_gen: int = 0
    wait_reason: Optional[Reason] = None
    wait_queue: Optional[int] = None         # queue id, or None
    wake_result: Optional[Result] = None
    wake_data: int = 0
    suspended: bool = False
    cancel_pending: bool = False
    terminated: bool = False
    op_index: int = 0
    pc: int = 0                               # section counter inside the current op
    saved_gen: int = 0                        # generation captured in section 1
    results: tuple = ()                       # ((op_index, Result, data), ...)
    notify_bits: int = 0

    def key(self):
        return (self.tid, self.prio, int(self.wait_state), self.wait_gen,
                None if self.wait_reason is None else int(self.wait_reason), self.wait_queue,
                None if self.wake_result is None else int(self.wake_result), self.wake_data,
                self.suspended, self.cancel_pending, self.terminated, self.op_index, self.pc,
                self.saved_gen, self.results, self.notify_bits)


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
    initial: int = 0          # units present at creation
    destroyed: bool = False
    gen: int = 0
    gives: int = 0            # units given in total
    handed: int = 0           # units handed to a blocked waiter (§6.1)
    immediate: int = 0        # units taken in section 1 without blocking
    binding: Optional[tuple] = None   # (tid, bit) notification binding (ADR-027 hook)

    def key(self):
        return (self.sid, self.count, self.qid, self.initial, self.destroyed, self.gen, self.gives,
                self.handed, self.immediate, self.binding)


@dataclass
class Scenario:
    """Static description of a test configuration."""
    threads: dict                     # tid -> (prio, program tuple)
    sems: dict = field(default_factory=dict)   # sid -> dict(count=, policy=, binding=)
    isrs: tuple = ()                  # tuple of op tuples; each ISR fires at most once
    name: str = ""


def build(sc: Scenario) -> "Kernel":
    k = Kernel(programs={tid: tuple(p) for tid, (_, p) in sc.threads.items()}, isr_bodies=tuple(sc.isrs))
    for tid, (prio, _) in sc.threads.items():
        k.threads[tid] = Thread(tid=tid, prio=prio)
        k.queues[-(tid + 1)] = WaitQueue(qid=-(tid + 1), policy=Policy.FIFO)   # pseudo-queue for SLEEP
    for sid, cfg in sc.sems.items():
        pol = cfg.get("policy", Policy.PRIORITY_FIFO)
        k.queues[sid] = WaitQueue(qid=sid, policy=pol)
        k.sems[sid] = Semaphore(sid=sid, count=cfg.get("count", 0), initial=cfg.get("count", 0), qid=sid,
                                binding=cfg.get("binding"))
    if any(q.policy == Policy.BITMAP for q in k.queues.values()):
        prios = [t.prio for t in k.threads.values()]
        if len(set(prios)) != len(prios):
            raise ModelError("BITMAP wait queues require unique priorities (ADR-036)")
    for t in k.threads.values():
        k._make_ready(t, preempted=False)
    k.reschedule_pending = False
    k.dispatch()                                   # P3: kernel start
    return k


@dataclass
class Kernel:
    programs: dict
    isr_bodies: tuple = ()
    threads: dict = field(default_factory=dict)
    queues: dict = field(default_factory=dict)
    sems: dict = field(default_factory=dict)
    ready: dict = field(default_factory=dict)      # prio -> [tid...], head first
    current: Optional[int] = None
    now: int = 0
    irq_nesting: int = 0                           # SPEC-002 §2 irq_nesting_depth
    sched_lock_depth: int = 0
    reschedule_pending: bool = False
    timeouts: list = field(default_factory=list)   # [(deadline, seq, tid, gen)] sorted
    seq: int = 0
    dequeued: frozenset = frozenset()              # {(tid, gen)}: at most one dequeue per generation
    stale_timeouts: int = 0
    isr_fired: frozenset = frozenset()
    trace: list = field(default_factory=list)      # not part of the state key

    # ------------------------------------------------------------------ state identity
    def key(self):
        return (tuple(t.key() for _, t in sorted(self.threads.items())),
                tuple(q.key() for _, q in sorted(self.queues.items())),
                tuple(s.key() for _, s in sorted(self.sems.items())),
                tuple((p, tuple(v)) for p, v in sorted(self.ready.items()) if v),
                self.current, self.now, self.irq_nesting, self.sched_lock_depth,
                self.reschedule_pending, tuple(self.timeouts), self.dequeued,
                self.stale_timeouts, self.isr_fired)

    def clone(self) -> "Kernel":
        k = copy.copy(self)
        k.threads = {i: copy.copy(t) for i, t in self.threads.items()}
        k.queues = {i: WaitQueue(q.qid, q.policy, list(q.waiters)) for i, q in self.queues.items()}
        k.sems = {i: copy.copy(s) for i, s in self.sems.items()}
        k.ready = {p: list(v) for p, v in self.ready.items()}
        k.timeouts = list(self.timeouts)
        k.trace = []
        return k

    def _next_seq(self) -> int:
        self.seq += 1
        return self.seq

    def _event(self, *ev):
        self.trace.append(ev)

    # ------------------------------------------------------------------ helpers
    def runnable(self, t: Thread) -> bool:
        return t.wait_state != WaitState.BLOCKED and not t.suspended and not t.terminated

    def in_ready(self, tid: int) -> bool:
        return any(tid in v for v in self.ready.values())

    def _make_ready(self, t: Thread, preempted: bool):
        """Insert into the ready structure. A preempted thread goes ahead of its peers
        (KRN-SCH-041); a woken, yielding, or newly started thread goes behind them."""
        lst = self.ready.setdefault(t.prio, [])
        if preempted:
            lst.insert(0, t.tid)
        else:
            lst.append(t.tid)
        cur = self.threads.get(self.current) if self.current is not None else None
        if cur is None or t.prio > cur.prio:
            self.reschedule_pending = True      # SPEC-002 §6.2 step 2

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
        """Select the next thread (P1, P2, P3 of SPEC-002 §6). The current thread, when
        still runnable and preempted, is placed ahead of its equal-priority peers."""
        cur = self.threads.get(self.current) if self.current is not None else None
        best_tid = self._best_ready()
        if cur is not None and self.runnable(cur):
            if best_tid is None or self.threads[best_tid].prio <= cur.prio:
                self.reschedule_pending = False
                return
            self._make_ready(cur, preempted=True)
        if best_tid is None:
            self.current = None             # idle
        else:
            self.ready[self.threads[best_tid].prio].pop(0)
            self.current = best_tid
        self.reschedule_pending = False
        self._event("switch", self.current)

    def reschedule_if_needed(self):          # P2, SPEC-002 §6.3
        if self.reschedule_pending and self.sched_lock_depth == 0:
            self.dispatch()

    # ------------------------------------------------------------------ wait queues
    def _enqueue(self, q: WaitQueue, t: Thread):
        s = self._next_seq()
        if q.policy == Policy.FIFO:
            q.waiters.append((t.tid, s))
            return
        # PRIORITY_FIFO and BITMAP: highest effective priority first, FIFO among equals.
        # Insert from the tail: a new waiter is most often not higher than the existing ones.
        i = len(q.waiters)
        while i > 0 and self.threads[q.waiters[i - 1][0]].prio < t.prio:
            i -= 1
        q.waiters.insert(i, (t.tid, s))

    def _dequeue(self, q: WaitQueue, t: Thread):
        for i, (tid, _) in enumerate(q.waiters):
            if tid == t.tid:
                del q.waiters[i]
                return
        raise ModelError(f"thread {t.tid} not in queue {q.qid}")

    def wait_first(self, q: WaitQueue) -> Optional[Thread]:
        return self.threads[q.waiters[0][0]] if q.waiters else None

    def wait_requeue(self, t: Thread):                                   # §6.4
        if t.wait_queue is None:
            return
        q = self.queues[t.wait_queue]
        if q.policy == Policy.FIFO:
            return
        self._dequeue(q, t)
        # keep the original sequence so FIFO-among-equals is preserved for a re-sorted thread
        s = self._next_seq()
        i = len(q.waiters)
        while i > 0 and self.threads[q.waiters[i - 1][0]].prio < t.prio:
            i -= 1
        q.waiters.insert(i, (t.tid, s))
        self._event("wait_requeue", t.tid, t.prio)

    # ------------------------------------------------------------------ timeouts (SPEC-003 §5)
    def timeout_arm(self, t: Thread, deadline: int, gen: int):
        for d, _, tid, _ in self.timeouts:
            if tid == t.tid:
                raise ModelError(f"thread {t.tid} already has an armed timeout (SPEC-004 §16.9)")
        self.timeouts.append((deadline, self._next_seq(), t.tid, gen))
        self.timeouts.sort()

    def timeout_disarm(self, t: Thread):
        self.timeouts = [n for n in self.timeouts if n[2] != t.tid]

    def timeout_armed(self, tid: int) -> bool:
        return any(n[2] == tid for n in self.timeouts)

    # ------------------------------------------------------------------ wake (§5.2)
    def wake(self, t: Thread, result: Result, data: int = 0, q: Optional[WaitQueue] = None) -> bool:
        """Called with the object lock held (one section). Returns False when `t` no
        longer waits on `q`."""
        if q is None or t.wait_queue != q.qid:
            return False
        self._dequeue(q, t)
        t.wait_queue = None
        t.wake_result = result                      # result before READY (KRN-WAIT-005)
        t.wake_data = data
        pair = (t.tid, t.wait_gen)
        if pair in self.dequeued:
            raise ModelError(f"second dequeue for {pair} (KRN-WAIT-004)")
        self.dequeued = self.dequeued | {pair}
        t.wait_gen += 1
        if t.wait_state == WaitState.INTEND_TO_BLOCK:
            t.wait_state = WaitState.READY          # the thread is still running; it sees READY at commit
            self._event("wake", t.tid, int(result), "intend")
            return True
        if t.wait_state != WaitState.BLOCKED:
            raise ModelError(f"wake of thread {t.tid} in state {t.wait_state}")
        t.wait_state = WaitState.READY
        self.timeout_disarm(t)
        if not t.suspended:
            self._make_ready(t, preempted=False)
        self._event("wake", t.tid, int(result), "blocked")
        return True

    def wake_timeout(self, tid: int, gen: int):                          # §5.3
        t = self.threads[tid]
        if t.wait_gen != gen or t.wait_queue is None:
            self.stale_timeouts += 1
            self._event("stale_timeout", tid, gen)
            return
        self.wake(t, Result.TIMEOUT, 0, self.queues[t.wait_queue])

    def wake_all(self, q: WaitQueue, result: Result, data: int = 0) -> int:   # §6.7, KRN-WAIT-013
        n = 0
        while q.waiters:
            self.wake(self.threads[q.waiters[0][0]], result, data, q)
            n += 1
        return n

    # ------------------------------------------------------------------ blocking (§5.1)
    def _current_thread(self) -> Thread:
        return self.threads[self.current]

    def _finish(self, t: Thread, result: Result, data: int = 0):
        t.results = t.results + ((t.op_index, result, data),)
        t.op_index += 1
        t.pc = 0
        t.wait_reason = None
        t.wake_result = None
        t.wake_data = 0
        self._event("wait_end", t.tid, int(result))

    def _advance(self, t: Thread):
        t.op_index += 1
        t.pc = 0

    def _block_section(self, t: Thread, q: WaitQueue, reason: Reason, timeout: Optional[int],
                       satisfied, consume) -> bool:
        """Sections 1 to 3 of SPEC-004 §5.1 driven by `t.pc`. Returns True when the op is
        complete (result recorded) and False when the thread yielded between sections or
        blocked."""
        if t.pc == 0:                                                   # section 1
            if t.cancel_pending:                                        # KRN-WAIT-015
                t.cancel_pending = False
                self._finish(t, Result.CANCELED)
                return True
            if satisfied():
                data = consume()
                self._finish(t, Result.SATISFIED, data)
                return True
            if timeout == NO_WAIT:                                      # API-024: no queue, no timer, no yield
                self._finish(t, Result.TIMEOUT)
                return True
            if self.irq_nesting > 0 or self.sched_lock_depth > 0:
                raise ModelError("blocking with a nonzero timeout from ISR or under the scheduler lock (SPEC-001 §5.3)")
            if t.wait_state != WaitState.READY:
                raise ModelError(f"block while wait_state is {t.wait_state} (invariant)")
            t.saved_gen = t.wait_gen
            t.wait_reason = reason
            t.wait_queue = q.qid
            self._enqueue(q, t)
            t.wait_state = WaitState.INTEND_TO_BLOCK
            t.pc = 1
            self._event("wait_begin", t.tid, q.qid, int(reason))
            return False                                                # window
        if t.pc == 1:                                                   # section 2
            if timeout is not FOREVER:
                self.timeout_arm(t, self.now + timeout, t.saved_gen)
            t.pc = 2
            return False
        if t.pc == 2:                                                   # section 3
            if t.wait_state == WaitState.INTEND_TO_BLOCK:
                t.wait_state = WaitState.BLOCKED
                t.pc = 3
                self.dispatch()                                         # P2: switch away
                return False
            # a waker won in the window
            self.timeout_disarm(t)
            t.pc = 3
            # fall through: the result is available now
        # pc == 3: after the wake (or immediately when the waker won in the window)
        if t.wait_state != WaitState.READY or t.wake_result is None:
            raise ModelError(f"thread {t.tid} resumed without a result")
        self._finish(t, t.wake_result, t.wake_data)
        return True

    # ------------------------------------------------------------------ operations
    def _op_take(self, t: Thread, op: Take) -> bool:
        sem = self.sems[op.sem]
        if sem.destroyed and t.pc == 0:                                  # new take on a destroyed object: stale handle
            self._finish(t, Result.STALE)
            return True
        q = self.queues[sem.qid]

        def satisfied():
            return sem.count > 0

        def consume():
            sem.count -= 1
            sem.immediate += 1
            return 1
        return self._block_section(t, q, Reason.OBJECT, op.timeout, satisfied, consume)

    def _op_sleep(self, t: Thread, op: Sleep) -> bool:
        if op.ticks == 0 and t.pc == 0:                                 # SPEC-003 §15.1: sleep(0) is yield
            self._op_yield(t)
            return True
        q = self.queues[-(t.tid + 1)]
        return self._block_section(t, q, Reason.SLEEP, op.ticks, lambda: False, lambda: 0)

    def _op_give(self, t_or_none: Optional[Thread], op: Give):
        sem = self.sems[op.sem]
        if sem.destroyed:
            return
        q = self.queues[sem.qid]
        sem.gives += 1
        w = self.wait_first(q)
        if w is not None:
            self.wake(w, Result.SATISFIED, 1, q)                        # hand-off, count unchanged (§6.1)
            sem.handed += 1
            return                                                      # transition consumed: no binding bit
        sem.count += 1
        if sem.binding is not None and sem.count == 1:                  # ready transition (§6.3)
            tid, bit = sem.binding
            self.threads[tid].notify_bits |= (1 << bit)

    def _op_yield(self, t: Thread):
        self._make_ready(t, preempted=False)
        self.current = None
        self.dispatch()
        self._advance(t)

    def _op_setprio(self, op: SetPrio):
        t = self.threads[op.tid]
        if self.in_ready(t.tid):
            self._remove_ready(t.tid)
            t.prio = op.prio
            self._make_ready(t, preempted=False)
        else:
            t.prio = op.prio
        self.wait_requeue(t)                                            # KRN-WAIT-003
        best = self._best_ready()
        cur = self.threads.get(self.current) if self.current is not None else None
        if cur is not None and best is not None and self.threads[best].prio > cur.prio:
            self.reschedule_pending = True

    def _op_suspend(self, op: Suspend):
        t = self.threads[op.tid]
        if t.terminated or t.suspended:
            return
        t.suspended = True                                              # overlay (§6.5)
        if self.in_ready(t.tid):
            self._remove_ready(t.tid)
        if self.current == t.tid:
            self.reschedule_pending = True

    def _op_resume(self, op: Resume):
        t = self.threads[op.tid]
        if not t.suspended:
            return
        t.suspended = False
        # A thread suspended while in INTEND_TO_BLOCK (preempted inside the window) is still
        # executing its block path and is runnable; only BLOCKED threads stay off the ready structure.
        if t.wait_state != WaitState.BLOCKED and not t.terminated and self.current != t.tid:
            self._make_ready(t, preempted=False)

    def _op_cancel(self, op: Cancel):                                    # §6.6
        t = self.threads[op.tid]
        if t.terminated:
            return
        if t.wait_state != WaitState.READY and t.wait_reason in CANCELABLE and t.wait_queue is not None:
            self.wake(t, Result.CANCELED, 0, self.queues[t.wait_queue])
            return
        t.cancel_pending = True

    def _op_destroy(self, op: Destroy):                                  # §6.7
        sem = self.sems[op.sem]
        if sem.destroyed:
            return
        q = self.queues[sem.qid]
        self.wake_all(q, Result.DESTROYED)
        sem.destroyed = True
        sem.gen += 1

    def step_thread(self) -> bool:
        """Run the current thread for one section. Returns False when idle."""
        if self.current is None:
            return False
        t = self._current_thread()
        prog = self.programs[t.tid]
        op = prog[t.op_index] if t.op_index < len(prog) else Exit()
        if isinstance(op, Take):
            self._op_take(t, op)
        elif isinstance(op, Sleep):
            self._op_sleep(t, op)
        elif isinstance(op, Give):
            self._op_give(t, op)
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
        elif isinstance(op, Exit):
            if self.sched_lock_depth:
                raise ModelError("thread exit with the scheduler locked")
            t.terminated = True
            self.current = None
            self.dispatch()
        else:
            raise ModelError(f"unknown op {op!r}")
        return True

    # ------------------------------------------------------------------ interrupts and time
    def fire_isr(self, index: int):
        """Run ISR body `index` atomically (kernel-aware interrupt, SPEC-002 §3), then P1."""
        if index in self.isr_fired:
            raise ModelError("ISR already fired")
        self.isr_fired = self.isr_fired | {index}
        self.irq_nesting += 1
        for op in self.isr_bodies[index]:
            if not isinstance(op, ISR_SAFE_OPS):
                raise ModelError(f"{op!r} is not ISR-safe (SPEC-001 §5.2)")
            if isinstance(op, Give):
                self._op_give(None, op)
            elif isinstance(op, Resume):
                self._op_resume(op)
        self.irq_nesting -= 1
        self._p1()

    def tick(self):
        """Advance the clock by one tick and expire due timeouts (SPEC-003 §5.3), then P1."""
        self.now += 1
        self.irq_nesting += 1
        while self.timeouts and self.timeouts[0][0] <= self.now:
            _, _, tid, gen = self.timeouts.pop(0)
            self.wake_timeout(tid, gen)
        self.irq_nesting -= 1
        self._p1()

    def _p1(self):                                                       # SPEC-002 §6.1 P1
        if self.irq_nesting == 0 and self.reschedule_pending and self.sched_lock_depth == 0:
            self.dispatch()

    # ------------------------------------------------------------------ invariants (§5.4)
    def check_invariants(self):
        for t in self.threads.values():
            membership = [q.qid for q in self.queues.values() if any(tid == t.tid for tid, _ in q.waiters)]
            if t.wait_state != WaitState.READY:
                if t.wait_queue is None or membership != [t.wait_queue]:
                    raise ModelError(f"invariant 1: thread {t.tid} state {t.wait_state} queues {membership}")
            else:
                if membership or t.wait_queue is not None:
                    raise ModelError(f"invariant 1: READY thread {t.tid} still in {membership}")
            ready_count = sum(v.count(t.tid) for v in self.ready.values())
            if ready_count > 1:
                raise ModelError(f"thread {t.tid} twice in the ready structure")
            should_be_ready = self.runnable(t) and self.current != t.tid
            if should_be_ready != (ready_count == 1):
                raise ModelError(f"invariant 5: thread {t.tid} ready={ready_count} runnable={self.runnable(t)} current={self.current}")
            if self.timeout_armed(t.tid) and t.wait_state == WaitState.READY and t.pc != 2:
                raise ModelError(f"thread {t.tid} has an armed timeout while READY outside the window")
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
        if self.current is not None and not self.runnable(self._current_thread()):
            raise ModelError(f"current thread {self.current} is not runnable")
        if self.irq_nesting != 0:
            raise ModelError("irq_nesting nonzero between steps")

    def check_terminal(self):
        """Properties that must hold when no action is enabled."""
        for t in self.threads.values():
            if t.wait_state == WaitState.READY and not t.suspended and not t.terminated:
                raise ModelError(f"terminal state with runnable thread {t.tid} not dispatched")
            if t.wait_state == WaitState.BLOCKED and self.timeout_armed(t.tid):
                raise ModelError(f"terminal state with an armed timeout for {t.tid}")

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
        """Hashable summary of a terminal state: every thread's results and every semaphore's count."""
        return (("threads", tuple((tid, t.results) for tid, t in sorted(self.threads.items()))),
                ("sems", tuple((sid, s.count) for sid, s in sorted(self.sems.items()))))
