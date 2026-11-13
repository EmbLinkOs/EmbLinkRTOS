/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_rtc.c
 * @brief   The RTC class (SPEC-017 §10, A123).
 *
 * @details
 * Its checks over a driver's operations, through a fake RTC whose time the test moves and whose
 * alarm the test fires: one alarm, re-armed from its own callback, canceled, disarmed by a set;
 * backup registers; the wall clock seeded at start.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <string.h>

#define T0 INT64_C(1794240000000000000) /* 2026-11-10T00:00:00Z */
#define S  INT64_C(1000000000)

typedef struct fake_rtc {
    emb_drv_rtc_data_t rtc; /* the class's, first */
    int64_t now;
    int64_t alarm_at;
    bool set;
    bool armed;
    uint32_t backup[2];
} fake_rtc_t;

static fake_rtc_t fake;
static emb_device_state_t rtc_state;

static emb_status_t fake_init(const emb_device_t *dev)
{
    return emb_drv_rtc_started(dev);
}

static emb_status_t fake_get(const emb_device_t *dev, int64_t *ns)
{
    (void)dev;
    if (!fake.set) {
        return EMB_ENOTSET;
    }
    *ns = fake.now;
    return EMB_OK;
}

static emb_status_t fake_set(const emb_device_t *dev, int64_t ns)
{
    (void)dev;
    fake.now = ns;
    fake.set = true;
    return EMB_OK;
}

static emb_status_t fake_alarm(const emb_device_t *dev, int64_t at_ns)
{
    (void)dev;
    if (at_ns - fake.now > 100 * S) {
        return EMB_EINVAL; /* its reach */
    }
    fake.alarm_at = at_ns;
    fake.armed = true;
    return EMB_OK;
}

static void fake_cancel(const emb_device_t *dev)
{
    (void)dev;
    fake.armed = false;
}

static emb_status_t fake_backup_read(const emb_device_t *dev, uint8_t index, uint32_t *value)
{
    (void)dev;
    *value = fake.backup[index];
    return EMB_OK;
}

static emb_status_t fake_backup_write(const emb_device_t *dev, uint8_t index, uint32_t value)
{
    (void)dev;
    fake.backup[index] = value;
    return EMB_OK;
}

static const emb_drv_rtc_api_t fake_api = {
    {fake_init, NULL, NULL, NULL, NULL, EMB_DRV_RTC_CAP_BACKUP},
    fake_get,
    fake_set,
    fake_alarm,
    fake_cancel,
    fake_backup_read,
    fake_backup_write,
    2u,
    {0u, 0u, 0u},
};
static const emb_device_t rtc_dev = {
    "rtc", &fake_api.dev, NULL, &rtc_state, &fake, NULL, EMB_DRV_CLASS_RTC, EMB_DEVICE_EARLY, 0u};

static unsigned fired;
static bool rearm;

static void on_alarm(void *arg)
{
    (void)arg;
    fired++;
    fake.armed = false;
    if (rearm) {
        rearm = false;
        (void)emb_drv_rtc_alarm_set(&rtc_dev, fake.now + S, on_alarm, NULL); /* from its own */
    }
}

/* The fake reaches its alarm's time: its interrupt runs. */
static void reach_alarm(void)
{
    fake.now = fake.alarm_at;
    (void)emb_drv_rtc_alarm_fired(&rtc_dev);
}

static void ready(bool set)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    (void)memset(&fake, 0, sizeof(fake));
    fake.now = T0;
    fake.set = set;
    rtc_state = idle;
    fired = 0u;
    rearm = false;
    EMB_ASSERT_OK(emb_device_init(&rtc_dev));
}

