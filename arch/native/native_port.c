/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Native port (SPEC-013), POSIX backend. One host thread per EmbLinkRTOS thread behind
 * a gate: a mutex held by whichever host thread is running simulated code, plus one
 * condition variable per thread. Interrupts are events delivered at gate crossings
 * (deterministic mode); time is virtual and advances only in idle. The kernel's
 * scheduling decisions are therefore exact and every run is reproducible (SIM-004).
 *
 * Asynchronous mode (§5, A93), for stress runs: an injector host thread sends SIGUSR2 to
 * the host thread holding the gate every period, and the handler delivers a tick (and,
 * when asked, one more interrupt) at whatever statement that thread is on. It defers to
 * the next gate crossing when the thread is masked, already in a handler, or in host code
 * (the gate's own operations, or a section the board marks). Not reproducible.
 *
 * Host usage is confined to this file's embh_* wrappers (§3) and the diagnostic
 * printing of the fault path.
 */
#include <emb/arch.h>
#include <emb/irq.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include <emb_native.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- host layer (SPEC-013 §3) ----------------------------------------------------- */

typedef pthread_mutex_t embh_mutex_t;
typedef pthread_cond_t embh_cond_t;
typedef pthread_t embh_thread_t;

static void embh_mutex_init(embh_mutex_t *m)
{
    (void)pthread_mutex_init(m, NULL);
}

static void embh_mutex_lock(embh_mutex_t *m)
{
    (void)pthread_mutex_lock(m);
}

static void embh_mutex_unlock(embh_mutex_t *m)
{
    (void)pthread_mutex_unlock(m);
}

static void embh_cond_init(embh_cond_t *c)
{
    (void)pthread_cond_init(c, NULL);
}

static void embh_cond_destroy(embh_cond_t *c)
{
    (void)pthread_cond_destroy(c);
}

static void embh_cond_wait(embh_cond_t *c, embh_mutex_t *m)
{
    (void)pthread_cond_wait(c, m);
}

static void embh_cond_signal(embh_cond_t *c)
{
    (void)pthread_cond_signal(c);
}

static bool embh_thread_create(embh_thread_t *out, void *(*fn)(void *), void *arg, size_t stack)
{
    pthread_attr_t attr;
    int rc;
    (void)pthread_attr_init(&attr);
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (stack != 0u) {
        (void)pthread_attr_setstacksize(&attr, stack);
    }
    rc = pthread_create(out, &attr, fn, arg);
    (void)pthread_attr_destroy(&attr);
    return rc == 0;
}

static uint64_t embh_clock_now_ns(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000u) + (uint64_t)ts.tv_nsec;
}

/* ---- port state ------------------------------------------------------------------ */

typedef struct embn_thread {
    embh_cond_t cv;
    embh_thread_t host;
    unsigned run;        /* this host thread may proceed */
    unsigned created;    /* host thread exists */
    unsigned saved_mask; /* the gate mask when the thread was switched out */
    emb_thread_entry_t entry;
    void *arg;
} embn_thread_t;

EMB_STATIC_ASSERT(sizeof(embn_thread_t) <= EMB_ARCH_TCB_EXTENSION,
                  "TCB extension too small for the host state");

#define EXT(t) ((embn_thread_t *)(void *)(((emb_arch_tcb_t *)(void *)(t))->ext))

static struct embn_state {
    embh_mutex_t gate;
    embk_thread_t *running;   /* the port's view of the running thread */
    volatile unsigned masked; /* kernel-aware interrupts masked (the critical section) */
    volatile unsigned in_isr; /* a handler is running: no nested delivery in this milestone */
    unsigned started;
    uint8_t enabled[EMB_ARCH_IRQ_COUNT];
    uint8_t pending[EMB_ARCH_IRQ_COUNT];
    unsigned pending_count;
    uint64_t now_raw;
    uint64_t deadline_raw;
    unsigned deadline_set;
    unsigned periodic;
    uint64_t period_raw;
    uint64_t switches;
    void (*idle_hook)(void);
    bool (*deadline_filter)(uint64_t deadline_raw, uint64_t now_raw);
} g;

static void port_switch(embk_thread_t *next);

