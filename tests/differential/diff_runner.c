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
 *   I<index>:<op>;<op>...                     an ISR body (give, resume, nset, start only)
 * Operations (t: -1 forever, 0 no wait, n ticks):
 *   take s t | give s | lock m t | unlock m | nset tid bits | nwait mask all clear t |
 *   sleep t | yield | setprio tid prio | suspend tid | resume tid | cancel tid | destroy s |
 *   slock | sunlock | exit code | start tid | join tid t
 * A schedule entry fires right after the n-th trace event (or in idle, if that event
 * never comes): "isr<i>@n" raises ISR i, "tick@n" advances virtual time by one tick.
 */
#include <emb/emb.h>

#include <stdio.h>
#include <stdlib.h>

#include <emb_board.h>
#include <emb_native.h>
#include <string.h>

#define MAX_THREADS  8u
#define MAX_OPS      16u
#define MAX_SEMS     4u
#define MAX_MUTEXES  4u
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
    OP_JOIN
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
    const char *args[4] = {NULL, NULL, NULL, NULL};
    unsigned n = 0u;
    while (p != NULL && n < 4u) {
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
            /* on the tiny profile the index is the base priority (SPEC-008 §3) */
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
    (void)printf("config profile=%s owner_death=%s checked=%d\n", CONFIG_EMB_PROFILE,
#if CONFIG_EMB_MUTEX_OWNER_DEATH_FAULT && CONFIG_EMB_CHECKED
                 "fault",
#else
                 "release",
#endif
                 (int)CONFIG_EMB_CHECKED);
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
        if (!EMB_HANDLE_IS_NULL(threads[i].handle) &&
            emb_thread_state(threads[i].handle, &state, &reason) == EMB_OK) {
            (void)printf("state %ld %u %u\n", threads[i].tid, (unsigned)state, (unsigned)reason);
        }
    }
    for (i = 0u; i < sem_count; i++) {
        (void)printf("sem %ld %lu\n", sems[i].sid, (unsigned long)emb_sem_count(sems[i].handle));
    }
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
    return (deadline_raw - now_raw) < (uint64_t)(FOREVER_SLEEP_TICKS / 2u);
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