EMB_TEST(rtc_get_set_and_not_set)
{
    int64_t ns = 0;
    EMB_TEST_REQ("DRV-001");
    ready(false);
    EMB_ASSERT_STATUS(emb_drv_rtc_get(&rtc_dev, &ns), EMB_ENOTSET);
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_set(&rtc_dev, T0 + S, on_alarm, NULL), EMB_ENOTSET);
    EMB_ASSERT_STATUS(emb_drv_rtc_set(&rtc_dev, -1), EMB_EINVAL);
    EMB_ASSERT_OK(emb_drv_rtc_set(&rtc_dev, T0));
    EMB_ASSERT_OK(emb_drv_rtc_get(&rtc_dev, &ns));
    EMB_ASSERT_TRUE(ns == T0);
}

EMB_TEST(rtc_alarm_once_and_again)
{
    EMB_TEST_REQ("DRV-001");
    ready(true);
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_set(&rtc_dev, T0, on_alarm, NULL), EMB_EINVAL); /* now */
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_set(&rtc_dev, T0 + (200 * S), on_alarm, NULL),
                      EMB_EINVAL); /* beyond its reach */
    EMB_ASSERT_OK(emb_drv_rtc_alarm_set(&rtc_dev, T0 + S, on_alarm, NULL));
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_set(&rtc_dev, T0 + (2 * S), on_alarm, NULL), EMB_EBUSY);
    rearm = true;
    reach_alarm();
    EMB_ASSERT_EQ(fired, 1u);
    EMB_ASSERT_TRUE(fake.armed && (fake.alarm_at == T0 + (2 * S))); /* armed from its callback */
    reach_alarm();
    EMB_ASSERT_EQ(fired, 2u);
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_fired(&rtc_dev), EMB_ESTATE); /* a stray interrupt */
    EMB_ASSERT_EQ(fired, 2u);
}

EMB_TEST(rtc_alarm_canceled_and_set_disarms)
{
    EMB_TEST_REQ("DRV-001");
    ready(true);
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_cancel(&rtc_dev), EMB_ESTATE); /* none */
    EMB_ASSERT_OK(emb_drv_rtc_alarm_set(&rtc_dev, T0 + S, on_alarm, NULL));
    EMB_ASSERT_OK(emb_drv_rtc_alarm_cancel(&rtc_dev));
    EMB_ASSERT_TRUE(!fake.armed);
    reach_alarm();
    EMB_ASSERT_EQ(fired, 0u); /* nothing after a cancel */
    EMB_ASSERT_OK(emb_drv_rtc_alarm_set(&rtc_dev, fake.now + S, on_alarm, NULL));
    EMB_ASSERT_OK(emb_drv_rtc_set(&rtc_dev, T0 + (50 * S)));
    EMB_ASSERT_TRUE(!fake.armed); /* counted in the old time */
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_cancel(&rtc_dev), EMB_ESTATE);
}

EMB_TEST(rtc_backup_and_misuse)
{
    uint32_t v = 0u;
    EMB_TEST_REQ("DRV-001");
    ready(true);
    EMB_ASSERT_OK(emb_drv_rtc_backup_write(&rtc_dev, 1u, 0xC0FFEEu));
    EMB_ASSERT_OK(emb_drv_rtc_backup_read(&rtc_dev, 1u, &v));
    EMB_ASSERT_EQ(v, 0xC0FFEEu);
    EMB_ASSERT_STATUS(emb_drv_rtc_backup_read(&rtc_dev, 2u, &v), EMB_EINVAL); /* past them */
    EMB_ASSERT_FAULTS((void)emb_drv_rtc_get(&rtc_dev, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_rtc_alarm_set(&rtc_dev, T0 + S, NULL, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_rtc_alarm_cancel(NULL));
}

#if CONFIG_EMB_WALLCLOCK
EMB_TEST(rtc_seeds_the_wall_clock)
{
    int64_t w = 0;
    EMB_TEST_REQ("KRN-TIM-031");
    ready(true); /* set: its start seeds the wall clock */
    EMB_ASSERT_OK(emb_wallclock_get(&w));
    EMB_ASSERT_TRUE((w >= T0) && (w < T0 + (INT64_C(10) * 1000000)));
}
#endif
