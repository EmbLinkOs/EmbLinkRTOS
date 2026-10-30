/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The differential runner (TEST-008, SPEC-004 §13, SPEC-013 §7): executes one scenario of
 * the reference model's catalogue on the native port under a given schedule of interrupt
 * raises and ticks, then prints the kernel's trace and every operation's result so that
 * tools/model/bridge.py can replay the run through the model and compare the outcomes.
 *
 * Usage: diff_runner '<scenario>' [--schedule 'isr0@12,tick@30,...']
 *
 * Scenario grammar, one string, sections separated by '|':
 *   T<tid>=<prio>[,inactive]:<op>;<op>...     a thread and its program
 *   S<sid>=<count>[,fifo][,bind=<tid>.<bit>]  a semaphore
 *   M<mid>=<protocol>,<ceiling>,<recursive>   a mutex (protocol 0 inherit, 1 ceiling, 2 none)
 *   E<eid>=<bits>[,fifo][,bind=<tid>.<bit>]   event flags
 *   C<cid>=<fifo>                             a condition variable (fifo 0 or 1)
 *   Q<qid>=<capacity>[,fifo][,bind=<tid>.<bit>] a message queue of 32-bit items
 *   I<index>:<op>;<op>...                     an ISR body (give, resume, nset, start, eset, eclear,
 *                                             and qsend, qfront, qrecv, qpeek with no wait)
 * Operations (t: -1 forever, 0 no wait, n ticks):
 *   take s t | give s | lock m t | unlock m | nset tid bits | nwait mask all clear t |
 *   sleep t | yield | setprio tid prio | suspend tid | resume tid | cancel tid | destroy s |
 *   slock | sunlock | exit code | start tid | join tid t | bind s tid bit | unbind s |
 *   tdestroy tid | eset e bits | eclear e bits | ewait e mask all clear t | edestroy e |
 *   cwait c m t | csignal c | cbcast c | cdestroy c | qsend q value t | qfront q value |
 *   qrecv q t | qpeek q | qdestroy q
 * A schedule entry fires right after the n-th trace event (or in idle, if that event never
 * comes): "isr<i>@n" raises ISR i, "tick@n" advances virtual time by one tick.
 */
#include <emb/emb.h>

#include <stdio.h>
#include <stdlib.h>

#include <emb_board.h>
#include <emb_native.h>
#include <string.h>

#if !CONFIG_EMB_TICKLESS
#include <embk/time.h>
#endif
#if CONFIG_EMB_MSGQ
#include <embk/ipc.h> /* the final ring contents */
#endif

#define MAX_THREADS  8u
#define MAX_OPS      16u
#define MAX_SEMS     4u
#define MAX_MUTEXES  4u
#define MAX_EVFLAGS  4u
#define MAX_CONDS    4u
#define MAX_MSGQS    4u
#define MAX_MSGQ_CAP 8u
#define MAX_ISRS     4u
#define MAX_EVENTS   16384u
#define MAX_SCHED    64u
#define STACK_SIZE   4096u
#define ISR_IRQ_BASE 1u /* interrupt numbers for ISR bodies; 0 is the virtual timer */

#define FOREVER_SLEEP_TICKS 1048576u /* a "sleep forever": its deadline is never reached */

#define EV_ISR_MARK   1000u /* the runner's own record: an ISR body starts, a0 = index */
#define EV_INDEX_MARK 1001u /* a thread's index after a priority change: a0 = tid, a1 = index */

enum op_kind {
    OP_TAKE,
    OP_GIVE,
    OP_LOCK,
    OP_UNLOCK,
    OP_NSET,
    OP_NWAIT,
    OP_SLEEP,
    OP_YIELD,
    OP_SETPRIO,
    OP_SUSPEND,
    OP_RESUME,
    OP_CANCEL,
    OP_DESTROY,
    OP_SLOCK,
    OP_SUNLOCK,
    OP_EXIT,
    OP_START,
    OP_JOIN,
    OP_BIND,
    OP_UNBIND,
    OP_TDESTROY,
    OP_ESET,
    OP_ECLEAR,
    OP_EWAIT,
    OP_EDESTROY,
    OP_CWAIT,
    OP_CSIGNAL,
    OP_CBCAST,
    OP_CDESTROY,
    OP_QSEND,
    OP_QFRONT,
    OP_QRECV,
    OP_QPEEK,
    OP_QDESTROY
};

struct op {
    enum op_kind kind;
    long a;
    long b;
    long c;
    long d;
};

struct program {
    unsigned count;
    struct op ops[MAX_OPS];
};

struct thread_desc {
    long tid;
    uint8_t prio;
    bool inactive;
    struct program prog;
    emb_thread_t handle;
    emb_thread_storage_t storage;
    int status[MAX_OPS];
    unsigned long data[MAX_OPS];
    unsigned done;
    bool destroyed; /* emb_thread_destroy succeeded: the handle is stale */
};

struct sem_desc {
    long sid;
    long count;
    bool fifo;
    long bind_tid;
    long bind_bit;
    emb_sem_storage_t storage;
    emb_sem_t handle;
};

