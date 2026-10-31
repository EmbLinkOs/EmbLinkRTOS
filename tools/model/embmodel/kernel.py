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
    BUSY = 12            # EMB_EBUSY from a non-wait operation (destroying a bound or live thread)
    NOENT = 13           # EMB_ENOENT: unbind with no binding
    EXIST = 14           # EMB_EEXIST: a second binding
    STATE = 15           # EMB_ESTATE: binding a terminated thread


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
EVENT_QID_BASE = 200            # event flags 200..
COND_QID_BASE = 300             # condition variables 300..
MSGQ_QID_BASE = 400             # message queues: receivers 400 + 2i, senders 401 + 2i
WORK_QID_BASE = 500             # work queues: the completion queue of queue q is 500 + q
K_WORK = 1 << 31                # EMB_NOTIFY_K_WORK with 32-bit notification words (SPEC-006 §2.1)
TIMER_WORK_BASE = 1000          # the work item embedded in timer t is item 1000 + t
DWORK_TIMER_BASE = 2000         # the internal timer of delayed item w is timer 2000 + w
WORK_IDLE, WORK_PENDING, WORK_RUNNING = 0, 1, 2   # emb_work_state_t; PENDING is "queued"


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


@dataclass(frozen=True)
class BindNotify:
    """emb_sem_bind_notify(sem, tid, 1 << bit) from thread context (SPEC-006 §3, ADR-046)."""
    sem: int
    tid: int
    bit: int


@dataclass(frozen=True)
class UnbindNotify:
    """emb_sem_unbind_notify(sem): later ready transitions set no bit (KRN-NOTIF-013)."""
    sem: int


@dataclass(frozen=True)
class ThreadDestroy:
    """emb_thread_destroy(tid): INACTIVE, or TERMINATED and collected, and no binding names it."""
    tid: int


NONWAIT_RESULT_OPS = (BindNotify, UnbindNotify, ThreadDestroy)   # record a result although they never wait


@dataclass(frozen=True)
class EventSet:
    """emb_event_set(event, bits): thread or ISR context (SPEC-005 §5.2)."""
    event: int
    bits: int


@dataclass(frozen=True)
class EventClear:
    """emb_event_clear(event, bits): never wakes anyone."""
    event: int
    bits: int


@dataclass(frozen=True)
class EventWait:
    """emb_event_wait(event, mask, mode, timeout): ANY unless all=True; clear consumes the satisfying bits."""
    event: int
    mask: int
    all: bool = False
    clear: bool = False
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class EventDestroy:
    """emb_event_destroy(event) with EMB_OBJ_ABORT_WAITERS: waiters leave with DESTROYED."""
    event: int


@dataclass(frozen=True)
class CondWait:
    """emb_cond_wait(cond, mutex, timeout): release the mutex with the enqueue, wait, relock (SPEC-005 §6)."""
    cond: int
    mutex: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class CondSignal:
    cond: int


@dataclass(frozen=True)
class CondBroadcast:
    cond: int


@dataclass(frozen=True)
class CondDestroy:
    """emb_cond_destroy(cond) with EMB_OBJ_ABORT_WAITERS: waiters leave with DESTROYED, after relocking."""
    cond: int


@dataclass(frozen=True)
class MsgSend:
    """emb_msgq_send(q, &value, timeout): to a waiting receiver, else the tail, else wait for a slot (SPEC-007 §2)."""
    q: int
    value: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class MsgSendFront:
    """emb_msgq_send_front(q, &value): never waits; to a waiting receiver, else the head, else TIMEOUT."""
    q: int
    value: int


@dataclass(frozen=True)
class MsgRecv:
    """emb_msgq_receive(q, &out, timeout): the head, refilled from a waiting sender, else wait."""
    q: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class MsgPeek:
    """emb_msgq_peek(q, &out): a copy of the head without removing it; TIMEOUT when empty."""
    q: int


@dataclass(frozen=True)
class MsgDestroy:
    """emb_msgq_destroy(q) with EMB_OBJ_ABORT_WAITERS: waiters leave with DESTROYED, items are dropped."""
    q: int


@dataclass(frozen=True)
class Submit:
    """emb_work_submit(q, w): queue an idle item, mark a running one for resubmission (SPEC-006 §4.3)."""
    q: int
    w: int


@dataclass(frozen=True)
class WorkCancel:
    """emb_work_cancel(w): unlink a queued item; the result's data is the state it was in."""
    w: int


@dataclass(frozen=True)
class WorkCancelSync:
    """emb_work_cancel_sync(w, timeout): cancel, then wait until a running handler returns."""
    w: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class WorkFlush:
    """emb_workq_flush(q, timeout): wait until every item queued before the call has run."""
    q: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class WorkqDestroy:
    """emb_workq_destroy(q): stop an idle queue and wait, not cancelably, for its thread to exit."""
    q: int


@dataclass(frozen=True)
class WorkPop:
    """The queue thread's section that takes the next FIFO entry (internal)."""
    q: int


@dataclass(frozen=True)
class WorkDone:
    """The queue thread's section after a handler returns (internal)."""
    q: int


@dataclass(frozen=True)
class TimerStart:
    """emb_timer_start(t, first), or emb_timer_start_periodic(t, first, period) when period > 0."""
    timer: int
    first: int
    period: int = 0


@dataclass(frozen=True)
class TimerStop:
    """emb_timer_stop(t): disarm and drop a queued callback; the result's data is the state found."""
    timer: int


@dataclass(frozen=True)
class TimerStopSync:
    """emb_timer_stop_sync(t, timeout): stop in one section, then wait for a running callback."""
    timer: int
    timeout: Optional[int] = FOREVER


@dataclass(frozen=True)
class DworkSchedule:
    """emb_dwork_schedule(q, w, delay), or emb_dwork_reschedule() when replace is set."""
    q: int
    w: int
    delay: int
    replace: bool = False


@dataclass(frozen=True)
class DworkCancel:
    """emb_dwork_cancel(w): stop the delay or unlink the queued item; data is the state found."""
    w: int


def event_satisfying(bits: int, mask: int, all_: bool) -> int:
    got = bits & mask
    if all_:
        return got if got == mask else 0
    return got


NONWAIT_RESULT_OPS = NONWAIT_RESULT_OPS + (Submit, TimerStart, DworkSchedule)
NONWAIT_DATA_OPS = (MsgSendFront, MsgPeek)                        # record a result and data, never wait
ISR_SAFE_OPS = (Give, Resume, NotifySet, Start, EventSet, EventClear,  # SPEC-001 §5.2: signal-type operations are `thread isr`
                MsgSendFront, MsgPeek, MsgSend, MsgRecv, Submit, WorkCancel, TimerStart, TimerStop,
                DworkSchedule, DworkCancel)
