/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Smoke test: the kernel starts, threads run and switch, time advances. */
#include <emb_test.h>

static uint32_t hits;

static void helper(void *arg)
{
    hits += (uint32_t)(uintptr_t)arg;
}

EMB_TEST(smoke_thread_runs_and_exits)
{
    emb_thread_t h;
    int code = -1;
    EMB_TEST_REQ("KRN-THR-001", "KRN-THR-005", "KRN-THR-017");
    hits = 0u;
    h = emb_test_thread(EMB_TEST_NAME("helper"), 3u, 0u, helper, (void *)(uintptr_t)7u);
    EMB_ASSERT_EQ(hits, 7u); /* higher priority: ran to completion before we continued */
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, &code));
    EMB_ASSERT_EQ(code, 0);
}

EMB_TEST(smoke_sleep_advances_virtual_time)
{
    emb_instant_t t0 = emb_time_now();
    EMB_TEST_REQ("KRN-TIM-001", "KRN-TIM-010");
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    EMB_ASSERT_ELAPSED(t0.ticks, 10u);
}