struct evflag_desc {
    long eid;
    long bits;
    bool fifo;
    long bind_tid;
    long bind_bit;
#if CONFIG_EMB_EVENT
    emb_event_storage_t storage;
    emb_event_t handle;
#endif
};

struct cond_desc {
    long cid;
    bool fifo;
#if CONFIG_EMB_COND
    emb_cond_storage_t storage;
    emb_cond_t handle;
#endif
};

struct msgq_desc {
    long qid;
    long capacity;
    bool fifo;
    long bind_tid;
    long bind_bit;
    bool destroyed;
#if CONFIG_EMB_MSGQ
    emb_msgq_storage_t storage;
    emb_msgq_t handle;
    EMB_MSGQ_RING(ring, sizeof(uint32_t), MAX_MSGQ_CAP);
#endif
};

struct mutex_desc {
    long mid;
    long protocol;
    long ceiling;
    bool recursive;
    emb_mutex_storage_t storage;
    emb_mutex_t handle;
};

struct event_rec {
    uint16_t id;
    uint8_t context;
    uint32_t ticks;
    uintptr_t a0;
    uintptr_t a1;
    uintptr_t a2;
};

struct sched_item {
    unsigned is_tick;
    unsigned index;
    unsigned at;
};

static struct thread_desc threads[MAX_THREADS];
static unsigned thread_count;
static struct sem_desc sems[MAX_SEMS];
static unsigned sem_count;
static struct evflag_desc evflags[MAX_EVFLAGS];
static unsigned evflag_count;
static struct cond_desc conds[MAX_CONDS];
static unsigned cond_count;
static struct msgq_desc msgqs[MAX_MSGQS];
static unsigned msgq_count;
static struct mutex_desc mutexes[MAX_MUTEXES];
static unsigned mutex_count;
static struct program isrs[MAX_ISRS];
static unsigned isr_count;
static uint8_t stacks[MAX_THREADS][STACK_SIZE] EMB_ALIGNED(EMB_STACK_ALIGN);

static struct event_rec events[MAX_EVENTS];
static volatile unsigned event_count; /* lock: the kernel's critical section at emission */
static unsigned events_lost;

static struct sched_item sched[MAX_SCHED];
static unsigned sched_count;
static unsigned sched_next;

static unsigned dumped;
static unsigned started;        /* the schedule fires only once the kernel runs */
static unsigned tick_pending;   /* a tick was raised and its timer interrupt has not run */
static unsigned ticks_deferred; /* ticks requested meanwhile: one timer interrupt per tick */

/* ---- parsing ---------------------------------------------------------------------- */

static void die(const char *msg)
{
    (void)fputs("diff_runner: ", stderr);
    (void)fputs(msg, stderr);
    (void)fputs("\n", stderr);
    exit(4);
}

/* Cuts @*p at the next @delim; returns the token and advances past the delimiter. */
static char *cut(char **p, char delim)
{
    char *start = *p;
    char *d;
    if (start == NULL) {
        return NULL;
    }
    d = strchr(start, delim);
    if (d != NULL) {
        *d = '\0';
        *p = d + 1;
    } else {
        *p = NULL;
    }
    return start;
}

static long num(const char *s)
{
    char *end;
    long v;
    if (s == NULL || *s == '\0') {
        die("missing number");
    }
    v = strtol(s, &end, 10);
    if (*end != '\0') {
        die("bad number");
    }
    return v;
}

