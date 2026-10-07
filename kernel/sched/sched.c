/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Fixed-priority preemptive scheduler with FIFO within a level (03 §2, KRN-SCH-001
 * to 008, 041). Base profile: one FIFO list per priority under a bitmap of
 * EMBK_WORD_BITS-bit words with a summary word when more than one is needed. Tiny
 * profile (ADR-036): a table of one thread per priority and a one-word ready set.
 *
 * With the table, priorities are unique at the base level but not at the effective
 * level (inheritance and ceilings raise an owner to a blocked waiter's level, and two
 * ceilings may coincide), so the highest ready thread is found by its bit only while
 * no thread is raised; otherwise the ready set is scanned, bounded by
 * CONFIG_EMB_PRIORITY_COUNT. The count of raised threads keeps the fast path.
 *
 * Lock: every function here runs with the scheduler domain held.
 */
#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/thread.h>

embk_thread_t embk_idle_thread;

#if CONFIG_EMB_SCHED_TABLE
embk_thread_t *embk_thread_table[CONFIG_EMB_PRIORITY_COUNT];
static embk_word_t ready_set;    /* bit i: embk_thread_table[i] is READY */
uint8_t embk_sched_raised_count; /* threads whose eff_prio != base_prio (lock: sched) */
#else
#define READY_WORDS ((CONFIG_EMB_PRIORITY_COUNT + EMBK_WORD_BITS - 1u) / EMBK_WORD_BITS)
static embk_list_t ready_lists[CONFIG_EMB_PRIORITY_COUNT];
static embk_word_t ready_map[READY_WORDS];
#if READY_WORDS > 1
static embk_word_t ready_summary; /* bit w: ready_map[w] != 0 */
#endif
#endif

static bool thread_runnable(const embk_thread_t *t)
{
    return t->wait_state != EMBK_WAIT_BLOCKED &&
           (t->tflags & (EMBK_THREAD_SUSPENDED | EMBK_THREAD_TERMINATED)) == 0u &&
           (t->tflags & EMBK_THREAD_STARTED) != 0u;
}

void embk_sched_init(void)
{
#if CONFIG_EMB_SCHED_TABLE
    unsigned i;
    for (i = 0u; i < (unsigned)CONFIG_EMB_PRIORITY_COUNT; i++) {
        embk_thread_table[i] = NULL;
    }
    ready_set = 0u;
    embk_sched_raised_count = 0u;
#else
    unsigned i;
    for (i = 0u; i < (unsigned)CONFIG_EMB_PRIORITY_COUNT; i++) {
        embk_list_init(&ready_lists[i]);
    }
    for (i = 0u; i < READY_WORDS; i++) {
        ready_map[i] = 0u;
    }
#if READY_WORDS > 1
    ready_summary = 0u;
#endif
#endif
    embk_thread_init_idle(&embk_idle_thread, NULL, 0u);
}

/* ---- the structure ------------------------------------------------------------ */

static void insert(embk_thread_t *t, bool ahead)
{
#if CONFIG_EMB_SCHED_TABLE
    (void)ahead; /* one thread per level: no order within a level */
    EMBK_ASSERT(embk_thread_table[t->base_prio] == t);
    ready_set |= EMBK_WORD_BIT(t->base_prio);
#else
    unsigned p = t->eff_prio;
    if (ahead) {
        embk_list_prepend(&ready_lists[p], &t->ready_node);
    } else {
        embk_list_append(&ready_lists[p], &t->ready_node);
    }
    ready_map[p / EMBK_WORD_BITS] |= EMBK_WORD_BIT(p % EMBK_WORD_BITS);
#if READY_WORDS > 1
    ready_summary |= EMBK_WORD_BIT(p / EMBK_WORD_BITS);
#endif
#endif
}

