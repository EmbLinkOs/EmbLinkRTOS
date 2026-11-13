/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_counter.c
 * @brief   The counter class (SPEC-017 §11a, A129).
 *
 * @details
 * Its checks over a driver's operations, through a fake counter whose count the test moves and
 * whose alarms it fires: one-shot alarms, one passed, one armed from its own callback, canceled;
 * periodic alarms without drift, late ones skipped, one stopped from its callback, one the
 * counter cannot keep; the channels a device has.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <string.h>

typedef struct fake_counter {
    emb_drv_counter_data_t counter; /* the class's, first */
    uint64_t now;
    uint64_t target[EMB_DRV_COUNTER_CHANNELS_MAX];
    bool armed[EMB_DRV_COUNTER_CHANNELS_MAX];
    bool refuse; /* every arm refused as passed: a period it cannot keep */
    unsigned arms;
} fake_counter_t;

static fake_counter_t fake;
static emb_device_state_t counter_state;

static emb_status_t fake_init(const emb_device_t *dev)
{
    return emb_drv_counter_started(dev, 3u); /* fewer than the driver's most */
}

static uint64_t fake_now(const emb_device_t *dev)
{
    (void)dev;
    return fake.now;
}

static uint32_t fake_rate(const emb_device_t *dev)
{
    (void)dev;
    return 1000000u;
}

static emb_status_t fake_arm(const emb_device_t *dev, uint8_t channel, uint64_t at)
{
    (void)dev;
    fake.arms++;
    if (fake.refuse || (at <= fake.now)) {
        return EMB_EPAST;
    }
    fake.target[channel] = at;
    fake.armed[channel] = true;
    return EMB_OK;
}

static void fake_disarm(const emb_device_t *dev, uint8_t channel)
{
    (void)dev;
    fake.armed[channel] = false;
}

static const emb_drv_counter_api_t fake_api = {
    {fake_init, NULL, NULL, NULL, NULL, 0u},
    fake_now,
    fake_rate,
    fake_arm,
    fake_disarm,
    4u,
    {0u, 0u, 0u},
};
static const emb_device_t counter_dev = {"counter",
                                         &fake_api.dev,
                                         NULL,
                                         &counter_state,
                                         &fake,
                                         NULL,
                                         EMB_DRV_CLASS_COUNTER,
                                         EMB_DEVICE_KERNEL,
                                         0u};

static const emb_device_api_t plain_api = {NULL, NULL, NULL, NULL, NULL, 0u};
static uint8_t plain_data;
static emb_device_state_t plain_state;
static const emb_device_t plain_dev = {
    "plain",           &plain_api, NULL, &plain_state, &plain_data, NULL, EMB_DRV_CLASS_GPIO,
    EMB_DEVICE_KERNEL, 0u};

static unsigned fired;
static uint64_t last_at;
static bool again;
static bool stop;

static void on_alarm(void *arg, uint64_t at)
{
    uint8_t channel = *(const uint8_t *)arg;
    fired++;
    last_at = at;
    if (again) {
        again = false;
        (void)emb_drv_counter_alarm_set(&counter_dev, channel, fake.now + 50u, on_alarm, arg);
    }
    if (stop) {
        (void)emb_drv_counter_periodic_stop(&counter_dev, channel);
    }
}

static uint8_t ch0 = 0u;
static uint8_t ch1 = 1u;

/* The fake reaches a channel's count: the hardware's alarm is spent, and its interrupt runs. */
static void reach(uint8_t channel)
{
    fake.now = fake.target[channel];
    fake.armed[channel] = false;
    (void)emb_drv_counter_fired(&counter_dev, channel);
}

static void ready(void)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    (void)memset(&fake, 0, sizeof(fake));
    fake.now = 1000u;
    counter_state = idle;
    fired = 0u;
    last_at = 0u;
    again = false;
    stop = false;
    EMB_ASSERT_OK(emb_device_init(&counter_dev));
}

EMB_TEST(counter_count_rate_and_channels)
{
    uint64_t count = 0u;
    uint32_t hz = 0u;
    uint8_t n = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_counter_now(&counter_dev, &count));
    EMB_ASSERT_TRUE(count == 1000u);
    EMB_ASSERT_OK(emb_drv_counter_rate(&counter_dev, &hz));
    EMB_ASSERT_EQ(hz, 1000000u);
    EMB_ASSERT_OK(emb_drv_counter_channels(&counter_dev, &n));
    EMB_ASSERT_EQ(n, 3u); /* the device's, fewer than the driver's four */
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(&counter_dev, 3u, 2000u, on_alarm, &ch0),
                      EMB_EINVAL);
}

EMB_TEST(counter_alarm_once_never_early)
{
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_counter_alarm_set(&counter_dev, 0u, 1500u, on_alarm, &ch0));
    EMB_ASSERT_TRUE(fake.armed[0] && (fake.target[0] == 1500u));
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(&counter_dev, 0u, 1600u, on_alarm, &ch0),
                      EMB_EBUSY);
    EMB_ASSERT_OK(emb_drv_counter_alarm_set(&counter_dev, 1u, 1700u, on_alarm, &ch1)); /* another */
    reach(0u);
    EMB_ASSERT_EQ(fired, 1u);
    EMB_ASSERT_TRUE(last_at == 1500u);
    EMB_ASSERT_STATUS(emb_drv_counter_fired(&counter_dev, 0u), EMB_ESTATE); /* a stray one */
    EMB_ASSERT_EQ(fired, 1u);
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_cancel(&counter_dev, 0u), EMB_ESTATE); /* it fired */
    EMB_ASSERT_OK(emb_drv_counter_alarm_cancel(&counter_dev, 1u));
    EMB_ASSERT_TRUE(!fake.armed[1]);
    reach(1u);
    EMB_ASSERT_EQ(fired, 1u); /* nothing after a cancel */
}