static void parse_op(char *text, struct op *op)
{
    char *p = text;
    const char *name = cut(&p, ' ');
    const char *args[5] = {NULL, NULL, NULL, NULL, NULL};
    unsigned n = 0u;
    while (p != NULL && n < 5u) {
        args[n++] = cut(&p, ' ');
    }
    op->a = 0;
    op->b = 0;
    op->c = 0;
    op->d = 0;
    if (strcmp(name, "take") == 0) {
        op->kind = OP_TAKE;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "give") == 0) {
        op->kind = OP_GIVE;
        op->a = num(args[0]);
    } else if (strcmp(name, "lock") == 0) {
        op->kind = OP_LOCK;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "unlock") == 0) {
        op->kind = OP_UNLOCK;
        op->a = num(args[0]);
    } else if (strcmp(name, "nset") == 0) {
        op->kind = OP_NSET;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "nwait") == 0) {
        op->kind = OP_NWAIT;
        op->a = num(args[0]);
        op->b = num(args[1]);
        op->c = num(args[2]);
        op->d = num(args[3]);
    } else if (strcmp(name, "sleep") == 0) {
        op->kind = OP_SLEEP;
        op->a = num(args[0]);
    } else if (strcmp(name, "yield") == 0) {
        op->kind = OP_YIELD;
    } else if (strcmp(name, "setprio") == 0) {
        op->kind = OP_SETPRIO;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "suspend") == 0) {
        op->kind = OP_SUSPEND;
        op->a = num(args[0]);
    } else if (strcmp(name, "resume") == 0) {
        op->kind = OP_RESUME;
        op->a = num(args[0]);
    } else if (strcmp(name, "cancel") == 0) {
        op->kind = OP_CANCEL;
        op->a = num(args[0]);
    } else if (strcmp(name, "destroy") == 0) {
        op->kind = OP_DESTROY;
        op->a = num(args[0]);
    } else if (strcmp(name, "slock") == 0) {
        op->kind = OP_SLOCK;
    } else if (strcmp(name, "sunlock") == 0) {
        op->kind = OP_SUNLOCK;
    } else if (strcmp(name, "exit") == 0) {
        op->kind = OP_EXIT;
        op->a = num(args[0]);
    } else if (strcmp(name, "start") == 0) {
        op->kind = OP_START;
        op->a = num(args[0]);
    } else if (strcmp(name, "join") == 0) {
        op->kind = OP_JOIN;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "bind") == 0) {
        op->kind = OP_BIND;
        op->a = num(args[0]);
        op->b = num(args[1]);
        op->c = num(args[2]);
    } else if (strcmp(name, "unbind") == 0) {
        op->kind = OP_UNBIND;
        op->a = num(args[0]);
    } else if (strcmp(name, "tdestroy") == 0) {
        op->kind = OP_TDESTROY;
        op->a = num(args[0]);
    } else if (strcmp(name, "eset") == 0 || strcmp(name, "eclear") == 0) {
        op->kind = (name[1] == 's') ? OP_ESET : OP_ECLEAR;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "ewait") == 0) {
        op->kind = OP_EWAIT;
        op->a = num(args[0]);
        op->b = num(args[1]);
        op->c = num(args[2]) | (num(args[3]) << 1); /* all | clear << 1 */
        op->d = num(args[4]);
    } else if (strcmp(name, "edestroy") == 0) {
        op->kind = OP_EDESTROY;
        op->a = num(args[0]);
    } else if (strcmp(name, "cwait") == 0) {
        op->kind = OP_CWAIT;
        op->a = num(args[0]);
        op->b = num(args[1]);
        op->c = num(args[2]);
    } else if (strcmp(name, "csignal") == 0 || strcmp(name, "cbcast") == 0 ||
               strcmp(name, "cdestroy") == 0) {
        op->kind = (name[1] == 's') ? OP_CSIGNAL : (name[1] == 'b') ? OP_CBCAST : OP_CDESTROY;
        op->a = num(args[0]);
    } else if (strcmp(name, "qsend") == 0) {
        op->kind = OP_QSEND;
        op->a = num(args[0]);
        op->b = num(args[1]);
        op->c = num(args[2]);
    } else if (strcmp(name, "qfront") == 0) {
        op->kind = OP_QFRONT;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "qrecv") == 0) {
        op->kind = OP_QRECV;
        op->a = num(args[0]);
        op->b = num(args[1]);
    } else if (strcmp(name, "qpeek") == 0 || strcmp(name, "qdestroy") == 0) {
        op->kind = (name[1] == 'p') ? OP_QPEEK : OP_QDESTROY;
        op->a = num(args[0]);
    } else {
        die("unknown operation");
    }
}

static void parse_program(char *text, struct program *prog)
{
    char *p = text;
    prog->count = 0u;
    while (p != NULL && *p != '\0') {
        char *one = cut(&p, ';');
        if (prog->count >= MAX_OPS) {
            die("too many operations");
        }
        parse_op(one, &prog->ops[prog->count++]);
    }
}

