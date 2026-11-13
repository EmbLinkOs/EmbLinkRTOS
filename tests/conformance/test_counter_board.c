/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_counter_board.c
 * @brief   The board's counters (SPEC-017 §11a, A129): every device of the class it has.
 *
 * @details
 * Each counter's rate against the kernel's clock; its count rising, across the wraps of a
 * 16-bit timer (TIM9 on the NUCLEO-F446RE wraps every 65.5 ms); one-shot alarms, near and many
 * wraps away, never early and at most LATE counts late; every channel; a passed count refused;
 * a periodic callback whose periods are exact.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <soc_devices.h>

#define TICK_NS ((uint64_t)CONFIG_EMB_TICK_NS)
#define LATE    200u /* counts: an interrupt's latency, under an emulator too; a wrap is 65536 */
#define PERIODS 100u
#define WAIT_MS 1u

static const emb_device_t *const counters[] = {EMB_SOC_COUNTER_DEVICES};

static volatile unsigned fired;
static volatile uint64_t fired_count; /* the count when the callback ran */
static volatile uint64_t fired_at;    /* the count it was due at */
static volatile uint64_t gaps;        /* periodic: the periods between two calls, other than one */
static volatile uint64_t worst;       /* periodic: the latest a call ran after it was due */
static const emb_device_t *current;
static uint64_t period;

static void on_alarm(void *arg, uint64_t at)
{
    uint64_t count = 0u;
    (void)arg;
    (void)emb_drv_counter_now(current, &count);
    fired_count = count;
    fired_at = at;
    fired++;
}

static void on_period(void *arg, uint64_t at)
{
    uint64_t count = 0u;
    uint8_t channel = *(const uint8_t *)arg;
    (void)emb_drv_counter_now(current, &count);
    if ((fired != 0u) && (at != fired_at + period)) {
        gaps++;
    }
    if ((count - at) > worst) {
        worst = count - at;
    }
    fired_at = at;
    fired++;
    if (fired == PERIODS) {
        (void)emb_drv_counter_periodic_stop(current, channel);
    }
}

/* Wait, sleeping, until the callbacks have run n times or limit_ms passed. */
static void wait_fired(unsigned n, uint32_t limit_ms)
{
    uint32_t ms;
    for (ms = 0u; (ms < limit_ms) && (fired < n); ms += WAIT_MS) { /* bounded */
        (void)emb_thread_sleep(EMB_MS(WAIT_MS));
    }
}

static uint64_t count_of(const emb_device_t *dev)
{
    uint64_t count = 0u;
    EMB_ASSERT_OK(emb_drv_counter_now(dev, &count));
    return count;
}

EMB_TEST(counter_board_rate_against_the_kernel_clock)
{
    unsigned i;
    EMB_TEST_REQ("DRV-001");
    for (i = 0u; i < sizeof(counters) / sizeof(counters[0]); i++) { /* bounded */
        const emb_device_t *dev = counters[i];
        uint32_t hz = 0u;
        uint64_t c0;
        uint64_t c1;
        uint64_t t0;
        uint64_t t1;
        uint64_t want;
        uint64_t slack;
        EMB_ASSERT_TRUE(emb_device_ready(dev));
        EMB_ASSERT_OK(emb_drv_counter_rate(dev, &hz));
        EMB_ASSERT_TRUE(hz > 0u);
        (void)emb_thread_sleep(EMB_MS(1u)); /* start on a tick */
        t0 = emb_time_now().ticks;
        c0 = count_of(dev);
        (void)emb_thread_sleep(EMB_MS(150u)); /* two wraps of a 16-bit counter at 1 MHz */
        c1 = count_of(dev);
        t1 = emb_time_now().ticks;
        want = ((t1 - t0) * TICK_NS * hz) / 1000000000u;
        slack = (2u * TICK_NS * hz) / 1000000000u + LATE; /* a tick at each end */
        EMB_ASSERT_TRUE(c1 > c0);
        EMB_ASSERT_TRUE(((c1 - c0) + slack >= want) && ((c1 - c0) <= want + slack));
    }
}

EMB_TEST(counter_board_count_rises_across_wraps)
{
    unsigned i;
    EMB_TEST_REQ("DRV-001");
    for (i = 0u; i < sizeof(counters) / sizeof(counters[0]); i++) { /* bounded */
        const emb_device_t *dev = counters[i];
        uint64_t last = count_of(dev);
        uint32_t n;
        bool rising = true;
        for (n = 0u; n < 200u; n++) { /* bounded: 200 ms, three wraps of a 16-bit counter */
            uint32_t k;
            for (k = 0u; k < 100u; k++) { /* bounded: reads back to back */
                uint64_t c = count_of(dev);
                rising = rising && (c >= last);
                last = c;
            }
            (void)emb_thread_sleep(EMB_MS(1u));
        }
        EMB_ASSERT_TRUE(rising);
    }
}

