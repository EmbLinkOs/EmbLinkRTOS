/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Software timer conformance (SPEC-003 §7; KRN-TIM-012 to 016, 028). */
#include <emb_test.h>

#if CONFIG_EMB_ARCH_NATIVE && CONFIG_EMB_TICKLESS
#include <emb_native.h>
#endif

static emb_timer_storage_t tm_storage;
static emb_timer_t tm;
#if CONFIG_EMB_WORKQ
static emb_workq_storage_t wq_storage;
EMB_THREAD_STACK(wq_stack, EMB_TEST_STACK_SIZE);
static emb_workq_t wq;
#endif

#define MAX_AT 8u
static uint32_t at[MAX_AT]; /* when each callback ran, relative to t_base */
static unsigned runs;
static uint32_t t_base;
static uint32_t sleep_in_callback;
static unsigned stop_after; /* the callback stops its own timer after this many runs */
static emb_context_t context_seen;

static void callback(emb_timer_t timer, void *arg)
{
    (void)arg;
    if (runs < MAX_AT) {
        at[runs] = EMB_TEST_NOW() - t_base;
    }
    runs++;
    context_seen = emb_context();
    emb_test_mark((int)runs);
    if (stop_after != 0u && runs == stop_after) {
        emb_timer_state_t st = EMB_TIMER_IDLE;
        EMB_ASSERT_OK(emb_timer_stop(timer, &st));
        EMB_ASSERT_EQ(st, EMB_TIMER_RUNNING); /* from its own callback */
    }
    if (sleep_in_callback != 0u) {
        EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(sleep_in_callback)));
    }
}

static void clear_runs(void)
{
    runs = 0u;
    sleep_in_callback = 0u;
    stop_after = 0u;
    t_base = EMB_TEST_NOW();
}

/* The instant a test starts its timer; on a real-time port a tick may pass between this
 * read and the start, so absolute instants are checked as n or n + 1 ticks. */
#define BEGIN()  (t_base = EMB_TEST_NOW())
#define AT(k, n) emb_test_check_elapsed(__LINE__, at[(k)], (n))

static void make(uint8_t flags)
{
    emb_timer_attr_t attr;
    clear_runs();
    emb_timer_attr_default(&attr);
    attr.fn = callback;
    attr.flags = flags;
#if CONFIG_EMB_WORKQ
    if ((flags & EMB_TIMER_ISR_CONTEXT) == 0u) {
        emb_workq_attr_t qa;
        emb_workq_attr_default(&qa);
        qa.stack = wq_stack;
        qa.stack_size = sizeof(wq_stack);
        qa.priority = 2u; /* above the runner: a callback runs as soon as it is queued */
        EMB_ASSERT_OK(emb_workq_init(&wq_storage, &qa, &wq));
        attr.workq = wq;
    }
#endif
    EMB_ASSERT_OK(emb_timer_init(&tm_storage, &attr, &tm));
}

static void teardown(uint8_t flags)
{
    EMB_ASSERT_OK(emb_timer_stop_sync(tm, EMB_WAIT_FOREVER));
    EMB_ASSERT_OK(emb_timer_destroy(tm));
#if CONFIG_EMB_WORKQ
    if ((flags & EMB_TIMER_ISR_CONTEXT) == 0u) {
        EMB_ASSERT_OK(emb_workq_destroy(wq));
    }
#endif
    (void)flags;
}

EMB_TEST(timer_isr_context_one_shot_and_periodic_without_drift)
{
    uint32_t exp = 0u;
    uint32_t over = 9u;
    EMB_TEST_REQ("KRN-TIM-025", "KRN-TIM-005", "KRN-TIM-014", "KRN-TIM-015");
    make(EMB_TIMER_ISR_CONTEXT);
    BEGIN();
    EMB_ASSERT_OK(emb_timer_start(tm, EMB_TICKS(3)));
    EMB_ASSERT_TRUE(emb_timer_is_running(tm));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(5)));
    EMB_ASSERT_MARKS(1);
    AT(0, 3u);
    EMB_ASSERT_EQ(context_seen, EMB_CONTEXT_ISR); /* in the expiry interrupt (KRN-TIM-005) */
    EMB_ASSERT_TRUE(!emb_timer_is_running(tm));   /* one-shot: done */
    clear_runs();
    stop_after = 4u;
    BEGIN();
    EMB_ASSERT_OK(emb_timer_start_periodic(tm, EMB_TICKS(2), EMB_TICKS(3)));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(20)));
    EMB_ASSERT_MARKS(1, 1, 2, 3, 4);  /* after the one-shot's; the fourth stopped it */
    AT(0, 2u);                        /* first + k * period: 2, 5, 8, 11 */
    EMB_ASSERT_EQ(at[1] - at[0], 3u); /* the callbacks run in the tick: spacing is exact */
    EMB_ASSERT_EQ(at[2] - at[0], 6u);
    EMB_ASSERT_EQ(at[3] - at[0], 9u);
    EMB_ASSERT_OK(emb_timer_get_counts(tm, &exp, &over));
    EMB_ASSERT_EQ(exp, 5u); /* the one-shot and four periodic expiries */
    EMB_ASSERT_EQ(over, 0u);
    teardown(EMB_TIMER_ISR_CONTEXT);
}