void embk_sched_remove_ready(embk_thread_t *t)
{
#if CONFIG_EMB_SCHED_TABLE
    ready_set &= (embk_word_t)~EMBK_WORD_BIT(t->base_prio);
#else
    unsigned p = t->eff_prio;
    EMBK_ASSERT(embk_list_node_is_linked(&t->ready_node));
    embk_list_remove(&t->ready_node);
    if (embk_list_is_empty(&ready_lists[p])) {
        ready_map[p / EMBK_WORD_BITS] &= (embk_word_t)~EMBK_WORD_BIT(p % EMBK_WORD_BITS);
#if READY_WORDS > 1
        if (ready_map[p / EMBK_WORD_BITS] == 0u) {
            ready_summary &= (embk_word_t)~EMBK_WORD_BIT(p / EMBK_WORD_BITS);
        }
#endif
    }
#endif
}

bool embk_sched_is_ready(const embk_thread_t *t)
{
#if CONFIG_EMB_SCHED_TABLE
    return (ready_set & EMBK_WORD_BIT(t->base_prio)) != 0u && embk_thread_table[t->base_prio] == t;
#else
    return embk_list_node_is_linked(&t->ready_node);
#endif
}

embk_thread_t *embk_sched_peek(void)
{
#if CONFIG_EMB_SCHED_TABLE
    embk_word_t set = ready_set;
    embk_thread_t *best;
    if (set == 0u) {
        return NULL;
    }
    best = embk_thread_table[embk_word_highest(set)];
    if (embk_sched_raised_count != 0u) {
        /* bounded by CONFIG_EMB_PRIORITY_COUNT: find the highest effective priority */
        embk_word_t rest = set & (embk_word_t)~EMBK_WORD_BIT(best->base_prio);
        while (rest != 0u) {
            unsigned i = embk_word_highest(rest);
            embk_thread_t *c = embk_thread_table[i];
            if (c->eff_prio > best->eff_prio) {
                best = c;
            }
            rest &= (embk_word_t)~EMBK_WORD_BIT(i);
        }
    }
    return best;
#else
    unsigned w;
    unsigned p;
#if READY_WORDS > 1
    if (ready_summary == 0u) {
        return NULL;
    }
    w = embk_word_highest(ready_summary);
#else
    if (ready_map[0] == 0u) {
        return NULL;
    }
    w = 0u;
#endif
    p = (w * EMBK_WORD_BITS) + embk_word_highest(ready_map[w]);
    return EMB_CONTAINER_OF(embk_list_first(&ready_lists[p]), embk_thread_t, ready_node);
#endif
}

bool embk_sched_should_preempt(uint8_t prio)
{
    const embk_thread_t *cur = embk_cpu.current;
    if (cur == NULL) {
        return false; /* prekernel: the start launches the highest */
    }
    if (cur == &embk_idle_thread || !thread_runnable(cur)) {
        return true;
    }
    return prio > cur->eff_prio;
}

void embk_sched_make_ready(embk_thread_t *t, bool ahead)
{
    EMBK_ASSERT(!embk_sched_is_ready(t));
    EMBK_ASSERT(t != &embk_idle_thread);
    insert(t, ahead);
    if (embk_sched_should_preempt(t->eff_prio)) {
        embk_cpu.reschedule_pending = 1u;
        if (embk_in_isr()) {
            emb_arch_reschedule_pend(); /* SPEC-002 §6.2 step 2 */
        }
        EMBK_TRACE(EMB_TRACE_RESCHED_PEND, embk_thread_index(t), 0u, 0u);
    }
}

bool embk_sched_current_runnable(void)
{
    const embk_thread_t *cur = embk_cpu.current;
    return cur != &embk_idle_thread && thread_runnable(cur);
}

/* ---- the decision --------------------------------------------------------------- */

static void check_stack(const embk_thread_t *t)
{
#if CONFIG_EMB_STACK_CHECK
    unsigned i;
    if (t->stack_base != NULL) {
        /* bounded: EMBK_STACK_GUARD_BYTES */
        for (i = 0u; i < EMBK_STACK_GUARD_BYTES; i++) {
            if (t->stack_base[i] != EMB_STACK_FILL) {
                embk_fault_raise(EMB_FAULT_STACK_OVERFLOW, embk_thread_index(t), 0u, "stack guard");
            }
        }
    }
    if (!emb_arch_stack_check(t)) {
        embk_fault_raise(EMB_FAULT_STACK_OVERFLOW, embk_thread_index(t), 1u, "arch stack check");
    }
#else
    (void)t;
#endif
}