static void parse_section(char *sec)
{
    char kind = sec[0];
    char *p = sec + 1;
    if (kind == 'T') {
        struct thread_desc *d;
        char *head;
        char *attrs;
        if (thread_count >= MAX_THREADS) {
            die("too many threads");
        }
        d = &threads[thread_count++];
        head = cut(&p, ':');
        d->tid = num(cut(&head, '='));
        attrs = head;
        d->prio = (uint8_t)num(cut(&attrs, ','));
        d->inactive = attrs != NULL && strcmp(attrs, "inactive") == 0;
        parse_program(p, &d->prog);
    } else if (kind == 'S') {
        struct sem_desc *s;
        char *attrs;
        if (sem_count >= MAX_SEMS) {
            die("too many semaphores");
        }
        s = &sems[sem_count++];
        s->sid = num(cut(&p, '='));
        attrs = p;
        s->count = num(cut(&attrs, ','));
        s->bind_tid = -1;
        while (attrs != NULL && *attrs != '\0') {
            char *a = cut(&attrs, ',');
            if (strcmp(a, "fifo") == 0) {
                s->fifo = true;
            } else if (strncmp(a, "bind=", 5) == 0) {
                char *b = a + 5;
                s->bind_tid = num(cut(&b, '.'));
                s->bind_bit = num(b);
            } else {
                die("bad semaphore attribute");
            }
        }
    } else if (kind == 'E') {
        struct evflag_desc *e;
        char *attrs;
        if (evflag_count >= MAX_EVFLAGS) {
            die("too many event flags");
        }
        e = &evflags[evflag_count++];
        e->eid = num(cut(&p, '='));
        attrs = p;
        e->bits = num(cut(&attrs, ','));
        e->bind_tid = -1;
        while (attrs != NULL && *attrs != '\0') {
            char *a = cut(&attrs, ',');
            if (strcmp(a, "fifo") == 0) {
                e->fifo = true;
            } else if (strncmp(a, "bind=", 5) == 0) {
                char *b = a + 5;
                e->bind_tid = num(cut(&b, '.'));
                e->bind_bit = num(b);
            } else {
                die("bad event flag attribute");
            }
        }
    } else if (kind == 'C') {
        struct cond_desc *c;
        if (cond_count >= MAX_CONDS) {
            die("too many condition variables");
        }
        c = &conds[cond_count++];
        c->cid = num(cut(&p, '='));
        c->fifo = num(p) != 0;
    } else if (kind == 'Q') {
        struct msgq_desc *q;
        char *attrs;
        if (msgq_count >= MAX_MSGQS) {
            die("too many message queues");
        }
        q = &msgqs[msgq_count++];
        q->qid = num(cut(&p, '='));
        attrs = p;
        q->capacity = num(cut(&attrs, ','));
        if (q->capacity < 1 || q->capacity > (long)MAX_MSGQ_CAP) {
            die("bad message queue capacity");
        }
        q->bind_tid = -1;
        while (attrs != NULL && *attrs != '\0') {
            char *a = cut(&attrs, ',');
            if (strcmp(a, "fifo") == 0) {
                q->fifo = true;
            } else if (strncmp(a, "bind=", 5) == 0) {
                char *b = a + 5;
                q->bind_tid = num(cut(&b, '.'));
                q->bind_bit = num(b);
            } else {
                die("bad message queue attribute");
            }
        }
    } else if (kind == 'M') {
        struct mutex_desc *m;
        char *attrs;
        if (mutex_count >= MAX_MUTEXES) {
            die("too many mutexes");
        }
        m = &mutexes[mutex_count++];
        m->mid = num(cut(&p, '='));
        attrs = p;
        m->protocol = num(cut(&attrs, ','));
        m->ceiling = num(cut(&attrs, ','));
        m->recursive = num(attrs) != 0;
    } else if (kind == 'I') {
        if (isr_count >= MAX_ISRS) {
            die("too many ISRs");
        }
        (void)cut(&p, ':');
        parse_program(p, &isrs[isr_count++]);
    } else {
        die("unknown section");
    }
}

static void parse_scenario(char *text)
{
    char *p = text;
    while (p != NULL && *p != '\0') {
        parse_section(cut(&p, '|'));
    }
}

static void parse_schedule(char *text)
{
    char *p = text;
    while (p != NULL && *p != '\0') {
        char *item = cut(&p, ',');
        char *at = strchr(item, '@');
        struct sched_item *it;
        if (at == NULL || sched_count >= MAX_SCHED) {
            die("bad schedule");
        }
        *at = '\0';
        it = &sched[sched_count++];
        it->at = (unsigned)num(at + 1);
        if (strcmp(item, "tick") == 0) {
            it->is_tick = 1u;
            it->index = 0u;
        } else if (strncmp(item, "isr", 3) == 0) {
            it->is_tick = 0u;
            it->index = (unsigned)num(item + 3);
        } else {
            die("bad schedule item");
        }
    }
}

/* ---- lookups ---------------------------------------------------------------------- */

static struct thread_desc *thread_by_tid(long tid)
{
    unsigned i;
    for (i = 0u; i < thread_count; i++) {
        if (threads[i].tid == tid) {
            return &threads[i];
        }
    }
    die("unknown thread id");
    return NULL;
}

static emb_sem_t sem_by_sid(long sid)
{
    unsigned i;
    for (i = 0u; i < sem_count; i++) {
        if (sems[i].sid == sid) {
            return sems[i].handle;
        }
    }
    die("unknown semaphore id");
    return sems[0].handle;
}

#if CONFIG_EMB_EVENT
static emb_event_t evflag_by_eid(long eid)
{
    unsigned i;
    for (i = 0u; i < evflag_count; i++) {
        if (evflags[i].eid == eid) {
            return evflags[i].handle;
        }
    }
    die("unknown event flags id");
    return evflags[0].handle;
}
#endif

#if CONFIG_EMB_COND
static emb_cond_t cond_by_cid(long cid)
{
    unsigned i;
    for (i = 0u; i < cond_count; i++) {
        if (conds[i].cid == cid) {
            return conds[i].handle;
        }
    }
    die("unknown condition variable id");
    return conds[0].handle;
}
#endif

#if CONFIG_EMB_MSGQ
static struct msgq_desc *msgq_by_qid(long qid)
{
    unsigned i;
    for (i = 0u; i < msgq_count; i++) {
        if (msgqs[i].qid == qid) {
            return &msgqs[i];
        }
    }
    die("unknown message queue id");
    return &msgqs[0];
}
#endif

static emb_mutex_t mutex_by_mid(long mid)
{
    unsigned i;
    for (i = 0u; i < mutex_count; i++) {
        if (mutexes[i].mid == mid) {
            return mutexes[i].handle;
        }
    }
    die("unknown mutex id");
    return mutexes[0].handle;
}

static emb_timeout_t timeout_of(long t)
{
    if (t < 0) {
        return EMB_WAIT_FOREVER;
    }
    if (t == 0) {
        return EMB_NO_WAIT;
    }
    return EMB_TIMEOUT(EMB_TICKS((emb_tick_t)t));
}

/* ---- the trace and the schedule ------------------------------------------------------ */