EMB_TEST(timer_restart_remaining_and_stop_states)
{
    emb_duration_t rem = {0u};
    emb_timer_state_t st = EMB_TIMER_RUNNING;
    EMB_TEST_REQ("KRN-TIM-025", "KRN-TIM-028");
    make(EMB_TIMER_ISR_CONTEXT);
    EMB_ASSERT_STATUS(emb_timer_restart(tm), EMB_ESTATE); /* never started */
    EMB_ASSERT_OK(emb_timer_stop(tm, &st));
    EMB_ASSERT_EQ(st, EMB_TIMER_IDLE);
    EMB_ASSERT_OK(emb_timer_start(tm, EMB_TICKS(10)));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(4)));
    EMB_ASSERT_OK(emb_timer_remaining(tm, &rem));
    emb_test_check_elapsed(__LINE__, 10u - (uint32_t)rem.ticks, 4u); /* 6 left */
    EMB_ASSERT_OK(emb_timer_restart(tm));                            /* the same delay, from now */
    EMB_ASSERT_OK(emb_timer_remaining(tm, &rem));
    emb_test_check_elapsed(__LINE__, 10u - (uint32_t)rem.ticks, 0u); /* 10 again */
    EMB_ASSERT_MISUSE(emb_timer_destroy(tm), EMB_EBUSY);             /* armed */
    EMB_ASSERT_OK(emb_timer_stop(tm, &st));
    EMB_ASSERT_EQ(st, EMB_TIMER_PENDING);
    EMB_ASSERT_OK(emb_timer_remaining(tm, &rem));
    EMB_ASSERT_EQ(rem.ticks, 0u);
    EMB_ASSERT_OK(emb_timer_start_at(tm, emb_instant_add(emb_time_now(), EMB_TICKS(2))));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(3)));
    EMB_ASSERT_MARKS(1);
    EMB_ASSERT_MISUSE(emb_timer_start_periodic(tm, EMB_TICKS(1), EMB_TICKS(0)), EMB_EINVAL);
    teardown(EMB_TIMER_ISR_CONTEXT);
}

#if CONFIG_EMB_WORKQ
EMB_TEST(timer_callback_runs_on_its_work_queue)
{
    EMB_TEST_REQ("KRN-TIM-013", "KRN-TIM-005", "KRN-TIM-026");
    make(0u);
    BEGIN();
    EMB_ASSERT_OK(emb_timer_start(tm, EMB_TICKS(2)));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(3)));
    EMB_ASSERT_MARKS(1);
    AT(0, 2u); /* the queue outranks the runner: it runs at the expiry */
    EMB_ASSERT_EQ(context_seen, EMB_CONTEXT_THREAD);     /* on the queue's thread */
    EMB_ASSERT_MISUSE(emb_workq_destroy(wq), EMB_EBUSY); /* a timer names the queue */
    teardown(0u);
}

EMB_TEST(timer_stop_sync_waits_for_a_running_callback)
{
    emb_timer_state_t st = EMB_TIMER_IDLE;
    EMB_TEST_REQ("KRN-TIM-028", "KRN-WAIT-012");
    make(0u);
    sleep_in_callback = 5u;
    BEGIN();
    EMB_ASSERT_OK(emb_timer_start(tm, EMB_TICKS(1)));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(2))); /* the callback started and sleeps */
    EMB_ASSERT_MARKS(1);
    EMB_ASSERT_OK(emb_timer_stop(tm, &st));
    EMB_ASSERT_EQ(st, EMB_TIMER_RUNNING);
    EMB_ASSERT_STATUS(emb_timer_stop_sync(tm, EMB_NO_WAIT), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_timer_stop_sync(tm, EMB_WAIT_FOREVER));
    EMB_ASSERT_TRUE(EMB_TEST_NOW() - t_base >= 6u); /* after the callback returned */
    EMB_ASSERT_OK(emb_timer_destroy(tm));
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}

