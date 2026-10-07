/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The v0.1 §41.3 first-execution sequence as a conformance test (M1 exit criterion):
 * two threads hand over with notifications, a third wakes on the timer every tick,
 * for a large number of switches; every hand-over and every tick is accounted for.
 */
#include <emb_test.h>

#define PING   EMB_NOTIFY_BIT(0)
#define ROUNDS 200000u

static emb_thread_t ta;
static emb_thread_t tb;
static uint32_t count_a;
static uint32_t count_b;
static uint32_t ticks_seen;
static uint32_t stop_timer;

static void task_a(void *arg)
{
    (void)arg;
    while (count_a < ROUNDS) {
        EMB_ASSERT_OK(
            emb_notify_wait(PING, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, NULL));
        count_a++;
        if ((count_a % 100u) == 0u) {
            EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(1)));
        }
        EMB_ASSERT_OK(emb_notify_set(tb, PING));
    }
}

static void task_b(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_notify_set(ta, PING));
    while (count_b < ROUNDS) {
        EMB_ASSERT_OK(
            emb_notify_wait(PING, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, NULL));
        count_b++;
        if (count_b < ROUNDS) {
            EMB_ASSERT_OK(emb_notify_set(ta, PING));
        }
    }
}

static void task_timer(void *arg)
{
    emb_instant_t next = emb_time_now();
    (void)arg;
    while (stop_timer == 0u) {
        next = emb_instant_add(next, EMB_TICKS(1));
        EMB_ASSERT_OK(emb_thread_sleep_until(next));
        ticks_seen++;
    }
}

EMB_TEST(first_execution_sequence_repeats_reliably)
{
    uint32_t t0 = EMB_TEST_NOW();
    emb_thread_t tt;
    EMB_TEST_REQ("KRN-SCH-001", "KRN-SCH-004", "KRN-IRQ-010", "KRN-TIM-010", "KRN-NOTIF-003");
    count_a = 0u;
    count_b = 0u;
    ticks_seen = 0u;
    stop_timer = 0u;
    emb_sched_lock(); /* both handles exist before either thread runs */
    ta = emb_test_thread("A", 2u, 0u, task_a, NULL);
    tb = emb_test_thread("B", 3u, 0u, task_b, NULL);
    tt = emb_test_thread("T", 4u, 0u, task_timer, NULL);
    emb_sched_unlock();
    EMB_ASSERT_OK(emb_thread_join(ta, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_thread_join(tb, EMB_WAIT_FOREVER, NULL));
    stop_timer = 1u;
    EMB_ASSERT_OK(emb_thread_join(tt, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_EQ(count_a, ROUNDS);
    EMB_ASSERT_EQ(count_b, ROUNDS);
    EMB_ASSERT_EQ(EMB_TEST_NOW() - t0,
                  ROUNDS / 100u + 1u); /* one tick per hundred rounds, plus the stop */
    EMB_ASSERT_EQ(ticks_seen, EMB_TEST_NOW() - t0);
}