static void record(uint16_t id, uint8_t context, uint32_t ticks, uintptr_t a0, uintptr_t a1,
                   uintptr_t a2)
{
    unsigned n = event_count;
    if (n >= MAX_EVENTS) {
        events_lost++;
        return;
    }
    events[n].id = id;
    events[n].context = context;
    events[n].ticks = ticks;
    events[n].a0 = a0;
    events[n].a1 = a1;
    events[n].a2 = a2;
    event_count = n + 1u;
}

static void fire(const struct sched_item *it)
{
    if (it->is_tick != 0u) {
        if (tick_pending != 0u) {
            ticks_deferred++; /* never two ticks in one timer interrupt (the model ticks once) */
            return;
        }
        tick_pending = 1u;
        emb_native_tick();
    } else if (it->index < isr_count) {
        emb_native_irq_raise((emb_irq_t)(ISR_IRQ_BASE + it->index));
    } else {
        die("schedule names an ISR the scenario has not");
    }
}

/* Fires every schedule item whose event ordinal has been reached; re-entrant (a fired
 * interrupt may be delivered inside the call and record events of its own). */
static void pump_schedule(void)
{
    while (started != 0u && sched_next < sched_count && sched[sched_next].at <= event_count) {
        const struct sched_item *it = &sched[sched_next];
        sched_next++;
        fire(it);
    }
}

static void sink(const emb_trace_event_t *e, void *ctx)
{
    (void)ctx;
    record(e->id, e->context, e->ticks, e->a0, e->a1, e->a2);
    if (e->id == (uint16_t)EMB_TRACE_TICK) {
        tick_pending = 0u;
        if (ticks_deferred != 0u) {
            ticks_deferred--;
            tick_pending = 1u;
            emb_native_tick(); /* pending: delivered when this timer interrupt returns */
        }
    }
    pump_schedule();
}

/* ---- running the programs ------------------------------------------------------------ */