EMB_TEST(counter_alarm_passed_and_again)
{
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(&counter_dev, 0u, 1000u, on_alarm, &ch0),
                      EMB_EPAST);
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(&counter_dev, 0u, 10u, on_alarm, &ch0), EMB_EPAST);
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_cancel(&counter_dev, 0u), EMB_ESTATE); /* none armed */
    EMB_ASSERT_OK(emb_drv_counter_alarm_set(&counter_dev, 0u, 1100u, on_alarm, &ch0));
    again = true;
    reach(0u);
    EMB_ASSERT_EQ(fired, 1u);
    EMB_ASSERT_TRUE(fake.armed[0] && (fake.target[0] == 1150u)); /* armed from its callback */
    reach(0u);
    EMB_ASSERT_EQ(fired, 2u);
}

EMB_TEST(counter_periodic_without_drift)
{
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_STATUS(emb_drv_counter_periodic(&counter_dev, 0u, 1u, on_alarm, &ch0), EMB_EINVAL);
    EMB_ASSERT_OK(emb_drv_counter_periodic(&counter_dev, 0u, 1000u, on_alarm, &ch0));
    EMB_ASSERT_TRUE(fake.target[0] == 2000u); /* a period from now */
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_cancel(&counter_dev, 0u), EMB_ESTATE); /* periodic */
    reach(0u);
    EMB_ASSERT_TRUE((last_at == 2000u) && (fake.target[0] == 3000u));
    fake.now = 3040u; /* its interrupt 40 counts late: the next is still due at 4000 */
    (void)emb_drv_counter_fired(&counter_dev, 0u);
    EMB_ASSERT_TRUE((last_at == 3000u) && (fake.target[0] == 4000u));
    fake.now = 6500u; /* held off past two periods: they are skipped, the next is 7000 */
    (void)emb_drv_counter_fired(&counter_dev, 0u);
    EMB_ASSERT_TRUE((last_at == 4000u) && (fake.target[0] == 7000u));
    EMB_ASSERT_EQ(fired, 3u);
    stop = true; /* it stops itself */
    reach(0u);
    EMB_ASSERT_EQ(fired, 4u);
    EMB_ASSERT_TRUE(!fake.armed[0]);
    EMB_ASSERT_STATUS(emb_drv_counter_periodic_stop(&counter_dev, 0u), EMB_ESTATE);
    EMB_ASSERT_STATUS(emb_drv_counter_fired(&counter_dev, 0u), EMB_ESTATE);
    EMB_ASSERT_EQ(fired, 4u);
}

EMB_TEST(counter_periodic_the_counter_cannot_keep)
{
    unsigned arms;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_counter_periodic(&counter_dev, 1u, 2u, on_alarm, &ch1));
    fake.refuse = true; /* every next period has passed by the time it is armed */
    arms = fake.arms;
    reach(1u);
    EMB_ASSERT_EQ(fired, 1u);            /* the period due still runs */
    EMB_ASSERT_EQ(fake.arms - arms, 4u); /* a bounded number of tries */
    EMB_ASSERT_STATUS(emb_drv_counter_periodic_stop(&counter_dev, 1u), EMB_ESTATE); /* stopped */
    fake.refuse = false;
    EMB_ASSERT_OK(emb_drv_counter_alarm_set(&counter_dev, 1u, fake.now + 10u, on_alarm, &ch1));
}

EMB_TEST(counter_not_ready_and_misuse)
{
    uint64_t count = 0u;
    uint32_t hz = 0u;
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    EMB_TEST_REQ("DRV-001");
    counter_state = idle;
    EMB_ASSERT_STATUS(emb_drv_counter_now(&counter_dev, &count), EMB_ESTATE);
    EMB_ASSERT_STATUS(emb_drv_counter_rate(&counter_dev, &hz), EMB_ESTATE);
    EMB_ASSERT_STATUS(emb_drv_counter_alarm_set(&counter_dev, 0u, 5000u, on_alarm, &ch0),
                      EMB_ESTATE);
    EMB_ASSERT_STATUS(emb_drv_counter_periodic(&counter_dev, 0u, 100u, on_alarm, &ch0), EMB_ESTATE);
    ready();
    EMB_ASSERT_FAULTS((void)emb_drv_counter_now(&plain_dev, &count)); /* not a counter */
    EMB_ASSERT_FAULTS((void)emb_drv_counter_now(&counter_dev, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_counter_alarm_set(&counter_dev, 0u, 5000u, NULL, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_counter_periodic(NULL, 0u, 100u, on_alarm, &ch0));
    EMB_ASSERT_FAULTS((void)emb_drv_counter_alarm_cancel(NULL, 0u));
}
