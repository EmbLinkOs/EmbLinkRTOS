/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The kernel clock, the timeout list, and the timer hooks (SPEC-003 §3 to §5).
 * Periodic mode counts the port's tick interrupt; tickless mode re-bases the clock
 * from the port's free-running counter and programs the next deadline only. The
 * clock is read with a sequence lock where the port says so (EMB_ARCH_CLOCK_SEQLOCK)
 * and inside the critical section otherwise (AVR, SPEC-003 §3.1).
 */
#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/time.h>
#include <embk/wait.h>

typedef struct embk_time_state {
    emb_tick_t
        ticks; /* lock: timeout domain for writes; sequence lock or critical section for reads */
#if EMB_ARCH_CLOCK_SEQLOCK
    uint32_t seq; /* odd while a write is in progress */
#endif
#if CONFIG_EMB_SCHED_TABLE
    embk_timeout_t *head; /* the earliest armed deadline, NULL when none; lock: timeout domain */
#else
    embk_list_t timeouts; /* ordered by deadline; lock: timeout domain */
#endif
#if CONFIG_EMB_TICKLESS
    emb_arch_timer_raw_t last_raw; /* counter value at which ticks was last exact */
    uint32_t raw_per_tick;
    uint32_t latency_ticks;
#endif
} embk_time_state_t;

static embk_time_state_t ts;

/* ---- clock ---------------------------------------------------------------------- */

static EMB_INLINE void clock_write_begin(void)
{
#if EMB_ARCH_CLOCK_SEQLOCK
    ts.seq++;
    EMB_COMPILER_BARRIER();
#endif
}

static EMB_INLINE void clock_write_end(void)
{
#if EMB_ARCH_CLOCK_SEQLOCK
    EMB_COMPILER_BARRIER();
    ts.seq++;
#endif
}

emb_tick_t embk_time_ticks_locked(void)
{
#if CONFIG_EMB_TICKLESS
    emb_arch_timer_raw_t d = emb_arch_timer_now_raw() - ts.last_raw;
    return ts.ticks + (emb_tick_t)(d / ts.raw_per_tick);
#else
    return ts.ticks;
#endif
}

emb_instant_t emb_time_now(void)
{
    emb_instant_t r;
#if EMB_ARCH_CLOCK_SEQLOCK
    uint32_t s1;
    uint32_t s2;
    emb_tick_t t;
#if CONFIG_EMB_TICKLESS
    emb_arch_timer_raw_t base;
#endif
    /* bounded in practice by one timer update during the read (SPEC-003 §3.1) */
    do {
        s1 = ts.seq;
        EMB_COMPILER_BARRIER();
        t = ts.ticks;
#if CONFIG_EMB_TICKLESS
        base = ts.last_raw;
#endif
        EMB_COMPILER_BARRIER();
        s2 = ts.seq;
    } while (s1 != s2 || (s1 & 1u) != 0u);
#if CONFIG_EMB_TICKLESS
    if (ts.raw_per_tick != 0u) {
        emb_arch_timer_raw_t d = emb_arch_timer_now_raw() - base;
        t += (emb_tick_t)(d / ts.raw_per_tick);
    }
#endif
    r.ticks = t;
#else
    emb_irq_key_t key = embk_irq_lock();
    r.ticks = embk_time_ticks_locked();
    embk_irq_unlock(key);
#endif
    return r;
}

/* ---- timeout list (SPEC-003 §5.1) --------------------------------------------------- */

/* The two representations behind one interface: a doubly linked list (base profile,
 * O(1) removal) or a singly linked one bounded by the table's thread count (tiny
 * profile, SPEC-012 §13): there a node's next is NULL when not armed and the last
 * node points to itself. */
#if CONFIG_EMB_SCHED_TABLE
static EMB_INLINE embk_timeout_t *tl_first(void)
{
    return ts.head;
}

static EMB_INLINE embk_timeout_t *tl_after(const embk_timeout_t *n)
{
    return (n->next == n) ? NULL : n->next;
}

static EMB_INLINE bool tl_is_linked(const embk_timeout_t *n)
{
    return n->next != NULL;
}