/* ---- asynchronous mode state (§5, A93) --------------------------------------------- */

/* The host thread running simulated code, as an integer, or 0 between two of them: the
 * injector signals it, and the handler acts only on it. */
static _Atomic uintptr_t async_holder;
static atomic_bool async_on;
static atomic_int async_host_depth; /* the holder is in host or port code: defer */
static atomic_int async_missed;     /* deferred: delivered at the next crossing */
static unsigned async_period_us;
static emb_irq_t async_irq = EMB_ARCH_IRQ_COUNT; /* also raised every fourth signal, if set */
static unsigned async_count;
static atomic_ullong async_delivered;
static atomic_ullong async_deferred;

static void holder_set(bool running)
{
    atomic_store(&async_holder, running ? (uintptr_t)pthread_self() : (uintptr_t)0);
}

/* ---- interrupts and delivery (§5) ------------------------------------------------- */

static void run_isr(emb_irq_t irq)
{
    unsigned saved = g.masked;
    embk_thread_t *next;
    g.masked = 1u; /* handlers run with kernel-aware interrupts masked */
    g.in_isr = 1u;
    embk_isr_enter();
    if (irq == EMB_NATIVE_IRQ_TIMER) {
        embk_time_timer_isr();
        if (g.periodic != 0u) {
            g.deadline_raw = g.now_raw + g.period_raw;
            g.deadline_set = 1u;
        }
    } else {
#if CONFIG_EMB_IRQ_DYNAMIC
        embk_isr_dispatch(irq);
#endif
    }
    next = embk_isr_exit();
    g.in_isr = 0u;
    if (next != NULL) {
        port_switch(next); /* P1: resumes here when this thread runs again */
    }
    g.masked = saved;
}

static void deliver_pending(void)
{
    /* bounded by the number of pending interrupts plus those they raise */
    while (g.masked == 0u && g.in_isr == 0u && g.pending_count != 0u) {
        unsigned i;
        emb_irq_t found = EMB_ARCH_IRQ_COUNT;
        atomic_fetch_add(&async_host_depth, 1); /* the table is the port's: no signal inside */
        for (i = 0u; i < (unsigned)EMB_ARCH_IRQ_COUNT; i++) { /* lowest number first: the timer */
            if (g.pending[i] != 0u && g.enabled[i] != 0u) {
                found = (emb_irq_t)i;
                break;
            }
        }
        if (found != EMB_ARCH_IRQ_COUNT) {
            g.pending[found] = 0u;
            g.pending_count--;
        }
        atomic_fetch_sub(&async_host_depth, 1);
        if (found == EMB_ARCH_IRQ_COUNT) {
            return; /* pending but disabled */
        }
        run_isr(found);
    }
}

emb_irq_key_t embn_irq_lock(void)
{
    emb_irq_key_t key = g.masked;
    g.masked = 1u;
    return key;
}

static void async_inject(void);

void embn_irq_unlock(emb_irq_key_t key)
{
    g.masked = key;
    if (key == 0u) {
#if CONFIG_EMB_NATIVE_INVARIANTS
        if (g.started != 0u && g.in_isr == 0u) {
            embk_invariants_check(); /* after the operation, at the crossing (SPEC-013 §8) */
        }
#endif
        if (atomic_exchange(&async_missed, 0) != 0) {
            async_inject(); /* a signal the thread could not take then */
        }
        deliver_pending(); /* a gate crossing */
    }
}

bool embn_irq_locked(void)
{
    return g.masked != 0u;
}

void emb_native_irq_raise(emb_irq_t irq)
{
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return;
    }
    atomic_fetch_add(&async_host_depth, 1); /* the table is the port's: no signal inside */
    if (g.pending[irq] == 0u) {
        g.pending[irq] = 1u;
        g.pending_count++;
    }
    atomic_fetch_sub(&async_host_depth, 1);
    deliver_pending();
}

void emb_native_yield_point(void)
{
    deliver_pending();
}

bool emb_arch_in_isr(void)
{
    return g.in_isr != 0u;
}

emb_status_t emb_arch_irq_enable(emb_irq_t irq)
{
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return EMB_EINVAL;
    }
    g.enabled[irq] = 1u;
    deliver_pending();
    return EMB_OK;
}

