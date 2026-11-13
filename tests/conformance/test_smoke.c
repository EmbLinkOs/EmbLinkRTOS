/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_smoke.c
 * @brief   Smoke test: the kernel starts, threads run and switch, time advances.
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#include <limits.h>
#include <stdlib.h>

#include <emb_test.h>
#include <errno.h>

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

/* The C library works from a thread: errno is thread-local storage in picolibc, so the port
 * must point the thread pointer at a TLS block (RISC-V tp, A88). One block is shared by
 * every thread: errno is not per thread. */
EMB_TEST(smoke_libc_errno_from_a_thread)
{
    long v;
    EMB_TEST_REQ("PORT-015");
    errno = 0;
    v = strtol("99999999999999999999999", NULL, 10); /* out of range for long */
    EMB_ASSERT_TRUE(v == LONG_MAX);
    EMB_ASSERT_EQ(errno, ERANGE);
    errno = 0;
    EMB_ASSERT_EQ(errno, 0);
}