static void run_ops(const struct program *prog, struct thread_desc *self)
{
    unsigned i;
    for (i = 0u; i < prog->count; i++) {
        const struct op *op = &prog->ops[i];
        emb_status_t st = EMB_OK;
        unsigned long data = 0u;
        switch (op->kind) {
        case OP_TAKE:
            st = emb_sem_take(sem_by_sid(op->a), timeout_of(op->b));
            data = (st == EMB_OK) ? 1u : 0u;
            break;
        case OP_GIVE:
            st = emb_sem_give(sem_by_sid(op->a));
            break;
        case OP_LOCK:
            st = emb_mutex_lock(mutex_by_mid(op->a), timeout_of(op->b));
            if (st == EMB_EOWNERDEAD) {
                data = 2u; /* the model's OWNERDEAD_FLAG */
                st = EMB_OK;
            }
            break;
        case OP_UNLOCK:
            st = emb_mutex_unlock(mutex_by_mid(op->a));
            break;
        case OP_NSET:
            st = emb_notify_set(thread_by_tid(op->a)->handle, (emb_notify_bits_t)op->b);
            break;
        case OP_NWAIT: {
            emb_notify_bits_t got = 0u;
            uint8_t mode = (uint8_t)((op->b != 0 ? EMB_NOTIFY_ALL : EMB_NOTIFY_ANY) |
                                     (op->c != 0 ? EMB_NOTIFY_CLEAR : 0u));
            st = emb_notify_wait((emb_notify_bits_t)op->a, mode, timeout_of(op->d), &got);
            data = got;
            break;
        }
        case OP_SLEEP:
            if (op->a < 0) {
                st = emb_thread_sleep(EMB_TICKS(FOREVER_SLEEP_TICKS));
            } else {
                st = emb_thread_sleep(EMB_TICKS((emb_tick_t)op->a));
            }
            break;
        case OP_YIELD:
            emb_thread_yield();
            break;
        case OP_SETPRIO: {
            struct thread_desc *d = thread_by_tid(op->a);
            st = emb_thread_set_priority(d->handle, (uint8_t)op->b);
            /* Keep the mapping record for compatibility with historical captures. */
            record((uint16_t)EV_INDEX_MARK, (uint8_t)emb_context(), 0u, (uintptr_t)d->tid,
                   (uintptr_t)emb_thread_index(d->handle), 0u);
            break;
        }
        case OP_SUSPEND:
            st = emb_thread_suspend(thread_by_tid(op->a)->handle);
            break;
        case OP_RESUME:
            st = emb_thread_resume(thread_by_tid(op->a)->handle);
            break;
        case OP_CANCEL:
            st = emb_thread_cancel(thread_by_tid(op->a)->handle);
            break;
        case OP_DESTROY:
            st = emb_sem_destroy(sem_by_sid(op->a));
            break;
        case OP_SLOCK:
            emb_sched_lock();
            break;
        case OP_SUNLOCK:
            emb_sched_unlock();
            break;
        case OP_EXIT:
            if (self != NULL) {
                self->status[i] = EMB_OK;
                self->done = i + 1u;
            }
            emb_thread_exit((int)op->a);
            break;
        case OP_START:
            st = emb_thread_start(thread_by_tid(op->a)->handle);
            break;
        case OP_JOIN: {
            int code = 0;
            st = emb_thread_join(thread_by_tid(op->a)->handle, timeout_of(op->b), &code);
            data = (unsigned long)(long)code;
            break;
        }
        case OP_BIND:
            st = emb_sem_bind_notify(sem_by_sid(op->a), thread_by_tid(op->b)->handle,
                                     EMB_NOTIFY_BIT((unsigned)op->c));
            break;
        case OP_UNBIND:
            st = emb_sem_unbind_notify(sem_by_sid(op->a));
            break;
        case OP_TDESTROY: {
            struct thread_desc *d = thread_by_tid(op->a);
            st = emb_thread_destroy(d->handle);
            if (st == EMB_OK) {
                d->destroyed = true;
            }
            break;
        }
#if CONFIG_EMB_EVENT
        case OP_ESET:
            st = emb_event_set(evflag_by_eid(op->a), (emb_event_bits_t)op->b);
            break;
        case OP_ECLEAR:
            st = emb_event_clear(evflag_by_eid(op->a), (emb_event_bits_t)op->b);
            break;
        case OP_EWAIT: {
            emb_event_bits_t got = 0u;
            uint8_t mode = (uint8_t)(((op->c & 1) != 0 ? EMB_EVENT_ALL : EMB_EVENT_ANY) |
                                     ((op->c & 2) != 0 ? EMB_EVENT_CLEAR : 0u));
            st = emb_event_wait(evflag_by_eid(op->a), (emb_event_bits_t)op->b, mode,
                                timeout_of(op->d), &got);
            data = got;
            break;
        }
        case OP_EDESTROY:
            st = emb_event_destroy(evflag_by_eid(op->a));
            break;
#else
        case OP_ESET:
        case OP_ECLEAR:
        case OP_EWAIT:
        case OP_EDESTROY:
            die("event flags are not configured");
            break;
#endif
#if CONFIG_EMB_COND
        case OP_CWAIT:
            st = emb_cond_wait(cond_by_cid(op->a), mutex_by_mid(op->b), timeout_of(op->c));
            if (st == EMB_EOWNERDEAD) {
                data = 2u; /* the model's OWNERDEAD_FLAG, as for lock */
                st = EMB_OK;
            }
            break;
        case OP_CSIGNAL:
            st = emb_cond_signal(cond_by_cid(op->a));
            break;
        case OP_CBCAST:
            st = emb_cond_broadcast(cond_by_cid(op->a));
            break;
        case OP_CDESTROY:
            st = emb_cond_destroy(cond_by_cid(op->a));
            break;
#else
        case OP_CWAIT:
        case OP_CSIGNAL:
        case OP_CBCAST:
        case OP_CDESTROY:
            die("condition variables are not configured");
            break;
#endif
#if CONFIG_EMB_MSGQ
        case OP_QSEND: {
            uint32_t v = (uint32_t)op->b;
            st = emb_msgq_send(msgq_by_qid(op->a)->handle, &v, timeout_of(op->c));
            break;
        }
        case OP_QFRONT: {
            uint32_t v = (uint32_t)op->b;
            st = emb_msgq_send_front(msgq_by_qid(op->a)->handle, &v);
            break;
        }
        case OP_QRECV:
        case OP_QPEEK: {
            uint32_t v = 0u;
            st = (op->kind == OP_QRECV)
                     ? emb_msgq_receive(msgq_by_qid(op->a)->handle, &v, timeout_of(op->b))
                     : emb_msgq_peek(msgq_by_qid(op->a)->handle, &v);
            data = (st == EMB_OK) ? v : 0u;
            break;
        }
        case OP_QDESTROY: {
            struct msgq_desc *q = msgq_by_qid(op->a);
            st = emb_msgq_destroy(q->handle);
            if (st == EMB_OK) {
                q->destroyed = true;
            }
            break;
        }
#else
        case OP_QSEND:
        case OP_QFRONT:
        case OP_QRECV:
        case OP_QPEEK:
        case OP_QDESTROY:
            die("message queues are not configured");
            break;
#endif
        default:
            die("bad operation kind");
            break;
        }
        if (self != NULL) {
            self->status[i] = st;
            self->data[i] = data;
            self->done = i + 1u;
        }
    }
}

static void thread_entry(void *arg)
{
    run_ops(&((struct thread_desc *)arg)->prog, (struct thread_desc *)arg);
}

static void isr_body(void *arg)
{
    unsigned i = (unsigned)(uintptr_t)arg;
    record((uint16_t)EV_ISR_MARK, (uint8_t)emb_context(), 0u, (uintptr_t)i, 0u, 0u);
    run_ops(&isrs[i], NULL);
}

/* ---- the dump ------------------------------------------------------------------------ */