EMB_TEST(timer_expiries_during_a_long_callback_are_overruns)
{
    uint32_t exp = 0u;
    uint32_t over = 0u;
    EMB_TEST_REQ("KRN-TIM-027");
    make(0u);
    sleep_in_callback = 2u;
    EMB_ASSERT_OK(emb_timer_start_periodic(tm, EMB_TICKS(1), EMB_TICKS(1)));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(6)));
    EMB_ASSERT_OK(emb_timer_stop_sync(tm, EMB_WAIT_FOREVER));
    EMB_ASSERT_OK(emb_timer_get_counts(tm, &exp, &over));
    /* each expiry started a run, was an overrun, or was dropped by the stop */
    EMB_ASSERT_TRUE(runs + over <= exp);
    EMB_ASSERT_TRUE(over >= 2u);
#if CONFIG_EMB_ARCH_NATIVE
    /* virtual time: runs at 1, 3 and 5 sleep two ticks each; the expiries at 3 and 5 find
     * a rerun already due, and the rerun queued at 6 is dropped by the stop */
    EMB_ASSERT_EQ(exp, 6u);
    EMB_ASSERT_EQ(runs, 3u);
    EMB_ASSERT_EQ(over, 2u);
#endif
    EMB_ASSERT_OK(emb_timer_destroy(tm));
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}
#endif

EMB_TEST(timer_init_needs_a_queue_or_isr_context)
{
    emb_timer_attr_t attr;
    emb_timer_t other;
    EMB_TEST_REQ("KRN-TIM-013", "API-015");
    emb_timer_attr_default(&attr);
    EMB_ASSERT_MISUSE(emb_timer_init(&tm_storage, &attr, &other), EMB_EINVAL); /* no callback */
    attr.fn = callback;
    EMB_ASSERT_STATUS(emb_timer_init(&tm_storage, &attr, &other), EMB_ENOTSUP); /* no queue */
    attr.flags = 0x80u;
    EMB_ASSERT_MISUSE(emb_timer_init(&tm_storage, &attr, &other), EMB_EINVAL); /* unknown flag */
}

#if CONFIG_EMB_ARCH_NATIVE && CONFIG_EMB_TICKLESS
EMB_TEST(timer_missed_periods_are_skipped_in_closed_form)
{
    uint32_t exp = 0u;
    uint32_t over = 0u;
    emb_duration_t rem = {0u};
    emb_irq_key_t key;
    unsigned i;
    EMB_TEST_REQ("KRN-TIM-015", "KRN-TIM-027");
    make(EMB_TIMER_ISR_CONTEXT);
    EMB_ASSERT_OK(emb_timer_start_periodic(tm, EMB_TICKS(2), EMB_TICKS(2)));
    key = emb_irq_lock(); /* the clock runs on while the expiry interrupt waits */
    for (i = 0u; i < 9u; i++) {
        emb_native_tick();
    }
    emb_irq_unlock(key); /* one pass at 9: due at 2, then 4, 6 and 8 missed */
    EMB_ASSERT_MARKS(1);
    EMB_ASSERT_OK(emb_timer_get_counts(tm, &exp, &over));
    EMB_ASSERT_EQ(exp, 1u);
    EMB_ASSERT_EQ(over, 3u);
    EMB_ASSERT_OK(emb_timer_remaining(tm, &rem));
    EMB_ASSERT_EQ(rem.ticks, 1u); /* re-armed at 10: still on the 2 + 2k grid */
    teardown(EMB_TIMER_ISR_CONTEXT);
}
#endif

static void defined_callback(emb_timer_t timer, void *arg)
{
    (void)timer;
    emb_test_mark((int)(intptr_t)arg);
}

EMB_TIMER_DEFINE(defined_timer, defined_callback, (void *)6);

EMB_TEST(timer_define_constructs_in_init_table)
{
    EMB_TEST_REQ("API-021", "KRN-OBJ-009");
    EMB_ASSERT_TRUE(!emb_timer_is_running(defined_timer)); /* constructed stopped */
    EMB_ASSERT_OK(emb_timer_start(defined_timer, EMB_TICKS(1)));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(2)));
    EMB_ASSERT_MARKS(6);
}
