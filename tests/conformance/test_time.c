/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Time conformance (SPEC-001 §7, SPEC-003; KRN-TIM-001 to 036, API-020 to 024). */
#include <emb_test.h>

EMB_TEST(time_sleep_is_exact_in_virtual_time)
{
    uint32_t t0 = EMB_TEST_NOW();
    EMB_TEST_REQ("KRN-TIM-001", "KRN-TIM-010", "API-023");
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(7)));
    EMB_ASSERT_EQ(EMB_TEST_NOW() - t0, 7u);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(3)));
    EMB_ASSERT_EQ(EMB_TEST_NOW() - t0, 7u + EMB_MS_TO_TICKS(3));
}

EMB_TEST(time_sleep_until_does_not_drift)
{
    emb_instant_t next = emb_time_now();
    uint32_t t0 = EMB_TEST_NOW();
    unsigned i;
    EMB_TEST_REQ("KRN-TIM-011");
    for (i = 0u; i < 10u; i++) {
        next = emb_instant_add(next, EMB_TICKS(3));
        EMB_ASSERT_OK(emb_thread_sleep_until(next));
        EMB_ASSERT_EQ(EMB_TEST_NOW() - t0, 3u * (i + 1u));
    }
}

EMB_TEST(time_zero_sleep_and_past_deadline_yield)
{
    uint32_t t0 = EMB_TEST_NOW();
    emb_instant_t past;
    EMB_TEST_REQ("KRN-TIM-010", "API-024");
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(0)));
    past.ticks = (t0 > 0u) ? (t0 - 1u) : 0u;
    EMB_ASSERT_OK(emb_thread_sleep_until(past));
    EMB_ASSERT_EQ(EMB_TEST_NOW(), t0);
}

static void sleep_and_mark(void *arg)
{
    uint32_t n = (uint32_t)(uintptr_t)arg;
    uint32_t t0 = EMB_TEST_NOW();
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(n)));
    emb_test_mark((int)n);
    EMB_ASSERT_EQ(EMB_TEST_NOW() - t0, n);
}

EMB_TEST(time_timeouts_expire_in_deadline_order)
{
    EMB_TEST_REQ("KRN-TIM-020", "KRN-TIM-021");
    (void)emb_test_thread("s5", 2u, 0u, sleep_and_mark, (void *)5);
    (void)emb_test_thread("s3", 3u, 0u, sleep_and_mark, (void *)3);
    (void)emb_test_thread("s9", 4u, 0u, sleep_and_mark, (void *)9);
    (void)emb_test_thread("s1", 5u, 0u, sleep_and_mark, (void *)1);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(12)));
    EMB_ASSERT_MARKS(1, 3, 5, 9);
}

EMB_TEST(time_conversions_round_up_and_saturate)
{
    emb_duration_t d;
    EMB_TEST_REQ("API-021", "API-022");
    EMB_ASSERT_EQ(EMB_NS_TO_TICKS(1), 1u);
    EMB_ASSERT_EQ(EMB_NS_TO_TICKS(CONFIG_EMB_TICK_NS), 1u);
    EMB_ASSERT_EQ(EMB_NS_TO_TICKS(CONFIG_EMB_TICK_NS + 1u), 2u);
    EMB_ASSERT_EQ(EMB_MS_TO_TICKS(1000), EMB_SEC_TO_TICKS(1));
    EMB_ASSERT_EQ(EMB_NS_TO_TICKS(0), 0u);
    d = EMB_SEC(UINT64_C(100000000000)); /* far beyond any tick type */
    EMB_ASSERT_TRUE(emb_duration_is_saturated(d));
    EMB_ASSERT_TRUE(!emb_duration_is_saturated(EMB_MS(5)));
    EMB_ASSERT_EQ(emb_duration_to_ns(EMB_TICKS(2)), 2u * (uint64_t)CONFIG_EMB_TICK_NS);
    EMB_ASSERT_TRUE(EMB_TIMEOUT_IS_FOREVER(EMB_WAIT_FOREVER));
    EMB_ASSERT_TRUE(EMB_TIMEOUT_IS_NO_WAIT(EMB_NO_WAIT));
    EMB_ASSERT_TRUE(
        !EMB_TIMEOUT_IS_FOREVER(EMB_TIMEOUT(d))); /* a finite wait never reads as forever */
    EMB_ASSERT_EQ(EMB_TIMEOUT(d).ticks, EMB_TIMEOUT_MAX_TICKS);
}

EMB_TEST(time_instant_helpers)
{
    emb_instant_t a;
    emb_instant_t b;
    EMB_TEST_REQ("API-021");
    a.ticks = 100u;
    b = emb_instant_add(a, EMB_TICKS(5));
    EMB_ASSERT_EQ(b.ticks, 105u);
    EMB_ASSERT_TRUE(emb_instant_before(a, b));
    EMB_ASSERT_TRUE(!emb_instant_before(b, a));
    EMB_ASSERT_EQ(emb_instant_sub(b, a).ticks, 5u);
    EMB_ASSERT_EQ(emb_instant_sub(a, b).ticks, 0u);
    a.ticks = EMB_TICK_MAX - 1u;
    EMB_ASSERT_EQ(emb_instant_add(a, EMB_TICKS(10)).ticks, EMB_TICK_MAX);
}

EMB_TEST(time_now_is_monotonic_across_sleeps)
{
    emb_instant_t prev = emb_time_now();
    unsigned i;
    EMB_TEST_REQ("KRN-TIM-001");
    for (i = 0u; i < 20u; i++) {
        emb_instant_t now;
        EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS((i % 3u) + 1u)));
        now = emb_time_now();
        EMB_ASSERT_TRUE(!emb_instant_before(now, prev));
        prev = now;
    }
}

#if CONFIG_EMB_TICK_32BIT
EMB_TEST(time_32bit_far_deadlines_and_wrap)
{
    emb_instant_t t0 = emb_time_now();
    emb_instant_t far;
    emb_instant_t t1;
    EMB_TEST_REQ("KRN-TIM-022");
    far = emb_instant_add(t0, EMB_TICKS(EMB_TIMEOUT_MAX_TICKS));
    EMB_ASSERT_OK(emb_thread_sleep_until(far)); /* the longest finite wait: 2^31 - 1 ticks */
    t1 = emb_time_now();
    EMB_ASSERT_EQ(emb_instant_sub(t1, t0).ticks, EMB_TIMEOUT_MAX_TICKS);
    EMB_ASSERT_OK(emb_thread_sleep_until(emb_instant_add(t1, EMB_TICKS(EMB_TIMEOUT_MAX_TICKS))));
    EMB_ASSERT_TRUE(emb_time_now().ticks < t1.ticks); /* the 32-bit counter wrapped */
    EMB_ASSERT_EQ(emb_instant_sub(emb_time_now(), t1).ticks, EMB_TIMEOUT_MAX_TICKS);
    t1 = emb_time_now();
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    EMB_ASSERT_EQ(emb_instant_sub(emb_time_now(), t1).ticks, 10u);
    /* an instant beyond the window reads as past: yields, does not wait */
    t0 = emb_time_now();
    far.ticks = t0.ticks + EMB_TIMEOUT_MAX_TICKS + 2u;
    EMB_ASSERT_OK(emb_thread_sleep_until(far));
    EMB_ASSERT_EQ(emb_time_now().ticks, t0.ticks);
}
#endif
