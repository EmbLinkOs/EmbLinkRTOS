/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_wdt_board.c
 * @brief   The board's watchdog (SPEC-017 §8; FLT-004).
 *
 * @details
 * Started, fed for half a second, then starved, it resets the board, and after the reset it says it
 * did. The reset check (EMB_ASSERT_RESETS) needs a board that resumes a case after a reset;
 * elsewhere it is skipped. Built where the board has a watchdog device (EMB_SOC_WATCHDOG_DEVICE).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <soc_devices.h>

#define WDT     EMB_SOC_WATCHDOG_DEVICE
#define TIMEOUT 100u /* ms */

EMB_TEST(wdt_board_refuses_what_it_cannot_count)
{
    EMB_TEST_REQ("FLT-004");
    EMB_ASSERT_TRUE(emb_device_ready(WDT));
    EMB_ASSERT_STATUS(emb_drv_wdt_start(WDT, 0u), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_drv_wdt_start(WDT, 100000000u), EMB_EINVAL); /* a day: not started */
}

/* The statement the reset check runs: a reset before emb_test_reset_expected() fails it, so a
 * watchdog that reset the board while fed is caught. */
static void starve(void)
{
    unsigned i;
    EMB_ASSERT_OK(emb_drv_wdt_start(WDT, TIMEOUT));
    EMB_ASSERT_STATUS(emb_drv_wdt_start(WDT, TIMEOUT), EMB_ESTATE); /* once */
    for (i = 0u; i < 20u; i++) { /* bounded: half a second, fed every quarter of the timeout */
        EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(TIMEOUT / 4u)));
        EMB_ASSERT_OK(emb_drv_wdt_feed(WDT));
    }
    emb_test_reset_expected();
    (void)emb_thread_sleep(EMB_MS(30u * TIMEOUT)); /* starved: it resets the board first */
}

EMB_TEST(wdt_board_starved_resets_the_board)
{
    bool yes = false;
    EMB_TEST_REQ("FLT-004");
    EMB_ASSERT_RESETS(starve());
    EMB_ASSERT_OK(emb_drv_wdt_caused_reset(WDT, &yes)); /* the boot after the reset */
    EMB_ASSERT_TRUE(yes);
}