emb_status_t emb_arch_irq_disable(emb_irq_t irq)
{
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return EMB_EINVAL;
    }
    g.enabled[irq] = 0u;
    return EMB_OK;
}

bool emb_arch_irq_is_enabled(emb_irq_t irq)
{
    return irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT && g.enabled[irq] != 0u;
}

emb_status_t emb_arch_irq_set_level(emb_irq_t irq, uint8_t level)
{
    /* One configurable kernel-aware level; timer-first delivery is unchanged. */
    return (irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT && level == 0u) ? EMB_OK : EMB_EINVAL;
}

emb_status_t emb_arch_irq_clear_pending(emb_irq_t irq)
{
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return EMB_EINVAL;
    }
    atomic_fetch_add(&async_host_depth, 1);
    if (g.pending[irq] != 0u) {
        g.pending[irq] = 0u;
        g.pending_count--;
    }
    atomic_fetch_sub(&async_host_depth, 1);
    return EMB_OK;
}

emb_status_t emb_arch_irq_pend_soft(emb_irq_t irq)
{
    if (irq >= (emb_irq_t)EMB_ARCH_IRQ_COUNT) {
        return EMB_EINVAL;
    }
    emb_native_irq_raise(irq);
    return EMB_OK;
}

/* ---- threads and the gate (§4) ------------------------------------------------------ */

static void *trampoline(void *arg)
{
    embk_thread_t *t = (embk_thread_t *)arg;
    embn_thread_t *x = EXT(t);
    embh_mutex_lock(&g.gate);
    while (x->run == 0u) {
        embh_cond_wait(&x->cv, &g.gate);
    }
    holder_set(true);
    g.masked = 0u; /* a fresh context starts with interrupts enabled */
    deliver_pending();
    embk_thread_launch(x->entry, x->arg);
    return NULL; /* unreachable */
}

static void start_host(embk_thread_t *t)
{
    embn_thread_t *x = EXT(t);
    x->run = 1u;
    x->created = 1u;
    if (!embh_thread_create(&x->host, trampoline, t, (size_t)CONFIG_EMB_NATIVE_HOST_STACK)) {
        (void)fprintf(stderr, "emb native: host thread creation failed\n");
        emb_native_exit(EMB_NATIVE_EXIT_FAULT);
    }
}

static void wake_host(embk_thread_t *t)
{
    embn_thread_t *x = EXT(t);
    if (x->created == 0u) {
        start_host(t);
    } else {
        x->run = 1u;
        embh_cond_signal(&x->cv);
    }
}

static void port_switch(embk_thread_t *next)
{
    embk_thread_t *self = g.running;
    embn_thread_t *x = EXT(self);
    g.switches++;
    holder_set(false); /* host code from here: no asynchronous delivery */
    x->saved_mask = g.masked;
    x->run = 0u;
    g.running = next;
    wake_host(next);
    while (x->run == 0u) {
        embh_cond_wait(&x->cv, &g.gate); /* parked: the gate is released while waiting */
    }
    g.masked = x->saved_mask;
    holder_set(true);
}

void emb_arch_context_init(embk_thread_t *t, void *stack_base, size_t stack_size,
                           emb_thread_entry_t entry, void *arg, bool privileged)
{
    embn_thread_t *x = EXT(t);
    (void)stack_base; /* host stacks are unrelated to the configured sizes (§4) */
    (void)stack_size;
    (void)privileged;
    embh_cond_init(&x->cv);
    x->run = 0u;
    x->created = 0u;
    x->saved_mask = 0u;
    x->entry = entry;
    x->arg = arg;
}

void emb_arch_switch_to(embk_thread_t *next)
{
    port_switch(next);
}

void emb_arch_switch_final(embk_thread_t *next)
{
    embk_thread_t *self = g.running;
    embn_thread_t *x = EXT(self);
    g.switches++;
    holder_set(false);
    g.running = next;
    wake_host(next);
    embh_cond_destroy(&x->cv);
    x->created = 0u;
    x->run = 0u;
    embh_mutex_unlock(&g.gate); /* the only switch-out completion this port has (§4) */
    pthread_exit(NULL);
}