ISR_NO_WAIT_ONLY = (MsgSend, MsgRecv)                             # with EMB_NO_WAIT only (SPEC-007 §1)


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
    collected: bool = False                   # the exit code was joined: destroy is allowed (SPEC-008 §8)
    destroyed: bool = False                   # emb_thread_destroy succeeded; no handle names it any more
    cond_phase: int = 0                       # CondWait: 0 waiting, 1 relocking, 2 relocked (reporting)
    cond_result: Optional[Result] = None      # the condition wait's result, then the final one
    cond_data: int = 0                        # the final result's data (OWNERDEAD_FLAG)
    msg_value: int = 0                        # a blocked sender's item (the kernel keeps its pointer)
    wq_item: Optional[int] = None             # a queue thread: the item whose handler it runs
    work_target: Optional[tuple] = None       # a completion wait: ("w", item) or ("m", flusher tid)
    stop_phase: int = 0                       # TimerStopSync: 1 once the stop section ran
    cancel_deferred: bool = False             # cancellation is not delivered (the relock, A41)

    def key(self):
        return (self.tid, self.prio, self.base_prio, int(self.wait_state), self.wait_gen,
                None if self.wait_reason is None else int(self.wait_reason), self.wait_queue,
                None if self.wake_result is None else int(self.wake_result), self.wake_data,
                self.suspended, self.cancel_pending, self.terminated, self.op_index, self.pc,
                self.saved_gen, self.deadline, self.results, self.notify_bits, self.notify_mask, self.notify_all,
                self.notify_clear, tuple(self.owned), self.inactive, self.exit_code, self.joiner,
                self.collected, self.destroyed, self.cond_phase,
                None if self.cond_result is None else int(self.cond_result), self.cond_data,
                self.cancel_deferred, self.msg_value, self.wq_item, self.work_target, self.stop_phase)


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
class Event:
    eid: int
    bits: int
    qid: int
    destroyed: bool = False
    binding: Optional[tuple] = None

    def key(self):
        return (self.eid, self.bits, self.qid, self.destroyed, self.binding)


@dataclass
class Cond:
    cid: int
    qid: int
    destroyed: bool = False

    def key(self):
        return (self.cid, self.qid, self.destroyed)


@dataclass
class Msgq:
    qid: int
    capacity: int
    recv_qid: int
    send_qid: int
    ring: tuple = ()
    destroyed: bool = False
    binding: Optional[tuple] = None
    accepted: int = 0                         # items the queue took: handed to a receiver or stored
    received: int = 0                         # items delivered to receivers
    dropped: int = 0                          # items discarded by destroy

    def key(self):
        return (self.qid, self.capacity, self.ring, self.destroyed, self.binding, self.accepted, self.received,
                self.dropped)


@dataclass
class Work:
    wid: int
    state: int = WORK_IDLE
    queue: Optional[int] = None               # the queue it is queued or running on
    resubmitted: bool = False
    runs: int = 0                             # handlers that returned

    def key(self):
        return (self.wid, self.state, self.queue, self.resubmitted, self.runs)


@dataclass
class Timer:
    tmid: int
    isr: bool                                 # EMB_TIMER_ISR_CONTEXT, else its work item runs it on queue q
    q: Optional[int] = None
    deadline: Optional[int] = None            # armed when set
    seq: int = 0                              # arming order among equal deadlines
    period: int = 0
    origin: int = 0                           # the first deadline of a periodic start: deadlines stay on its grid
    expiries: int = 0
    overruns: int = 0
    armed_in_pass: bool = False               # armed by this expiry pass: the next pass expires it
    in_callback: bool = False                 # an ISR_CONTEXT callback runs
    dwork: Optional[int] = None               # a delayed item's internal timer: the item it submits
    target: Optional[int] = None              # and the queue, while the delay runs

    def key(self):
        return (self.tmid, self.deadline, self.seq, self.period, self.origin, self.expiries, self.overruns,
                self.armed_in_pass, self.in_callback, self.target)


@dataclass
class Workq:
    q: int
    tid: int                                  # the queue's thread
    done_qid: int
    fifo: tuple = ()                          # ("w", item) and ("m", flusher tid) entries, head first
    running: Optional[int] = None
    stopping: bool = False
    destroyed: bool = False

    def key(self):
        return (self.q, self.tid, self.fifo, self.running, self.stopping, self.destroyed)


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
    events: dict = field(default_factory=dict)   # eid -> dict(bits=, policy=, binding=)
    conds: dict = field(default_factory=dict)    # cid -> dict(policy=)
    msgqs: dict = field(default_factory=dict)    # qid -> dict(capacity=, policy=, binding=)
    workqs: dict = field(default_factory=dict)   # q -> dict(tid=, prio=): the queue thread
    works: dict = field(default_factory=dict)    # w -> dict(body=ops the handler runs)
    timers: dict = field(default_factory=dict)   # t -> dict(isr=bool, q=queue, body=callback ops)
    horizon: Optional[int] = None              # no tick past it while a periodic timer is armed
    isrs: tuple = ()
    owner_death: str = "release"               # CONFIG_EMB_MUTEX_OWNER_DEATH: "release" | "fault"
    inactive: tuple = ()                       # tids created INACTIVE (must be started by another thread or an ISR)
    name: str = ""
    tickless: bool = True                      # already-due compares request a timer interrupt