static void dump(const char *end, unsigned long e0, unsigned long e1)
{
    unsigned i;
    unsigned n;
    if (dumped != 0u) {
        return;
    }
    dumped = 1u;
    (void)printf("config profile=%s owner_death=%s checked=%d tickless=%d idle_index=254 events=%d "
                 "conds=%d msgqs=%d\n",
                 CONFIG_EMB_PROFILE,
#if CONFIG_EMB_MUTEX_OWNER_DEATH_FAULT && CONFIG_EMB_CHECKED
                 "fault",
#else
                 "release",
#endif
                 (int)CONFIG_EMB_CHECKED, (int)CONFIG_EMB_TICKLESS, (int)CONFIG_EMB_EVENT,
                 (int)CONFIG_EMB_COND, (int)CONFIG_EMB_MSGQ);
    n = event_count;
    for (i = 0u; i < n; i++) {
        (void)printf("ev %u %u %lu %lu %lu %lu\n", (unsigned)events[i].id,
                     (unsigned)events[i].context, (unsigned long)events[i].ticks,
                     (unsigned long)events[i].a0, (unsigned long)events[i].a1,
                     (unsigned long)events[i].a2);
    }
    if (events_lost != 0u) {
        (void)printf("events_lost %u\n", events_lost);
    }
    for (i = 0u; i < thread_count; i++) {
        unsigned k;
        uint8_t state = 0u;
        uint8_t reason = 0u;
        for (k = 0u; k < threads[i].done; k++) {
            (void)printf("res %ld %u %d %lu\n", threads[i].tid, k, (int)threads[i].status[k],
                         threads[i].data[k]);
        }
        if (!EMB_HANDLE_IS_NULL(threads[i].handle) && !threads[i].destroyed &&
            emb_thread_state(threads[i].handle, &state, &reason) == EMB_OK) {
            (void)printf("state %ld %u %u\n", threads[i].tid, (unsigned)state, (unsigned)reason);
        }
    }
    for (i = 0u; i < sem_count; i++) {
        (void)printf("sem %ld %lu\n", sems[i].sid, (unsigned long)emb_sem_count(sems[i].handle));
    }
#if CONFIG_EMB_EVENT
    for (i = 0u; i < evflag_count; i++) {
        (void)printf("evflag %ld %lu\n", evflags[i].eid,
                     (unsigned long)emb_event_get(evflags[i].handle));
    }
#endif
#if CONFIG_EMB_MSGQ
    for (i = 0u; i < msgq_count; i++) {
        const embk_msgq_t *q = (const embk_msgq_t *)(const void *)&msgqs[i].storage;
        unsigned k;
        if (msgqs[i].destroyed) {
            continue;
        }
        (void)printf("msgq %ld", msgqs[i].qid);
        for (k = 0u; k < q->count; k++) {
            uint32_t v;
            unsigned slot = (q->head + k) % q->capacity;
            (void)memcpy(&v, q->ring + ((size_t)slot * q->item_size), sizeof(v));
            (void)printf(" %lu", (unsigned long)v);
        }
        (void)printf("\n");
    }
#endif
    (void)printf("end %s %lu %lu\n", end, e0, e1);
    (void)fflush(stdout);
}

static void fault_hook(const emb_fault_info_t *info)
{
    (void)fprintf(stderr, "diff_runner: fault class %u code %u argument %u at %s\n",
                  (unsigned)info->fault_class, (unsigned)info->code, (unsigned)info->argument,
                  (info->where != NULL) ? info->where : "?");
    dump("fault", (unsigned long)info->fault_class, (unsigned long)info->code);
}

/* ---- idle: the schedule's leftovers, then the verdict --------------------------------- */

static void idle_hook(void)
{
    if (sched_next < sched_count) {
        const struct sched_item *it = &sched[sched_next];
        sched_next++;
        fire(it); /* nothing else can move the run forward */
        return;
    }
    dump("done", 0u, 0u);
    emb_native_exit(0);
}

static bool deadline_filter(uint64_t deadline_raw, uint64_t now_raw)
{
#if !CONFIG_EMB_TICKLESS
    emb_tick_t next = 0u;
    emb_irq_key_t key = embk_lock_timeout();
    bool armed = embk_timeout_next(&next);
    emb_instant_t deadline = {.ticks = next};
    emb_instant_t now;
    embk_unlock_timeout(key);
    (void)deadline_raw;
    (void)now_raw;
    now = emb_time_now();
    /* A periodic timer keeps rearming after all finite waits end. Stop virtual idle
     * ticking at that point so the same verdict/leftover-ISR policy can run in both modes. */
    return armed && (emb_instant_before(deadline, now) ||
                     (next - now.ticks) < (emb_tick_t)(FOREVER_SLEEP_TICKS / 2u));
#else
    return (deadline_raw - now_raw) < (uint64_t)(FOREVER_SLEEP_TICKS / 2u);
#endif
}

/* ---- construction ---------------------------------------------------------------------- */