void emb_arch_reschedule_pend(void)
{
    /* the flag is enough: the ISR epilogue (run_isr) asks the kernel at exit */
}

bool emb_arch_switch_out_done(const embk_thread_t *t)
{
    (void)t;
    return true; /* another thread runs only after the gate was released */
}

/* ---- startup (§4 of SPEC-011) ------------------------------------------------------- */

void emb_arch_early_init(void)
{
}

void emb_arch_init(void)
{
    unsigned i;
    embh_mutex_init(&g.gate);
    embh_mutex_lock(&g.gate); /* the pre-kernel context holds the gate from here on */
    g.running = NULL;
    g.masked = 0u;
    for (i = 0u; i < (unsigned)EMB_ARCH_IRQ_COUNT; i++) {
        g.enabled[i] = 1u; /* enabled unless a test disables them */
        g.pending[i] = 0u;
    }
    g.enabled[EMB_NATIVE_IRQ_TIMER] = 1u;
}

void emb_arch_kernel_start(embk_thread_t *first, embk_thread_t *idle)
{
    embn_thread_t *ix = EXT(idle);
    /* the host main thread becomes the idle context (§4) */
    if (ix->created == 0u) {
        embh_cond_init(&ix->cv);
    }
    ix->host = pthread_self();
    ix->created = 1u;
    ix->run = 1u;
    g.running = idle;
    g.started = 1u;
    g.masked =
        1u; /* the kernel holds its section across the launch; the first thread starts unmasked */
    if (first != idle) {
        port_switch(first);
    }
    /* resumed as the idle context */
    holder_set(true);
    g.masked = 0u;
    if (getenv("EMB_NATIVE_ASYNC_US") != NULL) {
        emb_native_async_start((unsigned)strtoul(getenv("EMB_NATIVE_ASYNC_US"), NULL, 10),
                               EMB_ARCH_IRQ_COUNT);
    }
    embk_thread_launch(ix->entry, ix->arg);
}

/* ---- idle and virtual time (§6) --------------------------------------------------------- */

void emb_arch_idle(void)
{
    deliver_pending();
    if (g.pending_count != 0u) {
        return;
    }
    if (atomic_load(&async_on)) {
        /* the next signal brings the time: wait for it, interruptible (not host code) */
        struct timespec ts = {0, 200000};
        (void)nanosleep(&ts, NULL);
        return;
    }
    if (g.deadline_set != 0u &&
        (g.deadline_filter == NULL || g.deadline_filter(g.deadline_raw, g.now_raw))) {
        if (g.deadline_raw > g.now_raw) {
            g.now_raw = g.deadline_raw; /* time jumps to the next deadline */
        }
        g.deadline_set = 0u;
        emb_native_irq_raise(EMB_NATIVE_IRQ_TIMER);
        return;
    }
    if (g.idle_hook != NULL) {
        g.idle_hook();
        return;
    }
    (void)fprintf(stderr,
                  "emb native: nothing runnable, no pending interrupt, no deadline (SIM-005)\n");
    emb_native_exit(EMB_NATIVE_EXIT_DEADLOCK);
}

void emb_native_set_idle_hook(void (*hook)(void))
{
    g.idle_hook = hook;
}

uint64_t emb_native_virtual_now(void)
{
    return g.now_raw;
}

void emb_native_set_deadline_filter(bool (*filter)(uint64_t deadline_raw, uint64_t now_raw))
{
    g.deadline_filter = filter;
}

void emb_native_tick(void)
{
    atomic_fetch_add(&async_host_depth, 1); /* the clock is the port's: no signal inside */
    g.now_raw += 1u;                        /* raw units are ticks (emb_arch_timer_hz) */
    if (g.deadline_set != 0u && g.deadline_raw <= g.now_raw) {
        g.deadline_set = 0u;
    }
    atomic_fetch_sub(&async_host_depth, 1);
    emb_native_irq_raise(EMB_NATIVE_IRQ_TIMER);
}

uint64_t emb_native_switch_count(void)
{
    return g.switches;
}

/* ---- asynchronous mode (§5, A93) ------------------------------------------------------ */

