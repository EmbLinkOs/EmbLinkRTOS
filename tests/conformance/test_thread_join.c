/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_thread_join.c
 * @brief   Thread join and detach (SPEC-008 §5, §6; KRN-THR-004, 006, 017, 018, 022).
 *
 * @details
 * Split from test_thread.c, whose image no longer fit the Uno's 32,256-byte partition.
 *
 * @author  Junior Deogracias
 * @date    2026-11-05 (created)
 */
#include <emb_test.h>

static void exit_with_code(void *arg)
{
    emb_thread_exit((int)(intptr_t)arg);
}

static void sleep_then_exit(void *arg)
{
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS((uint32_t)(uintptr_t)arg)));
    emb_thread_exit(42);
}

EMB_TEST(thread_join_after_exit_returns_code)
{
    emb_thread_t h = emb_test_thread(EMB_TEST_NAME("h"), 3u, 0u, exit_with_code, (void *)77);
    int code = 0;
    EMB_TEST_REQ("KRN-THR-004", "KRN-THR-006", "KRN-THR-017");
    EMB_ASSERT_OK(emb_thread_join(h, EMB_NO_WAIT, &code));
    EMB_ASSERT_EQ(code, 77);
}

EMB_TEST(thread_join_before_exit_blocks_and_hands_off)
{
    uint32_t t0 = EMB_TEST_NOW();
    emb_thread_t h = emb_test_thread(EMB_TEST_NAME("h"), 3u, 0u, sleep_then_exit, (void *)5);
    int code = 0;
    EMB_TEST_REQ("KRN-THR-017", "KRN-WAIT-012");
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, &code));
    EMB_ASSERT_EQ(code, 42);
    EMB_ASSERT_ELAPSED(t0, 5u);
}

EMB_TEST(thread_join_timeout_then_success)
{
    emb_thread_t h = emb_test_thread(EMB_TEST_NAME("h"), 3u, 0u, sleep_then_exit, (void *)20);
    EMB_TEST_REQ("KRN-THR-017", "KRN-WAIT-004");
    EMB_ASSERT_STATUS(emb_thread_join(h, EMB_TIMEOUT(EMB_TICKS(5)), NULL), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
}

static void joiner_entry(void *arg)
{
    emb_thread_t target = *(emb_thread_t *)arg;
    int code = 0;
    EMB_ASSERT_OK(emb_thread_join(target, EMB_WAIT_FOREVER, &code));
    EMB_ASSERT_EQ(code, 42);
    emb_test_mark(1);
}

EMB_TEST(thread_second_joiner_is_ebusy)
{
    static emb_thread_t target;
    EMB_TEST_REQ("KRN-THR-018");
    target = emb_test_thread(EMB_TEST_NAME("target"), 2u, 0u, sleep_then_exit, (void *)10);
    (void)emb_test_thread(EMB_TEST_NAME("joiner"), 3u, 0u, joiner_entry, &target);
    EMB_ASSERT_STATUS(emb_thread_join(target, EMB_WAIT_FOREVER, NULL), EMB_EBUSY);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(15)));
    EMB_ASSERT_MARKS(1);
}

EMB_TEST(thread_join_self_and_detached_are_errors)
{
    emb_thread_t d =
        emb_test_thread(EMB_TEST_NAME("d"), 3u, EMB_THREAD_DETACHED, exit_with_code, (void *)1);
    EMB_TEST_REQ("KRN-THR-017");
    EMB_ASSERT_MISUSE(emb_thread_join(emb_thread_self(), EMB_NO_WAIT, NULL), EMB_EDEADLK);
    EMB_ASSERT_STATUS(emb_thread_join(d, EMB_NO_WAIT, NULL), EMB_EINVAL);
    EMB_ASSERT_OK(emb_test_thread_destroy(d)); /* detached and terminated: reusable at exit */
}

EMB_TEST(thread_detach_after_exit_makes_reusable)
{
    emb_thread_t h = emb_test_thread(EMB_TEST_NAME("h"), 3u, 0u, exit_with_code, (void *)1);
    EMB_TEST_REQ("KRN-THR-017", "KRN-THR-022");
    EMB_ASSERT_MISUSE(emb_thread_destroy(h), EMB_EBUSY);
    EMB_ASSERT_OK(emb_thread_detach(h));
    EMB_ASSERT_OK(emb_test_thread_destroy(h));
}