static void construct(void)
{
    unsigned i;
    for (i = 0u; i < sem_count; i++) {
        emb_sem_attr_t attr;
        emb_sem_attr_default(&attr);
        attr.initial = (emb_sem_count_t)sems[i].count;
        attr.flags = (uint8_t)(EMB_OBJ_ABORT_WAITERS | (sems[i].fifo ? EMB_OBJ_FIFO : 0u));
        EMB_CHECK(emb_sem_init(&sems[i].storage, &attr, &sems[i].handle));
    }
#if CONFIG_EMB_EVENT
    for (i = 0u; i < evflag_count; i++) {
        emb_event_attr_t attr;
        emb_event_attr_default(&attr);
        attr.initial = (emb_event_bits_t)evflags[i].bits;
        attr.flags = (uint8_t)(EMB_OBJ_ABORT_WAITERS | (evflags[i].fifo ? EMB_OBJ_FIFO : 0u));
        EMB_CHECK(emb_event_init(&evflags[i].storage, &attr, &evflags[i].handle));
    }
#else
    if (evflag_count != 0u) {
        die("event flags are not configured");
    }
#endif
#if CONFIG_EMB_COND
    for (i = 0u; i < cond_count; i++) {
        emb_cond_attr_t attr;
        emb_cond_attr_default(&attr);
        attr.flags = (uint8_t)(EMB_OBJ_ABORT_WAITERS | (conds[i].fifo ? EMB_OBJ_FIFO : 0u));
        EMB_CHECK(emb_cond_init(&conds[i].storage, &attr, &conds[i].handle));
    }
#else
    if (cond_count != 0u) {
        die("condition variables are not configured");
    }
#endif
#if CONFIG_EMB_MSGQ
    for (i = 0u; i < msgq_count; i++) {
        emb_msgq_attr_t attr;
        emb_msgq_attr_default(&attr);
        attr.ring = msgqs[i].ring;
        attr.ring_size = sizeof(msgqs[i].ring);
        attr.item_size = sizeof(uint32_t);
        attr.capacity = (uint16_t)msgqs[i].capacity;
        attr.flags = (uint8_t)(EMB_OBJ_ABORT_WAITERS | (msgqs[i].fifo ? EMB_OBJ_FIFO : 0u));
        EMB_CHECK(emb_msgq_init(&msgqs[i].storage, &attr, &msgqs[i].handle));
    }
#else
    if (msgq_count != 0u) {
        die("message queues are not configured");
    }
#endif
    for (i = 0u; i < mutex_count; i++) {
        emb_mutex_attr_t attr;
        emb_mutex_attr_default(&attr);
        attr.protocol = (uint8_t)mutexes[i].protocol;
        attr.ceiling = (uint8_t)mutexes[i].ceiling;
        attr.flags = (uint8_t)(mutexes[i].recursive ? EMB_MUTEX_RECURSIVE : 0u);
        EMB_CHECK(emb_mutex_init(&mutexes[i].storage, &attr, &mutexes[i].handle));
    }
    for (i = 0u; i < thread_count; i++) {
        emb_thread_attr_t attr;
        emb_thread_attr_default(&attr);
        attr.name = "diff";
        attr.stack = stacks[i];
        attr.stack_size = STACK_SIZE;
        attr.priority = threads[i].prio;
        EMB_CHECK(emb_thread_init(&threads[i].storage, &attr, thread_entry, &threads[i],
                                  &threads[i].handle));
    }
    for (i = 0u; i < sem_count; i++) {
        if (sems[i].bind_tid >= 0) {
            EMB_CHECK(emb_sem_bind_notify(sems[i].handle, thread_by_tid(sems[i].bind_tid)->handle,
                                          EMB_NOTIFY_BIT(sems[i].bind_bit)));
        }
    }
#if CONFIG_EMB_EVENT
    for (i = 0u; i < evflag_count; i++) {
        if (evflags[i].bind_tid >= 0) {
            EMB_CHECK(emb_event_bind_notify(evflags[i].handle,
                                            thread_by_tid(evflags[i].bind_tid)->handle,
                                            EMB_NOTIFY_BIT(evflags[i].bind_bit)));
        }
    }
#endif
#if CONFIG_EMB_MSGQ
    for (i = 0u; i < msgq_count; i++) {
        if (msgqs[i].bind_tid >= 0) {
            EMB_CHECK(emb_msgq_bind_notify(msgqs[i].handle,
                                           thread_by_tid(msgqs[i].bind_tid)->handle,
                                           EMB_NOTIFY_BIT(msgqs[i].bind_bit)));
        }
    }
#endif
    for (i = 0u; i < isr_count; i++) {
        EMB_CHECK(emb_irq_connect((emb_irq_t)(ISR_IRQ_BASE + i), isr_body, (void *)(uintptr_t)i));
    }
    for (i = 0u; i < thread_count; i++) {
        if (!threads[i].inactive) {
            EMB_CHECK(emb_thread_start(threads[i].handle));
        }
    }
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 2) {
        die("usage: diff_runner '<scenario>' [--schedule '<items>']");
    }
    parse_scenario(argv[1]);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--schedule") == 0 && i + 1 < argc) {
            parse_schedule(argv[i + 1]);
            i++;
        } else {
            die("unknown argument");
        }
    }
    emb_board_init();
    emb_fault_set_hook(fault_hook);
    emb_native_set_idle_hook(idle_hook);
    emb_native_set_deadline_filter(deadline_filter);
    emb_trace_set_sink(sink, NULL);
    emb_kernel_init();
    construct();
    for (i = 0; (unsigned)i < thread_count; i++) {
        (void)printf("thread %ld index %u\n", threads[i].tid,
                     (unsigned)emb_thread_index(threads[i].handle));
    }
    started = 1u;
    emb_kernel_start();
}
