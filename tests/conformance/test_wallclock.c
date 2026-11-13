/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_wallclock.c
 * @brief   The wall clock (SPEC-003 §10, KRN-TIM-031, A123).
 *
 * @details
 * The monotonic clock plus an offset, which setting and adjusting change without moving any
 * deadline. A board's RTC may have seeded it at boot, so its being unset is not assumed.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/wallclock.h>

#include <emb_test.h>

#define T0      INT64_C(1794240000000000000) /* 2026-11-10T00:00:00Z */
#define MS      INT64_C(1000000)
#define TICK_NS ((int64_t)CONFIG_EMB_TICK_NS)

static int64_t mono_ns(void)
{
    return (int64_t)emb_time_now().ticks * TICK_NS;
}

EMB_TEST(wallclock_set_and_get)
{
    int64_t w = 0;
    int64_t m0;
    EMB_TEST_REQ("KRN-TIM-031");
    EMB_ASSERT_OK(emb_wallclock_set(T0));
    m0 = mono_ns();
    EMB_ASSERT_OK(emb_wallclock_get(&w));
    EMB_ASSERT_TRUE((w >= T0) && (w <= T0 + (mono_ns() - m0) + TICK_NS));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(50u)));
    EMB_ASSERT_OK(emb_wallclock_get(&w));
    EMB_ASSERT_TRUE((w >= T0 + (50 * MS)) && (w <= T0 + (50 * MS) + (mono_ns() - m0)));
    EMB_ASSERT_STATUS(emb_wallclock_set(-1), EMB_EINVAL); /* before the epoch */
    EMB_ASSERT_FAULTS((void)emb_wallclock_get(NULL));
}

EMB_TEST(wallclock_adjust_moves_no_deadline)
{
    int64_t before = 0;
    int64_t after = 0;
    emb_instant_t t0;
    emb_duration_t slept;
    EMB_TEST_REQ("KRN-TIM-031");
    EMB_ASSERT_OK(emb_wallclock_set(T0));
    EMB_ASSERT_OK(emb_wallclock_get(&before));
    t0 = emb_time_now();
    EMB_ASSERT_OK(emb_wallclock_adjust(INT64_C(3600) * 1000 * MS)); /* an hour ahead */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(20u)));
    slept = emb_instant_sub(emb_time_now(), t0);
    emb_test_check_elapsed(__LINE__, (uint32_t)slept.ticks, (uint32_t)EMB_MS_TO_TICKS(20u));
    EMB_ASSERT_OK(emb_wallclock_get(&after));
    EMB_ASSERT_TRUE(after - before >= (INT64_C(3600) * 1000 * MS) + (20 * MS));
    EMB_ASSERT_OK(emb_wallclock_adjust(-(INT64_C(3600) * 1000 * MS)));
    EMB_ASSERT_STATUS(emb_wallclock_adjust(-T0 * 2), EMB_EINVAL); /* before the epoch */
}