embk_thread_t *embk_sched_select(bool yield)
{
    embk_thread_t *cur = embk_cpu.current;
    embk_thread_t *next = embk_sched_peek();
    bool runnable = cur != &embk_idle_thread && thread_runnable(cur);

    embk_cpu.reschedule_pending = 0u;
    if (runnable) {
        if (next == NULL || next->eff_prio < cur->eff_prio ||
            (next->eff_prio == cur->eff_prio && !yield)) {
            return NULL; /* KRN-SCH-004: nothing changed the decision */
        }
        insert(cur, !yield); /* KRN-SCH-041: a preempted thread goes ahead of its peers */
    }
    if (next != NULL) {
        embk_sched_remove_ready(next);
    } else {
        next = &embk_idle_thread;
    }
    if (next == cur) {
        return NULL;
    }
    check_stack(cur);
    EMBK_TRACE(EMB_TRACE_SWITCH, embk_thread_index(cur), embk_thread_index(next), 0u);
#if CONFIG_EMB_CHECKED
    /* the depth belongs to the context: a thread that blocked inside its section resumes
     * at 1, a preempted one at 0, a fresh one at 0 */
    cur->lock_depth = embk_cpu.irq_lock_depth;
    embk_cpu.irq_lock_depth = next->lock_depth;
#endif
    embk_cpu.current = next;
    return next;
}

void embk_sched_reschedule_if_needed(void)
{
    embk_thread_t *next;
    if (embk_cpu.reschedule_pending == 0u || embk_cpu.sched_lock_depth != 0u || embk_in_isr() ||
        embk_cpu.kernel_state != EMBK_KERNEL_RUNNING) {
        return;
    }
    next = embk_sched_select(false);
    if (next != NULL) {
        emb_arch_switch_to(next); /* P2 */
    }
}

void embk_sched_switch_away(void)
{
    embk_thread_t *next = embk_sched_select(false);
    EMBK_ASSERT(next != NULL);
    if (next != NULL) {
        emb_arch_switch_to(next);
    }
}

void embk_sched_switch_final(void)
{
    embk_thread_t *next = embk_sched_select(false);
    EMBK_ASSERT(next != NULL);
    emb_arch_switch_final(next);
}

void embk_sched_yield_locked(void)
{
    embk_thread_t *next;
    if (embk_cpu.sched_lock_depth != 0u) {
        embk_cpu.reschedule_pending =
            1u; /* SPEC-002 §5: a no-op that keeps the reschedule pending */
        return;
    }
    next = embk_sched_select(true);
    if (next != NULL) {
        emb_arch_switch_to(next);
    }
}

void embk_sched_prio_changed(embk_thread_t *t, uint8_t old_eff)
{
#if CONFIG_EMB_SCHED_TABLE
    bool was_raised = old_eff != t->base_prio;
    bool is_raised = t->eff_prio != t->base_prio;
    if (was_raised != is_raised) {
        if (is_raised) {
            embk_sched_raised_count++;
        } else {
            embk_sched_raised_count--;
        }
    }
#else
    if (embk_sched_is_ready(t)) {
        /* unlink from the old level, relink behind the peers of the new one (SPEC-005 §2.1) */
        unsigned p = old_eff;
        embk_list_remove(&t->ready_node);
        if (embk_list_is_empty(&ready_lists[p])) {
            ready_map[p / EMBK_WORD_BITS] &= (embk_word_t)~EMBK_WORD_BIT(p % EMBK_WORD_BITS);
#if READY_WORDS > 1
            if (ready_map[p / EMBK_WORD_BITS] == 0u) {
                ready_summary &= (embk_word_t)~EMBK_WORD_BIT(p / EMBK_WORD_BITS);
            }
#endif
        }
        insert(t, false);
    }
#endif
    if (t == embk_cpu.current) {
        const embk_thread_t *top = embk_sched_peek();
        if (top != NULL && top->eff_prio > t->eff_prio) {
            embk_cpu.reschedule_pending = 1u;
        }
    }
}