EMB_TEST(counter_board_alarms_never_early)
{
    static const uint32_t after[] = {500u, 3000u, 200000u}; /* the last, three wraps away */
    unsigned i;
    unsigned k;
    EMB_TEST_REQ("DRV-001");
    for (i = 0u; i < sizeof(counters) / sizeof(counters[0]); i++) { /* bounded */
        const emb_device_t *dev = counters[i];
        uint32_t hz = 0u;
        current = dev;
        EMB_ASSERT_OK(emb_drv_counter_rate(dev, &hz));
        for (k = 0u; k < sizeof(after) / sizeof(after[0]); k++) { /* bounded */
            uint64_t at = count_of(dev) + ((uint64_t)after[k] * hz / 1000000u);
            fired = 0u;
            EMB_ASSERT_OK(emb_drv_counter_alarm_set(dev, 0u, at, on_alarm, NULL));
            wait_fired(1u, (after[k] / 1000u) + 100u);
            EMB_ASSERT_EQ(fired, 1u);
            EMB_ASSERT_TRUE(fired_at == at);
            EMB_ASSERT_TRUE(fired_count >= at);        /* never early */
            EMB_ASSERT_TRUE(fired_count - at <= LATE); /* nor a wrap late */
        }
    }
}

EMB_TEST(counter_board_every_channel_and_a_passed_count)
{
    static uint8_t chans[EMB_DRV_COUNTER_CHANNELS_MAX] = {0u, 1u, 2u, 3u};
    unsigned i;
    EMB_TEST_REQ("DRV-001");
    for (i = 0u; i < sizeof(counters) / sizeof(counters[0]); i++) { /* bounded */
        const emb_device_t *dev = counters[i];
        uint8_t n = 0u;
        uint8_t ch;
        uint64_t now;
        current = dev;
        EMB_ASSERT_OK(emb_drv_counter_channels(dev, &n));
        EMB_ASSERT_TRUE((n >= 1u) && (n <= EMB_DRV_COUNTER_CHANNELS_MAX));
        now = count_of(dev);
        EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(dev, 0u, now, on_alarm, NULL), EMB_EPAST);
        EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(dev, n, now + 1000u, on_alarm, NULL),
                          EMB_EINVAL);
        fired = 0u;
        now = count_of(dev);
        for (ch = 0u; ch < n; ch++) { /* bounded */
            EMB_ASSERT_OK(emb_drv_counter_alarm_set(dev, ch, now + 2000u + (1000u * ch), on_alarm,
                                                    &chans[ch]));
        }
        EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(dev, 0u, now + 9000u, on_alarm, NULL),
                          EMB_EBUSY);
        wait_fired(n, 100u);
        EMB_ASSERT_EQ(fired, (unsigned)n);
        EMB_ASSERT_TRUE(fired_at == now + 2000u + (1000u * (n - 1u))); /* in order, the last last */
        EMB_ASSERT_OK(emb_drv_counter_alarm_set(dev, 0u, count_of(dev) + 50000u, on_alarm, NULL));
        EMB_ASSERT_OK(emb_drv_counter_alarm_cancel(dev, 0u));
        fired = 0u;
        (void)emb_thread_sleep(EMB_MS(80u));
        EMB_ASSERT_EQ(fired, 0u); /* nothing after a cancel */
    }
}

EMB_TEST(counter_board_periodic_exact_periods)
{
    static uint8_t ch0 = 0u;
    unsigned i;
    EMB_TEST_REQ("DRV-001");
    for (i = 0u; i < sizeof(counters) / sizeof(counters[0]); i++) { /* bounded */
        const emb_device_t *dev = counters[i];
        uint32_t hz = 0u;
        current = dev;
        EMB_ASSERT_OK(emb_drv_counter_rate(dev, &hz));
        period = hz / 1000u; /* a millisecond */
        fired = 0u;
        gaps = 0u;
        worst = 0u;
        EMB_ASSERT_OK(emb_drv_counter_periodic(dev, 0u, period, on_period, &ch0));
        wait_fired(PERIODS, (PERIODS * 2u) + 100u);
        EMB_ASSERT_EQ(fired, PERIODS);
        EMB_ASSERT_TRUE(gaps == 0u);    /* each a period after the last was due */
        EMB_ASSERT_TRUE(worst <= LATE); /* and run soon after */
        (void)emb_thread_sleep(EMB_MS(5u));
        EMB_ASSERT_EQ(fired, PERIODS); /* stopped from its own callback */
        EMB_ASSERT_STATUS(emb_drv_counter_periodic_stop(dev, 0u), EMB_ESTATE);
    }
}