def build(sc: Scenario) -> "Kernel":
    k = Kernel(programs={tid: tuple(p) for tid, (_, p) in sc.threads.items()}, isr_bodies=tuple(sc.isrs),
               owner_death=sc.owner_death, tickless=sc.tickless, horizon=sc.horizon)
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
    for q, cfg in sc.workqs.items():
        tid = cfg["tid"]
        if tid in sc.threads:
            raise ModelError(f"work queue {q}: thread id {tid} is taken")
        k.threads[tid] = Thread(tid=tid, prio=cfg["prio"], base_prio=cfg["prio"])
        k.queues[-(tid + 1)] = WaitQueue(qid=-(tid + 1), policy=Policy.FIFO)
        k.queues[-(1000 + tid)] = WaitQueue(qid=-(1000 + tid), policy=Policy.FIFO)
        k.queues[WORK_QID_BASE + q] = WaitQueue(qid=WORK_QID_BASE + q, policy=Policy.PRIORITY_FIFO)
        k.workqs[q] = Workq(q=q, tid=tid, done_qid=WORK_QID_BASE + q)
        k.wq_of_tid[tid] = q
        k.programs[tid] = ()
    for w, cfg in sc.works.items():
        k.works[w] = Work(wid=w)
        k.work_bodies[w] = tuple(cfg.get("body", ()))
        if cfg.get("delayed"):                                        # emb_dwork_t: an internal timer
            k.timers[DWORK_TIMER_BASE + w] = Timer(tmid=DWORK_TIMER_BASE + w, isr=True, dwork=w)
            k.timer_bodies[DWORK_TIMER_BASE + w] = ()
    for tm, cfg in sc.timers.items():
        isr = bool(cfg.get("isr", False))
        k.timers[tm] = Timer(tmid=tm, isr=isr, q=None if isr else cfg["q"])
        k.timer_bodies[tm] = tuple(cfg.get("body", ()))
        if not isr:                                                   # the embedded work item
            k.works[TIMER_WORK_BASE + tm] = Work(wid=TIMER_WORK_BASE + tm)
            k.work_bodies[TIMER_WORK_BASE + tm] = k.timer_bodies[tm]
    for mq, cfg in sc.msgqs.items():
        rq, wq = MSGQ_QID_BASE + 2 * mq, MSGQ_QID_BASE + 2 * mq + 1
        k.queues[rq] = WaitQueue(qid=rq, policy=cfg.get("policy", Policy.PRIORITY_FIFO))
        k.queues[wq] = WaitQueue(qid=wq, policy=cfg.get("policy", Policy.PRIORITY_FIFO))
        k.msgqs[mq] = Msgq(qid=mq, capacity=cfg.get("capacity", 1), recv_qid=rq, send_qid=wq,
                           binding=cfg.get("binding"))
    for cid, cfg in sc.conds.items():
        qid = COND_QID_BASE + cid
        k.queues[qid] = WaitQueue(qid=qid, policy=cfg.get("policy", Policy.PRIORITY_FIFO))
        k.conds[cid] = Cond(cid=cid, qid=qid)
    for eid, cfg in sc.events.items():
        qid = EVENT_QID_BASE + eid
        k.queues[qid] = WaitQueue(qid=qid, policy=cfg.get("policy", Policy.PRIORITY_FIFO))
        k.events[eid] = Event(eid=eid, bits=cfg.get("bits", 0), qid=qid, binding=cfg.get("binding"))
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
    tickless: bool = True
    threads: dict = field(default_factory=dict)
    queues: dict = field(default_factory=dict)
    sems: dict = field(default_factory=dict)
    mutexes: dict = field(default_factory=dict)
    events: dict = field(default_factory=dict)
    conds: dict = field(default_factory=dict)
    msgqs: dict = field(default_factory=dict)
    workqs: dict = field(default_factory=dict)
    works: dict = field(default_factory=dict)
    wq_of_tid: dict = field(default_factory=dict)   # static: queue thread tid -> queue
    work_bodies: dict = field(default_factory=dict)  # static: item -> handler ops
    timers: dict = field(default_factory=dict)
    timer_bodies: dict = field(default_factory=dict)  # static: timer -> callback ops
    qid_mutex: dict = field(default_factory=dict)   # static map, not part of the key
    ready: dict = field(default_factory=dict)
    current: Optional[int] = None
    now: int = 0
    irq_nesting: int = 0
    sched_lock_depth: int = 0
    reschedule_pending: bool = False
    timeouts: list = field(default_factory=list)
    timer_pending: bool = False
    seq: int = 0
    dequeued: frozenset = frozenset()
    stale_timeouts: int = 0
    isr_fired: frozenset = frozenset()
    pi_depth_max: int = 0
    trace: list = field(default_factory=list)
    in_expiry: bool = False                         # transient: inside a timer's expiry, not part of the key
    horizon: Optional[int] = None                   # static: the scenario's tick bound for periodic timers

    # ------------------------------------------------------------------ state identity
    def key(self):
        return (tuple(t.key() for _, t in sorted(self.threads.items())),
                tuple(q.key() for _, q in sorted(self.queues.items())),
                tuple(s.key() for _, s in sorted(self.sems.items())),
                tuple(m.key() for _, m in sorted(self.mutexes.items())),
                tuple(e.key() for _, e in sorted(self.events.items())),
                tuple(c.key() for _, c in sorted(self.conds.items())),
                tuple(q.key() for _, q in sorted(self.msgqs.items())),
                tuple(q.key() for _, q in sorted(self.workqs.items())),
                tuple(w.key() for _, w in sorted(self.works.items())),
                tuple(t.key() for _, t in sorted(self.timers.items())),
                tuple((p, tuple(v)) for p, v in sorted(self.ready.items()) if v),
                self.current, self.now, self.irq_nesting, self.sched_lock_depth,
                self.reschedule_pending, tuple(self.timeouts), self.timer_pending, self.dequeued,
                self.stale_timeouts, self.isr_fired, self.pi_depth_max)

    def clone(self) -> "Kernel":
        k = copy.copy(self)
        k.threads = {i: copy.copy(t) for i, t in self.threads.items()}
        for t in k.threads.values():
            t.owned = list(t.owned)
        k.queues = {i: WaitQueue(q.qid, q.policy, list(q.waiters)) for i, q in self.queues.items()}
        k.sems = {i: copy.copy(s) for i, s in self.sems.items()}
        k.mutexes = {i: copy.copy(m) for i, m in self.mutexes.items()}
        k.events = {i: copy.copy(e) for i, e in self.events.items()}
        k.conds = {i: copy.copy(c) for i, c in self.conds.items()}
        k.msgqs = {i: copy.copy(q) for i, q in self.msgqs.items()}
        k.workqs = {i: copy.copy(q) for i, q in self.workqs.items()}
        k.works = {i: copy.copy(w) for i, w in self.works.items()}
        k.timers = {i: copy.copy(t) for i, t in self.timers.items()}
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

    def _program(self, t: Thread) -> tuple:
        """A queue thread's program follows its state: the drain loop, or a handler's body."""
        q = self.wq_of_tid.get(t.tid)
        if q is None:
            return self.programs[t.tid]
        if t.wq_item is None:
            return (WorkPop(q), NotifyWait(K_WORK, clear=True, timeout=FOREVER))
        return self.work_bodies[t.wq_item] + (WorkDone(q),)

    def _finish(self, t: Thread, result: Result, data: int = 0):
        prog = self._program(t)
        op = prog[t.op_index] if t.op_index < len(prog) else Exit()
        if t.tid not in self.wq_of_tid:                                   # a queue thread has no program results
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
                       satisfied, consume, pre_nowait=None, pre_block=None, on_enqueue=None,
                       finish=None) -> bool:
        """Sections 1 to 3 of SPEC-004 §5.1 driven by `t.pc`. `pre_nowait` runs before the
        EMB_NO_WAIT check and `pre_block` after it; either may return a Result to finish with
        (SPEC-005 §3.2: self-relock, then cycle detection). `on_enqueue` runs after the enqueue
        inside section 1 (the inheritance walk). `finish` replaces _finish (a condition wait's
        two phases). Returns True when the op completed."""
        finish = finish or self._finish
        if t.pc == 0:
            if t.cancel_pending and not t.cancel_deferred:
                t.cancel_pending = False
                finish(t, Result.CANCELED)
                return True
            if satisfied():
                finish(t, Result.SATISFIED, consume())
                return True
            if pre_nowait is not None:
                r = pre_nowait()
                if r is not None:
                    finish(t, r)
                    return True
            if timeout == NO_WAIT:
                finish(t, Result.TIMEOUT)
                return True
            if pre_block is not None:
                r = pre_block()
                if r is not None:
                    finish(t, r)
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
            self._event("wait_begin", t.tid, q.qid, int(reason))      # traced by embk_wait_prepare()
            if on_enqueue is not None:
                on_enqueue()                                          # after it, in the same section
            t.pc = 1
            return False
        if t.pc == 1:
            if t.deadline is not None:
                self.timeout_arm(t, t.deadline, t.saved_gen)
                if self.tickless and t.deadline <= self.now:
                    # Programming an already-due compare requests an interrupt; the timeout
                    # lock still masks delivery (SPEC-002 §6, SPEC-003 §4.2). A raw-clock tick
                    # during this section can precede that delivery in the runner's trace.
                    self.timer_pending = True
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
        finish(t, t.wake_result, t.wake_data)
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
        if t.destroyed:
            raise ModelError(f"notification set to destroyed thread {t.tid}: a dangling binding (ADR-046)")
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

    def _op_lock(self, t: Thread, op: Lock, finish=None) -> bool:
        m = self.mutexes[op.mutex]
        q = self.queues[m.qid]
        finish = finish or self._finish

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
            finish(t, Result.EPERM)                                       # ceiling violation (release build)
            return True
        return self._block_section(t, q, Reason.OBJECT, op.timeout, satisfied, consume,
                                   pre_nowait, pre_block, on_enqueue, finish)

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
            t.collected = True
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
            target.collected = True
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
        if t.wait_state != WaitState.READY and t.wait_reason in CANCELABLE and t.wait_queue is not None \
                and not t.cancel_deferred:
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
        sem.binding = None                                                # releases the target (ADR-046)

    # ------------------------------------------------------------------ binding lifetime (M2-03)
    def _record(self, t: Thread, result: Result, data: int = 0):
        """The status of a non-wait operation that the bridge compares."""
        if t.tid not in self.wq_of_tid:
            t.results = t.results + ((t.op_index, result, data),)

    def _bound_to(self, tid: int) -> bool:
        return any(o.binding is not None and o.binding[0] == tid
                   for o in list(self.sems.values()) + list(self.events.values()) + list(self.msgqs.values()))

    # ------------------------------------------------------------------ message queues (SPEC-007 §2, M2-07)
    def _msgq_store(self, q: Msgq, value: int, front: bool):
        was_empty = not q.ring
        q.ring = (value,) + q.ring if front else q.ring + (value,)
        q.accepted += 1
        if was_empty and q.binding is not None:                           # empty to non-empty, receive side
            tid, bit = q.binding
            self._notify_set(self.threads[tid], 1 << bit)

    def _msgq_put(self, q: Msgq, value: int, front: bool) -> bool:
        """The non-blocking part of send: a waiting receiver, else a free slot. False when full."""
        rq = self.queues[q.recv_qid]
        w = self.wait_first(rq)
        if w is not None:
            q.accepted += 1
            q.received += 1
            self.wake(w, Result.SATISFIED, value, rq, handoff=True)        # copied into its buffer
            return True
        if len(q.ring) < q.capacity:
            self._msgq_store(q, value, front)
            return True
        return False

    def _msgq_take(self, q: Msgq) -> Optional[int]:
        """The non-blocking part of receive: the head, with the first waiting sender's item moved in."""
        if not q.ring:
            return None
        value, q.ring = q.ring[0], q.ring[1:]
        q.received += 1
        sq = self.queues[q.send_qid]
        w = self.wait_first(sq)
        if w is not None:
            q.ring = q.ring + (w.msg_value,)                              # the slot hand-off
            q.accepted += 1
            self.wake(w, Result.SATISFIED, 0, sq, handoff=True)
        return value

    def _op_msg_send(self, t: Optional[Thread], op: MsgSend) -> bool:
        q = self.msgqs[op.q]
        if t is None:                                                     # ISR, EMB_NO_WAIT only
            if not q.destroyed:
                self._msgq_put(q, op.value, front=False)
            return True
        if t.pc == 0 and q.destroyed:
            self._finish(t, Result.STALE)
            return True
        if t.pc == 0:
            t.msg_value = op.value
        sq = self.queues[q.send_qid]
        return self._block_section(t, sq, Reason.OBJECT, op.timeout,
                                   lambda: self._msgq_room(q), lambda: self._msgq_put(q, op.value, False) and 0)

    def _msgq_room(self, q: Msgq) -> bool:
        return self.wait_first(self.queues[q.recv_qid]) is not None or len(q.ring) < q.capacity

    def _op_msg_recv(self, t: Optional[Thread], op: MsgRecv) -> bool:
        q = self.msgqs[op.q]
        if t is None:
            if not q.destroyed:
                self._msgq_take(q)
            return True
        if t.pc == 0 and q.destroyed:
            self._finish(t, Result.STALE)
            return True
        rq = self.queues[q.recv_qid]
        return self._block_section(t, rq, Reason.OBJECT, op.timeout, lambda: bool(q.ring), lambda: self._msgq_take(q))

    def _op_msg_nowait(self, op) -> tuple:
        """send_front and peek: (result, data); neither waits."""
        q = self.msgqs[op.q]
        if q.destroyed:
            return Result.STALE, 0
        if isinstance(op, MsgSendFront):
            return (Result.SATISFIED, 0) if self._msgq_put(q, op.value, front=True) else (Result.TIMEOUT, 0)
        return (Result.SATISFIED, q.ring[0]) if q.ring else (Result.TIMEOUT, 0)

    # ------------------------------------------------------------------ work queues (SPEC-006 §4, M2-08)
    def _op_submit(self, op: Submit) -> Result:
        wq, w = self.workqs[op.q], self.works[op.w]
        if wq.destroyed:
            return Result.STALE
        if wq.stopping:
            return Result.STATE                                           # being destroyed
        if w.state == WORK_PENDING:
            return Result.EXIST                                           # idempotent
        if w.state == WORK_RUNNING:
            if w.queue != op.q:
                return Result.BUSY                                        # one queue at a time
            w.resubmitted = True                                          # appended again when it returns
            return Result.SATISFIED
        w.state, w.queue = WORK_PENDING, op.q
        wq.fifo = wq.fifo + (("w", op.w),)
        self._notify_set(self.threads[wq.tid], K_WORK)
        return Result.SATISFIED

    def _op_work_cancel(self, op: WorkCancel) -> int:
        w = self.works[op.w]
        if w.state == WORK_PENDING:
            wq = self.workqs[w.queue]
            wq.fifo = tuple(e for e in wq.fifo if e != ("w", op.w))
            w.state, w.queue = WORK_IDLE, None
            return WORK_PENDING
        if w.state == WORK_RUNNING:
            w.resubmitted = False                                         # the handler completes
            return WORK_RUNNING
        return WORK_IDLE

    def _wake_done(self, wq: Workq, target: tuple):
        """Wake every completion waiter for @target, in queue order."""
        dq = self.queues[wq.done_qid]
        for tid, _ in list(dq.waiters):
            if self.threads[tid].work_target == target:
                self.wake(self.threads[tid], Result.SATISFIED, 0, dq, handoff=True)

    def _op_work_pop(self, t: Thread, op: WorkPop):
        wq = self.workqs[op.q]
        if wq.fifo:
            entry, wq.fifo = wq.fifo[0], wq.fifo[1:]
            if entry[0] == "m":                                           # a flush marker: never run
                self._wake_done(wq, entry)
                self.reschedule_if_needed()
                return
            w = self.works[entry[1]]
            w.state = WORK_RUNNING
            wq.running = w.wid
            self._event("work_run", w.wid, 0)
            t.wq_item, t.op_index, t.pc = w.wid, 0, 0                     # the handler's body follows
            return
        if wq.stopping:
            self._exit(t, 0)
            return
        self._advance(t)                                                  # wait for K_WORK

    def _op_work_done(self, t: Thread, op: WorkDone):
        wq, w = self.workqs[op.q], self.works[t.wq_item]
        if t.owned:
            raise ModelError(f"work item {w.wid} returned owning mutexes {t.owned} (SPEC-006 §6)")
        self._event("work_run", w.wid, 1)
        wq.running = None
        w.runs += 1
        if w.resubmitted:
            w.resubmitted = False
            w.state = WORK_PENDING
            wq.fifo = wq.fifo + (("w", w.wid),)
        else:
            w.state, w.queue = WORK_IDLE, None
        self._wake_done(wq, ("w", w.wid))
        t.wq_item, t.op_index, t.pc = None, 0, 0
        self.reschedule_if_needed()

    def _op_work_cancel_sync(self, t: Thread, op: WorkCancelSync) -> bool:
        w = self.works[op.w]
        if t.pc == 0 and w.state != WORK_IDLE and self.wq_of_tid.get(t.tid) == w.queue:
            self._finish(t, Result.EPERM)                                 # it would wait for itself: misuse
            return True
        dq = self.queues[self.workqs[w.queue].done_qid] if w.queue is not None else None

        def satisfied():
            return w.state != WORK_RUNNING

        def consume():
            self._op_work_cancel(WorkCancel(op.w))
            return 0

        def pre_nowait():
            w.resubmitted = False
            return None

        def on_enqueue():
            t.work_target = ("w", op.w)

        def finish(t_, result, data=0):
            t_.work_target = None
            self._finish(t_, result, data)
        return self._block_section(t, dq, Reason.OBJECT, op.timeout, satisfied, consume, pre_nowait=pre_nowait,
                                   on_enqueue=on_enqueue, finish=finish)

    def _op_work_flush(self, t: Thread, op: WorkFlush) -> bool:
        wq = self.workqs[op.q]
        if t.pc == 0:
            if wq.destroyed:
                self._finish(t, Result.STALE)
                return True
            if t.tid == wq.tid:
                self._finish(t, Result.EPERM)                             # it would wait for itself: misuse
                return True
            if wq.stopping:
                self._finish(t, Result.STATE)
                return True
        marker = ("m", t.tid)

        def pre_block():
            wq.fifo = wq.fifo + (marker,)
            self._notify_set(self.threads[wq.tid], K_WORK)
            return None

        def on_enqueue():
            t.work_target = marker

        def finish(t_, result, data=0):
            if result != Result.SATISFIED:
                wq.fifo = tuple(e for e in wq.fifo if e != marker)        # unlinked on timeout or cancel
            t_.work_target = None
            self._finish(t_, result, data)
        return self._block_section(t, self.queues[wq.done_qid], Reason.OBJECT, op.timeout,
                                   lambda: not wq.fifo and wq.running is None, lambda: 0,
                                   pre_block=pre_block, on_enqueue=on_enqueue, finish=finish)

    def _op_workq_destroy(self, t: Thread, op: WorkqDestroy) -> bool:
        wq = self.workqs[op.q]
        qt = self.threads[wq.tid]
        if t.pc == 0:
            if wq.destroyed:
                self._finish(t, Result.STALE)
                return True
            busy = (wq.fifo or wq.running is not None or wq.stopping or self.queues[wq.done_qid].waiters
                    or any((not tm.isr and tm.q == wq.q) or tm.target == wq.q for tm in self.timers.values()))
            if t.tid == wq.tid or busy:
                self._finish(t, Result.EPERM)                             # misuse: a fault when checked
                return True
            t.cancel_deferred = True                                      # the join is not cancelable (E6)

        def pre_block():
            wq.stopping = True
            self._notify_set(qt, K_WORK)
            return None

        def on_enqueue():
            qt.joiner = t.tid

        def finish(t_, result, data=0):
            t_.cancel_deferred = False
            qt.joiner, qt.collected, qt.destroyed = None, True, True
            wq.destroyed = True
            self._finish(t_, result, 0)
        return self._block_section(t, self.queues[-(1000 + wq.tid)], Reason.JOIN, FOREVER,
                                   lambda: qt.terminated, lambda: 0, pre_block=pre_block,
                                   on_enqueue=on_enqueue, finish=finish)

    def _op_msg_destroy(self, op: MsgDestroy):
        q = self.msgqs[op.q]
        if q.destroyed:
            return
        self.wake_all(self.queues[q.recv_qid], Result.DESTROYED)
        self.wake_all(self.queues[q.send_qid], Result.DESTROYED)
        q.dropped += len(q.ring)
        q.ring, q.destroyed, q.binding = (), True, None

    # ------------------------------------------------------------------ event flags (SPEC-005 §5, M2-05)
    def _op_event_set(self, op: EventSet):
        e = self.events[op.event]
        if e.destroyed:
            return
        before = e.bits
        e.bits |= op.bits
        q = self.queues[e.qid]
        for tid, _ in list(q.waiters):                                    # queue order, one section
            w = self.threads[tid]
            got = event_satisfying(e.bits, w.notify_mask, w.notify_all)
            if got:
                if w.notify_clear:
                    e.bits &= ~got                                        # before the next waiter (§5.2)
                self.wake(w, Result.SATISFIED, got, q, handoff=True)
        self._event("event_set", op.event, e.bits)
        if e.binding is not None and e.bits != 0 and e.bits != before:  # what direct waiters left
            tid, bit = e.binding
            self._notify_set(self.threads[tid], 1 << bit)

    def _op_event_clear(self, op: EventClear):
        e = self.events[op.event]
        if not e.destroyed:
            e.bits &= ~op.bits

    def _op_event_wait(self, t: Thread, op: EventWait) -> bool:
        e = self.events[op.event]
        if t.pc == 0:
            if e.destroyed:
                self._finish(t, Result.STALE)
                return True
            t.notify_mask, t.notify_all, t.notify_clear = op.mask, op.all, op.clear   # the wait's arguments

        def satisfied():
            return event_satisfying(e.bits, op.mask, op.all) != 0

        def consume():
            got = event_satisfying(e.bits, op.mask, op.all)
            if op.clear:
                e.bits &= ~got
            return got
        return self._block_section(t, self.queues[e.qid], Reason.OBJECT, op.timeout, satisfied, consume)

    # ------------------------------------------------------------------ condition variables (SPEC-005 §6, M2-06)
    def _op_cond_wait(self, t: Thread, op: CondWait) -> bool:
        c, m = self.conds[op.cond], self.mutexes[op.mutex]
        if t.cond_phase == 0:
            if t.pc == 0:
                if c.destroyed:
                    self._finish(t, Result.STALE)
                    return True
                if m.owner != t.tid:
                    self._finish(t, Result.EPERM)                         # misuse: a fault when checked
                    return True
                if m.count != 1:
                    self._finish(t, Result.STATE)                         # a recursive hold
                    return True
                if t.cancel_pending:
                    t.cancel_pending = False
                    self._finish(t, Result.CANCELED)                      # the mutex was never released
                    return True
                if op.timeout == NO_WAIT:
                    self._finish(t, Result.TIMEOUT)                       # no stored signal; the mutex is kept
                    return True

            def release():
                self._release(t, m, dead=False)                           # the same section as the enqueue

            def to_relock(tt, result, data=0):
                t.cond_result, t.cond_phase, t.pc = result, 1, 0
                t.wait_reason, t.wake_result, t.wake_data = None, None, 0
            self._block_section(t, self.queues[c.qid], Reason.OBJECT, op.timeout, lambda: False, lambda: 0,
                                on_enqueue=release, finish=to_relock)
            return False
        if t.cond_phase == 2:                                             # the report follows the lock section
            result, data = t.cond_result, t.cond_data
            t.cond_result, t.cond_data, t.cond_phase = None, 0, 0
            self._finish(t, result, data)
            return True
        t.cancel_deferred = True

        def relocked(tt, result, data=0):
            t.cancel_deferred = False
            self._event("wait_end", t.tid, int(result), "Lock")          # the relock's own record
            if result == Result.SATISFIED and m.owner != t.tid:
                raise ModelError(f"thread {t.tid} returns from a condition wait without its mutex")
            if result == Result.SATISFIED and data & OWNERDEAD_FLAG:
                t.cond_result, t.cond_data = Result.SATISFIED, OWNERDEAD_FLAG   # EMB_EOWNERDEAD wins
            elif result != Result.SATISFIED:
                t.cond_result = result                                    # the relock failed: no mutex
            t.cond_phase, t.pc = 2, 0
            t.wait_reason, t.wake_result, t.wake_data = None, None, 0
        return self._op_lock(t, Lock(op.mutex, FOREVER), finish=relocked)

    def _op_cond_wake(self, op):
        c = self.conds[op.cond]
        if c.destroyed:
            return
        q = self.queues[c.qid]
        if isinstance(op, CondDestroy):
            self.wake_all(q, Result.DESTROYED)
            c.destroyed = True
        elif isinstance(op, CondBroadcast):
            while q.waiters:
                self.wake(self.threads[q.waiters[0][0]], Result.SATISFIED, 0, q, handoff=True)
        else:
            w = self.wait_first(q)
            if w is not None:
                self.wake(w, Result.SATISFIED, 0, q, handoff=True)

    def _op_event_destroy(self, op: EventDestroy):
        e = self.events[op.event]
        if e.destroyed:
            return
        self.wake_all(self.queues[e.qid], Result.DESTROYED)
        e.destroyed = True
        e.binding = None

    def _op_bind(self, t: Thread, op: BindNotify) -> Result:
        sem, target = self.sems[op.sem], self.threads[op.tid]
        if sem.destroyed or target.destroyed:
            return Result.STALE                                           # a stale POINTER handle
        if sem.binding is not None:
            return Result.EXIST
        if target.terminated:
            return Result.STATE
        sem.binding = (op.tid, op.bit)
        return Result.SATISFIED

    def _op_unbind(self, op: UnbindNotify) -> Result:
        sem = self.sems[op.sem]
        if sem.destroyed:
            return Result.STALE
        if sem.binding is None:
            return Result.NOENT
        sem.binding = None
        return Result.SATISFIED

    def _op_thread_destroy(self, op: ThreadDestroy) -> Result:
        target = self.threads[op.tid]
        if target.destroyed:
            return Result.STALE
        reusable = target.inactive or (target.terminated and target.collected)
        if not reusable or self._bound_to(op.tid):
            return Result.BUSY                                            # misuse: a fault when checked
        if target.inactive:                                               # never started: leaves its START wait
            q = self.queues[target.wait_queue]
            q.waiters = [(tid, s) for tid, s in q.waiters if tid != target.tid]
            target.wait_queue, target.wait_reason, target.wait_state = None, None, WaitState.READY
            target.inactive, target.terminated = False, True
        target.destroyed = True
        return Result.SATISFIED

    def step_thread(self) -> bool:
        if self.current is None:
            return False
        t = self._current_thread()
        prog = self._program(t)
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
            if self._op_notify_wait(t, op) and t.tid in self.wq_of_tid and t.wq_item is None:
                t.op_index = 0                                            # back to the FIFO
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
        elif isinstance(op, EventWait):
            self._op_event_wait(t, op)
        elif isinstance(op, CondWait):
            self._op_cond_wait(t, op)
        elif isinstance(op, (MsgSend, MsgRecv)):
            done = self._op_msg_send(t, op) if isinstance(op, MsgSend) else self._op_msg_recv(t, op)
            if done:
                self.reschedule_if_needed()                               # a hand-off may preempt
        elif isinstance(op, MsgSendFront):
            res, data = self._op_msg_nowait(op)
            self._finish(t, res, data)                                    # traced like a send
            self.reschedule_if_needed()
        elif isinstance(op, MsgPeek):
            res, data = self._op_msg_nowait(op)
            t.results = t.results + ((t.op_index, res, data),)            # a snapshot: not traced
            self._advance(t)
        elif isinstance(op, MsgDestroy):
            self._op_msg_destroy(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, (CondSignal, CondBroadcast, CondDestroy)):
            self._op_cond_wake(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, (EventSet, EventClear, EventDestroy)):
            if isinstance(op, EventSet):
                self._op_event_set(op)
            elif isinstance(op, EventClear):
                self._op_event_clear(op)
            else:
                self._op_event_destroy(op)
            self._advance(t)
            self.reschedule_if_needed()
        elif isinstance(op, NONWAIT_RESULT_OPS):
            if isinstance(op, BindNotify):
                res = self._op_bind(t, op)
            elif isinstance(op, UnbindNotify):
                res = self._op_unbind(op)
            elif isinstance(op, Submit):
                res = self._op_submit(op)
            elif isinstance(op, TimerStart):
                res = self._op_timer_start(op)
            elif isinstance(op, DworkSchedule):
                res = self._op_dwork_schedule(op)
            else:
                res = self._op_thread_destroy(op)
            self._record(t, res)
            self._advance(t)
            if isinstance(op, Submit):
                self.reschedule_if_needed()
        elif isinstance(op, TimerStop):
            self._record(t, Result.SATISFIED, self._op_timer_stop(op))
            self._advance(t)
        elif isinstance(op, DworkCancel):
            self._record(t, Result.SATISFIED, self._op_dwork_cancel(op))
            self._advance(t)
        elif isinstance(op, TimerStopSync):
            if self._op_timer_stop_sync(t, op):
                self.reschedule_if_needed()
        elif isinstance(op, WorkCancel):
            self._record(t, Result.SATISFIED, self._op_work_cancel(op))
            self._advance(t)
        elif isinstance(op, WorkPop):
            self._op_work_pop(t, op)
        elif isinstance(op, WorkDone):
            self._op_work_done(t, op)
        elif isinstance(op, (WorkCancelSync, WorkFlush, WorkqDestroy)):
            if isinstance(op, WorkCancelSync):
                done = self._op_work_cancel_sync(t, op)
            elif isinstance(op, WorkFlush):
                done = self._op_work_flush(t, op)
            else:
                done = self._op_workq_destroy(t, op)
            if done:
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
            self._isr_op(op)
        self.irq_nesting -= 1
        self._p1()

    def _isr_op(self, op):
        """One operation of an interrupt body or of an EMB_TIMER_ISR_CONTEXT callback."""
        if True:
            if not isinstance(op, ISR_SAFE_OPS):
                raise ModelError(f"{op!r} is not ISR-safe (SPEC-001 §5.2)")
            if isinstance(op, ISR_NO_WAIT_ONLY) and op.timeout != NO_WAIT:
                raise ModelError(f"{op!r} may wait, which an ISR must not (SPEC-007 §1)")
            if isinstance(op, Give):
                self._op_give(op)
            elif isinstance(op, Resume):
                self._op_resume(op)
            elif isinstance(op, NotifySet):
                self._notify_set(self.threads[op.tid], op.bits)
            elif isinstance(op, EventSet):
                self._op_event_set(op)
            elif isinstance(op, EventClear):
                self._op_event_clear(op)
            elif isinstance(op, Start):
                self._op_start(op)
            elif isinstance(op, MsgSend):
                self._op_msg_send(None, op)
            elif isinstance(op, MsgRecv):
                self._op_msg_recv(None, op)
            elif isinstance(op, NONWAIT_DATA_OPS):
                self._op_msg_nowait(op)
            elif isinstance(op, Submit):
                self._op_submit(op)
            elif isinstance(op, WorkCancel):
                self._op_work_cancel(op)
            elif isinstance(op, TimerStart):
                self._op_timer_start(op)
            elif isinstance(op, TimerStop):
                self._op_timer_stop(op)
            elif isinstance(op, DworkSchedule):
                self._op_dwork_schedule(op)
            elif isinstance(op, DworkCancel):
                self._op_dwork_cancel(op)

    def tick(self):
        self.now += 1
        self._expire_due()

    def _expire_due(self):
        """The timer interrupt: every deadline at or before now, in deadline order. Timers come
        from their own list (ADR-047): the earlier head first, a thread timeout first on equal
        deadlines, and a timer armed by this pass waits for the next one."""
        self.timer_pending = False
        self.irq_nesting += 1
        timers_done = False
        while True:
            th = self.timeouts[0] if self.timeouts and self.timeouts[0][0] <= self.now else None
            tm = None
            if not timers_done:
                armed = sorted((t.deadline, t.seq, t.tmid) for t in self.timers.values() if t.deadline is not None)
                if armed and armed[0][0] <= self.now and (th is None or armed[0][0] < th[0]):
                    tm = self.timers[armed[0][2]]
            if tm is not None:
                if tm.armed_in_pass:
                    timers_done = True
                    continue
                self._timer_expired(tm)
                continue
            if th is None:
                break
            self.timeouts.pop(0)
            self.wake_timeout(th[2], th[3])
        for t in self.timers.values():
            t.armed_in_pass = False
        self.irq_nesting -= 1
        self._p1()

    # ------------------------------------------------------------------ software timers (SPEC-003 §7, M2-09)
    def _timer_arm(self, tm: Timer, deadline: int):
        tm.deadline, tm.seq = deadline, self._next_seq()
        tm.armed_in_pass = self.irq_nesting > 0 and self.in_expiry
        if self.tickless and deadline <= self.now:
            self.timer_pending = True                                 # an already-due compare

    def _timer_expired(self, tm: Timer):
        tm.expiries += 1
        if tm.period:
            nxt = tm.deadline + tm.period                             # from the previous deadline: no drift
            if nxt <= self.now:                                       # whole periods already past, closed form
                skip = (self.now - nxt) // tm.period + 1
                tm.overruns += skip
                nxt += skip * tm.period
            self.in_expiry = True
            self._timer_arm(tm, nxt)                                  # before the callback
            self.in_expiry = False
        else:
            tm.deadline = None
        self._event("timer_expire", tm.tmid)
        if tm.dwork is not None:                                      # the O(1) submit (KRN-WQ-007)
            if tm.target is not None:
                q, tm.target = tm.target, None
                self._op_submit(Submit(q, tm.dwork))
            return
        if tm.isr:
            tm.in_callback, self.in_expiry = True, True
            for op in self.timer_bodies[tm.tmid]:
                self._isr_op(op)
            tm.in_callback, self.in_expiry = False, False
            return
        w = self.works[TIMER_WORK_BASE + tm.tmid]
        if w.state == WORK_PENDING or (w.state == WORK_RUNNING and w.resubmitted):
            tm.overruns += 1                                          # coalesced
        else:
            self._op_submit(Submit(tm.q, w.wid))

    def _op_dwork_schedule(self, op: DworkSchedule) -> Result:
        wq, w, tm = self.workqs[op.q], self.works[op.w], self.timers[DWORK_TIMER_BASE + op.w]
        if wq.destroyed:
            return Result.STALE
        if wq.stopping:
            return Result.STATE
        if w.state == WORK_PENDING or (tm.target is not None and not op.replace):
            return Result.EXIST                                       # nothing changes
        if w.state == WORK_RUNNING and w.queue != op.q:
            return Result.BUSY
        tm.deadline, tm.target = None, op.q
        self._timer_arm(tm, self.now + op.delay)
        self._event("timer_start", tm.tmid)
        return Result.SATISFIED

    def _op_dwork_cancel(self, op: DworkCancel) -> int:
        tm = self.timers[DWORK_TIMER_BASE + op.w]
        if tm.target is not None:
            tm.deadline, tm.target = None, None
            return WORK_PENDING                                       # the delay stopped
        return self._op_work_cancel(WorkCancel(op.w))

    def _op_timer_start(self, op: TimerStart) -> Result:
        tm = self.timers[op.timer]
        tm.deadline = None
        tm.period = op.period
        if op.period:
            tm.origin = self.now + op.first
        self._timer_arm(tm, self.now + op.first)
        self._event("timer_start", tm.tmid)
        return Result.SATISFIED

    def _op_timer_stop(self, op) -> int:
        tm = self.timers[op.timer]
        st = WORK_IDLE
        if tm.deadline is not None:
            tm.deadline = None
            st = WORK_PENDING
        if not tm.isr:
            was = self._op_work_cancel(WorkCancel(TIMER_WORK_BASE + tm.tmid))
            if was != WORK_IDLE:
                st = was
        if tm.in_callback:
            st = WORK_RUNNING
        self._event("timer_stop", tm.tmid, st)
        return st

    def _op_timer_stop_sync(self, t: Thread, op: TimerStopSync) -> bool:
        tm = self.timers[op.timer]
        if t.stop_phase == 0:
            w = self.works.get(TIMER_WORK_BASE + tm.tmid)
            if w is not None and w.state == WORK_RUNNING and self.wq_of_tid.get(t.tid) == tm.q:
                self._finish(t, Result.EPERM)                         # its own callback: misuse
                return True
            self._op_timer_stop(op)                                   # the stop section
            t.stop_phase = 1
            return False
        if tm.isr:
            t.stop_phase = 0
            self._finish(t, Result.SATISFIED)                         # nothing runs under a thread
            return True
        done = self._op_work_cancel_sync(t, WorkCancelSync(TIMER_WORK_BASE + tm.tmid, op.timeout))
        if done:
            t.stop_phase = 0
        return done

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
            if t.destroyed and not t.terminated:
                raise ModelError(f"destroyed thread {t.tid} is not terminated")
        for s in self.sems.values():                                      # ADR-046: no binding outlives its target
            if s.binding is not None and self.threads[s.binding[0]].destroyed:
                raise ModelError(f"semaphore {s.sid} is bound to destroyed thread {s.binding[0]}")
        for tm in self.timers.values():                                   # SPEC-003 §7.4, KRN-TIM-015
            if tm.period and tm.deadline is not None and (tm.deadline - tm.origin) % tm.period != 0:
                raise ModelError(f"timer {tm.tmid}: deadline {tm.deadline} drifted off {tm.origin} + k * {tm.period}")
            if tm.dwork is not None and (tm.target is None) != (tm.deadline is None):
                raise ModelError(f"delayed item {tm.dwork}: target {tm.target} but deadline {tm.deadline}")
            if tm.deadline is not None and tm.armed_in_pass:
                raise ModelError(f"timer {tm.tmid}: the pass marker survived its expiry pass")
        for w in self.works.values():                                     # SPEC-006 §4.3
            places = [(wq.q, e) for wq in self.workqs.values() for e in wq.fifo if e == ("w", w.wid)]
            if (w.state == WORK_PENDING) != (len(places) == 1) or len(places) > 1:
                raise ModelError(f"work item {w.wid}: state {w.state} but queued {len(places)} times")
            if places and places[0][0] != w.queue:
                raise ModelError(f"work item {w.wid}: queued on {places[0][0]}, owned by {w.queue}")
            running_on = [wq.q for wq in self.workqs.values() if wq.running == w.wid]
            if (w.state == WORK_RUNNING) != bool(running_on) or (running_on and running_on != [w.queue]):
                raise ModelError(f"work item {w.wid}: state {w.state}, running on {running_on}")
            if w.resubmitted and w.state != WORK_RUNNING:
                raise ModelError(f"work item {w.wid}: resubmitted while not running")
            if w.state == WORK_IDLE and w.queue is not None:
                raise ModelError(f"work item {w.wid}: idle but on queue {w.queue}")
        for wq in self.workqs.values():
            if wq.running != self.threads[wq.tid].wq_item:
                raise ModelError(f"work queue {wq.q}: running {wq.running}, thread runs {self.threads[wq.tid].wq_item}")
            for tid, _ in self.queues[wq.done_qid].waiters:
                target = self.threads[tid].work_target
                ok = (target is not None and
                      (self.works[target[1]].state == WORK_RUNNING and self.works[target[1]].queue == wq.q
                       if target[0] == "w" else target in wq.fifo))
                if not ok:
                    raise ModelError(f"work queue {wq.q}: thread {tid} waits for {target}, which cannot complete")
        for q in self.msgqs.values():                                     # SPEC-007 §2.2
            if self.queues[q.recv_qid].waiters and q.ring:
                raise ModelError(f"message queue {q.qid}: receivers wait while {len(q.ring)} items are stored")
            if self.queues[q.send_qid].waiters and len(q.ring) < q.capacity and not q.destroyed:
                raise ModelError(f"message queue {q.qid}: senders wait while a slot is free")
            if len(q.ring) > q.capacity:
                raise ModelError(f"message queue {q.qid}: {len(q.ring)} items exceed capacity {q.capacity}")
            if q.accepted != q.received + len(q.ring) + q.dropped:            # every item once (KRN-IPC-022)
                raise ModelError(f"message queue {q.qid}: {q.accepted} accepted != {q.received} received "
                                 f"+ {len(q.ring)} stored + {q.dropped} dropped")
        for e in self.events.values():                                    # SPEC-005 §5.2: a set served every waiter it satisfied
            for tid, _ in self.queues[e.qid].waiters:
                w = self.threads[tid]
                if event_satisfying(e.bits, w.notify_mask, w.notify_all):
                    raise ModelError(f"event {e.eid} bits {e.bits:#x} satisfy queued waiter {tid}")
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
        if self.timer_pending:
            raise ModelError("terminal state with a pending timer interrupt")
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
        if self.timer_pending:
            # Deliver before another thread section. A tick may represent counter advancement
            # while the arming section was masked, followed by delivery at that later instant.
            return [("timer",), ("tick",)]
        acts = []
        if self.current is not None:
            acts.append(("run",))
        for i in range(len(self.isr_bodies)):
            if i not in self.isr_fired:
                acts.append(("isr", i))
        # A periodic timer keeps a tick enabled forever. The runner advances time only when
        # idle or by one of its few scheduled ticks, so past the scenario's horizon the model
        # ticks only while idle.
        periodic = any(t.deadline is not None and t.period for t in self.timers.values())
        past = periodic and self.horizon is not None and self.now >= self.horizon
        if (self.timeouts or any(t.deadline is not None for t in self.timers.values())) and \
                not (past and self.current is not None):
            acts.append(("tick",))
        return acts

    def apply(self, act):
        if self.timer_pending and act[0] not in ("timer", "tick"):
            raise ModelError("pending timer interrupt must be delivered before another section")
        if act[0] == "run":
            self.step_thread()
        elif act[0] == "isr":
            self.fire_isr(act[1])
        elif act[0] == "tick":
            self.tick()
        elif act[0] == "timer" and self.timer_pending:
            self._expire_due()                # already due: no extra clock tick is required
        else:
            raise ModelError(f"unknown action {act}")
        self.check_invariants()

    def outcome(self) -> tuple:
        """Hashable summary of a terminal state."""
        return (("threads", tuple((tid, t.results) for tid, t in sorted(self.threads.items()))),
                ("sems", tuple((sid, s.count) for sid, s in sorted(self.sems.items()))),
                ("blocked", tuple(tid for tid, t in sorted(self.threads.items()) if t.wait_state == WaitState.BLOCKED)),
                ("mutex_owners", tuple((mid, m.owner) for mid, m in sorted(self.mutexes.items()))),
                ("notify_bits", tuple((tid, t.notify_bits) for tid, t in sorted(self.threads.items()))),
                ("events", tuple((eid, e.bits) for eid, e in sorted(self.events.items()))),
                ("msgqs", tuple((qid, q.ring) for qid, q in sorted(self.msgqs.items()))),
                ("works", tuple((wid, w.state, w.runs) for wid, w in sorted(self.works.items()))),
                ("timers", tuple((tm, t.expiries, t.overruns, t.deadline is not None)
                                 for tm, t in sorted(self.timers.items()))))