/* One asynchronous event, on the holder, unmasked, outside host code. */
static void async_inject(void)
{
    async_count++;
    if (async_irq < (emb_irq_t)EMB_ARCH_IRQ_COUNT && (async_count % 4u) == 0u &&
        g.pending[async_irq] == 0u) {
        g.pending[async_irq] = 1u;
        g.pending_count++;
    }
    emb_native_tick(); /* the tick, delivered at once: we are unmasked */
}

static void async_signal(int sig)
{
    int saved = errno;
    (void)sig;
    if (atomic_load(&async_holder) != (uintptr_t)pthread_self() ||
        atomic_load(&async_host_depth) != 0 || g.masked != 0u || g.in_isr != 0u) {
        atomic_store(&async_missed, 1); /* taken at the next crossing, as a masked one is */
        atomic_fetch_add(&async_deferred, 1u);
    } else {
        atomic_fetch_add(&async_delivered, 1u);
        async_inject(); /* may switch: this handler then resumes when the thread runs again */
    }
    errno = saved;
}

static void *async_injector(void *arg)
{
    unsigned seed = 12345u;
    (void)arg;
    while (atomic_load(&async_on)) {
        uintptr_t h;
        struct timespec ts;
        /* bounded by the run: a period with jitter, so the signal lands anywhere */
        seed = seed * 1103515245u + 12345u;
        ts.tv_sec = 0;
        ts.tv_nsec = (long)(async_period_us / 2u + (seed >> 16) % (async_period_us + 1u)) * 1000L;
        (void)nanosleep(&ts, NULL);
        h = atomic_load(&async_holder);
        if (h != 0u) {
            (void)pthread_kill((pthread_t)h, SIGUSR2);
        }
    }
    return NULL;
}

void emb_native_async_start(unsigned period_us, emb_irq_t also_raise)
{
    struct sigaction sa;
    pthread_t t;
    if (atomic_load(&async_on) || period_us == 0u) {
        return;
    }
    async_period_us = period_us;
    async_irq = also_raise;
    (void)memset(&sa, 0, sizeof(sa));
    sa.sa_handler = async_signal;
    sa.sa_flags = SA_RESTART;
    (void)sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGUSR2, &sa, NULL);
    atomic_store(&async_on, true);
    atomic_fetch_add(&async_host_depth, 1);
    if (!embh_thread_create(&t, async_injector, NULL, 0u)) {
        (void)fprintf(stderr, "emb native: the asynchronous injector failed to start\n");
        atomic_store(&async_on, false);
    }
    atomic_fetch_sub(&async_host_depth, 1);
}

void emb_native_async_stop(void)
{
    atomic_store(&async_on, false); /* the injector ends after its current period */
}

void emb_native_async_stats(uint64_t *delivered, uint64_t *deferred)
{
    *delivered = (uint64_t)atomic_load(&async_delivered);
    *deferred = (uint64_t)atomic_load(&async_deferred);
}

void emb_native_host_begin(void)
{
    atomic_fetch_add(&async_host_depth, 1);
}

void emb_native_host_end(void)
{
    atomic_fetch_sub(&async_host_depth, 1);
}

emb_status_t emb_arch_timer_init(void)
{
    g.deadline_set = 0u;
    g.periodic = 0u;
    return EMB_OK;
}

void emb_arch_timer_start_periodic(uint32_t hz)
{
    uint32_t raw_hz = emb_arch_timer_hz();
    g.period_raw = (hz == 0u) ? 1u : (raw_hz / hz);
    if (g.period_raw == 0u) {
        g.period_raw = 1u;
    }
    g.periodic = 1u;
    g.deadline_raw = g.now_raw + g.period_raw;
    g.deadline_set = 1u;
}

emb_arch_timer_raw_t emb_arch_timer_now_raw(void)
{
    return g.now_raw;
}

void emb_arch_timer_set_deadline_raw(emb_arch_timer_raw_t when)
{
    g.deadline_raw = when;
    g.deadline_set = 1u;
    if (when <= g.now_raw) {
        g.deadline_set = 0u;
        emb_native_irq_raise(EMB_NATIVE_IRQ_TIMER); /* already due */
    }
}