/* Insert in deadline order, behind equal deadlines (they expire in arming order). */
static void tl_insert(embk_timeout_t *node)
{
    embk_timeout_t *prev = NULL;
    embk_timeout_t *p = ts.head;
    /* bounded by the armed nodes, at most the table's thread count */
    while (p != NULL && !embk_tick_before(node->deadline, p->deadline)) {
        prev = p;
        p = tl_after(p);
    }
    node->next = (p == NULL) ? node : p;
    if (prev == NULL) {
        ts.head = node;
    } else {
        prev->next = node;
    }
}

static void tl_remove(embk_timeout_t *node)
{
    embk_timeout_t *after = tl_after(node);
    if (ts.head == node) {
        ts.head = after;
    } else {
        embk_timeout_t *p = ts.head;
        /* bounded by the armed nodes; node is one of them */
        while (tl_after(p) != node) {
            p = tl_after(p);
        }
        p->next = (after == NULL) ? p : after;
    }
    node->next = NULL;
}
#else
static EMB_INLINE embk_timeout_t *tl_first(void)
{
    embk_list_node_t *n = embk_list_first(&ts.timeouts);
    return (n == NULL) ? NULL : EMB_CONTAINER_OF(n, embk_timeout_t, node);
}

static EMB_INLINE bool tl_is_linked(const embk_timeout_t *n)
{
    return embk_list_node_is_linked(&n->node);
}

static void tl_insert(embk_timeout_t *node)
{
    /* from the tail: a new deadline is most often later than most; bounded by the armed nodes */
    embk_list_node_t *p = embk_list_last(&ts.timeouts);
    while (p != NULL &&
           embk_tick_before(node->deadline, EMB_CONTAINER_OF(p, embk_timeout_t, node)->deadline)) {
        p = embk_list_prev(&ts.timeouts, p);
    }
    if (p == NULL) {
        embk_list_prepend(&ts.timeouts, &node->node);
    } else {
        embk_list_insert_after(p, &node->node); /* equal deadlines expire in arming order */
    }
}

static EMB_INLINE void tl_remove(embk_timeout_t *node)
{
    embk_list_remove(&node->node);
}
#endif

void embk_timeout_arm(embk_timeout_t *node, emb_tick_t deadline)
{
    EMBK_ASSERT(!tl_is_linked(node));
    node->deadline = deadline;
    tl_insert(node);
    EMBK_TRACE(EMB_TRACE_TIMEOUT_ARM,
               embk_thread_index(EMB_CONTAINER_OF(node, embk_thread_t, timeout)),
               (uint32_t)deadline, 0u);
#if CONFIG_EMB_TICKLESS
    if (tl_first() == node) {
        embk_time_program();
    }
#endif
}

void embk_timeout_disarm(embk_timeout_t *node)
{
#if CONFIG_EMB_TICKLESS
    bool was_head;
#endif
    if (!tl_is_linked(node)) {
        return;
    }
#if CONFIG_EMB_TICKLESS
    was_head = tl_first() == node;
#endif
    tl_remove(node);
#if CONFIG_EMB_TICKLESS
    if (was_head) {
        embk_time_program();
    }
#endif
}

bool embk_timeout_is_armed(const embk_timeout_t *node)
{
    return tl_is_linked(node);
}

bool embk_timeout_next(emb_tick_t *out_deadline)
{
    const embk_timeout_t *n = tl_first();
    if (n == NULL) {
        return false;
    }
    *out_deadline = n->deadline;
    return true;
}

/* Expire every node whose deadline is at or before @now, in deadline order (§5.3). */
static void expire(emb_tick_t now)
{
    embk_timeout_t *node;
    /* bounded by the number of expired nodes */
    while ((node = tl_first()) != NULL) {
        if (embk_tick_before(now, node->deadline)) {
            break;
        }
        tl_remove(node);
        embk_wait_wake_timeout(EMB_CONTAINER_OF(node, embk_thread_t, timeout));
    }
}

#if CONFIG_EMB_TICKLESS
/* Re-base the clock from the free-running counter (§4.1); lock held. */
static void update(void)
{
    emb_arch_timer_raw_t raw = emb_arch_timer_now_raw();
    emb_arch_timer_raw_t d = raw - ts.last_raw;
    emb_arch_timer_raw_t n = d / ts.raw_per_tick;
    if (n != 0u) {
        clock_write_begin();
        ts.ticks += (emb_tick_t)n;
        ts.last_raw += n * ts.raw_per_tick;
        clock_write_end();
    }
}
#endif

void embk_time_program(void)
{
#if CONFIG_EMB_TICKLESS
    emb_tick_t next;
    emb_tick_t delta;
    emb_tick_t max = emb_arch_timer_max_ticks();
    if (!embk_timeout_next(&next)) {
        emb_arch_timer_cancel();
        return;
    }
    delta = embk_tick_before(next, ts.ticks) ? 0u : (emb_tick_t)(next - ts.ticks);
    if (delta > max) {
        delta = max; /* reached in hops (§4.2) */
    }
    if (delta > ts.latency_ticks) {
        delta -= ts.latency_ticks; /* KRN-TIM-036 */
    } else {
        delta = 0u;
    }
    emb_arch_timer_set_deadline_raw(ts.last_raw + ((emb_arch_timer_raw_t)delta * ts.raw_per_tick));
#endif
}

void embk_time_init(void)
{
    ts.ticks = 0u;
#if EMB_ARCH_CLOCK_SEQLOCK
    ts.seq = 0u;
#endif
#if CONFIG_EMB_SCHED_TABLE
    ts.head = NULL;
#else
    embk_list_init(&ts.timeouts);
#endif
}

void embk_time_start(void)
{
    (void)emb_arch_timer_init();
#if CONFIG_EMB_TICKLESS
    ts.raw_per_tick =
        (uint32_t)(((uint64_t)emb_arch_timer_hz() * (uint64_t)CONFIG_EMB_TICK_NS) / 1000000000u);
    EMBK_ASSERT(ts.raw_per_tick != 0u);
    ts.latency_ticks = emb_arch_timer_set_latency_ticks();
    ts.last_raw = emb_arch_timer_now_raw();
    embk_time_program();
#else
    emb_arch_timer_start_periodic((uint32_t)(1000000000u / (uint32_t)CONFIG_EMB_TICK_NS));
#endif
}

void embk_time_timer_isr(void)
{
    emb_irq_key_t key = embk_lock_timeout();
#if CONFIG_EMB_TICKLESS
    update();
#else
    clock_write_begin();
    ts.ticks++;
    clock_write_end();
#endif
    expire(ts.ticks);
    embk_time_program();
    embk_unlock_timeout(key);
}

void embk_time_on_wake(void)
{
#if CONFIG_EMB_TICKLESS
    emb_irq_key_t key = embk_lock_timeout();
    update();
    expire(ts.ticks);
    embk_time_program();
    embk_unlock_timeout(key);
#endif
}

/* ---- public conversions (SPEC-001 §7.3) ---------------------------------------------- */

emb_timeout_t emb_timeout_from_duration(emb_duration_t d)
{
    emb_timeout_t t;
    t.ticks = (d.ticks > EMB_TIMEOUT_MAX_TICKS) ? EMB_TIMEOUT_MAX_TICKS : d.ticks;
    return t;
}

emb_instant_t emb_instant_add(emb_instant_t t, emb_duration_t d)
{
    emb_instant_t r;
#if CONFIG_EMB_TICK_32BIT
    r.ticks = t.ticks + d.ticks; /* the 32-bit instant space is circular (SPEC-003 §5.2) */
#else
    r.ticks = (t.ticks > (EMB_TICK_MAX - d.ticks)) ? EMB_TICK_MAX : (t.ticks + d.ticks);
#endif
    return r;
}

emb_duration_t emb_instant_sub(emb_instant_t later, emb_instant_t earlier)
{
    emb_duration_t r;
    r.ticks = embk_tick_before(later.ticks, earlier.ticks) ? 0u : (later.ticks - earlier.ticks);
    return r;
}

bool emb_instant_before(emb_instant_t a, emb_instant_t b)
{
    return embk_tick_before(a.ticks, b.ticks);
}

uint64_t emb_duration_to_ns(emb_duration_t d)
{
    return emb_mul_sat_u64((uint64_t)d.ticks, (uint64_t)CONFIG_EMB_TICK_NS);
}

bool emb_duration_is_saturated(emb_duration_t d)
{
    return d.ticks == EMB_TICK_MAX;
}