void emb_arch_timer_cancel(void)
{
    g.deadline_set = 0u;
}

uint32_t emb_arch_timer_hz(void)
{
    return (uint32_t)(1000000000u / (uint32_t)CONFIG_EMB_TICK_NS); /* one raw unit per tick */
}

emb_tick_t emb_arch_timer_max_ticks(void)
{
    return (emb_tick_t)UINT32_MAX;
}

uint32_t emb_arch_timer_set_latency_ticks(void)
{
    return 0u;
}

emb_cycle_t emb_arch_cycles(void)
{
    return (emb_cycle_t)embh_clock_now_ns();
}

uint32_t emb_arch_cycles_hz(void)
{
    return 1000000000u;
}

/* ---- stacks, faults, debug ---------------------------------------------------------------- */

uint8_t *emb_arch_isr_stack(size_t *size)
{
    *size = 0u; /* host stacks: interrupts run on the host thread's own (A90) */
    return NULL;
}

bool emb_arch_stack_check(const embk_thread_t *t)
{
    (void)t;
    return true; /* host stacks: no guard check on this port (§4) */
}

void emb_arch_stack_limit_set(void *limit)
{
    (void)limit;
}

static void report_fault(const emb_fault_info_t *info, const char *action)
{
    if (info == NULL) {
        (void)fprintf(stderr, "emb native: kernel fault, %s\n", action);
        return;
    }
    (void)fprintf(
        stderr,
        "emb native: kernel fault class %u code %u argument %u thread %u context %u at %s, %s\n",
        (unsigned)info->fault_class, (unsigned)info->code, (unsigned)info->argument,
        (unsigned)info->thread_index, (unsigned)info->context,
        (info->where != NULL) ? info->where : "?", action);
}

void emb_arch_fault_halt(const emb_fault_info_t *info)
{
    report_fault(info, "halting");
    emb_native_exit(EMB_NATIVE_EXIT_FAULT);
}

void emb_arch_reset(const emb_fault_info_t *info)
{
    report_fault(info, "reset requested");
    emb_native_exit(EMB_NATIVE_EXIT_FAULT);
}

/* A process cannot reset itself: it ends, and the next run is the next boot. */
void emb_arch_system_reset(void)
{
    emb_native_exit(EMB_NATIVE_EXIT_FAULT);
}

#if CONFIG_EMB_CRASH_RECORD
/* SPEC-015 §9: on the native port the record goes to a host file, $EMB_NATIVE_CRASH_FILE,
 * so that it outlives the process; without the variable it lives in process memory only. */
static uint8_t retained[192];

uint8_t *emb_arch_retained(uint16_t *size)
{
    *size = (uint16_t)sizeof(retained);
    return retained;
}

void emb_arch_retained_sync(bool store)
{
    const char *path = getenv("EMB_NATIVE_CRASH_FILE");
    FILE *f;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    f = fopen(path, store ? "wb" : "rb");
    if (f == NULL) {
        if (!store) {
            memset(retained, 0, sizeof(retained)); /* no file: nothing survived */
        }
        return;
    }
    if (store) {
        (void)fwrite(retained, 1u, sizeof(retained), f);
    } else if (fread(retained, 1u, sizeof(retained), f) != sizeof(retained)) {
        memset(retained, 0, sizeof(retained));
    }
    (void)fclose(f);
}

/* No hardware fault entry on this port: nothing to add to the record. */
uint8_t emb_arch_crash_regs(const emb_fault_info_t *info, uint32_t *regs, uint8_t max,
                            uint8_t *status_count, uint32_t *lr)
{
    (void)info;
    (void)regs;
    (void)max;
    *status_count = 0u;
    *lr = 0u;
    return 0u;
}
#endif

void emb_arch_breakpoint(void)
{
}

#ifdef EMB_NATIVE_COVERAGE
void __gcov_dump(void); /* the gcov runtime: _exit() skips its exit-time flush */
#endif

void emb_native_exit(int code)
{
    (void)fflush(NULL);
#ifdef EMB_NATIVE_COVERAGE
    __gcov_dump();
#endif
    _exit(code);
}
