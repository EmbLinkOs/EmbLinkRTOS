/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_pwm.c
 * @brief   The PWM class (SPEC-017 §11b, A129).
 *
 * @details
 * Its checks over a driver's operations, through a fake whose steps are 100 ns: an output set,
 * rounded and reported; the period a device's running channels share; a stop; a capture, and a
 * channel that outputs or captures but not both; a driver that captures nothing.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <string.h>

#define STEP 100u /* the fake's resolution, ns */

typedef struct fake_pwm {
    emb_drv_pwm_data_t pwm; /* the class's, first */
    uint32_t period[EMB_DRV_PWM_CHANNELS_MAX];
    uint32_t pulse[EMB_DRV_PWM_CHANNELS_MAX];
    uint32_t flags[EMB_DRV_PWM_CHANNELS_MAX];
    bool on[EMB_DRV_PWM_CHANNELS_MAX];
    emb_drv_pwm_wave_t input; /* what a capture measures */
    emb_status_t capture_status;
    emb_status_t set_during_capture;
} fake_pwm_t;

static fake_pwm_t fake;
static emb_device_state_t pwm_state;
static emb_device_state_t plain_state;

static emb_status_t fake_init(const emb_device_t *dev)
{
    return emb_drv_pwm_started(dev, 2u);
}

static uint32_t nearest(uint32_t ns)
{
    return ((ns + (STEP / 2u)) / STEP) * STEP;
}

static emb_status_t fake_set(const emb_device_t *dev, uint8_t channel, uint32_t period_ns,
                             uint32_t pulse_ns, uint32_t flags, emb_drv_pwm_wave_t *got)
{
    (void)dev;
    if (period_ns > 1000000000u) {
        return EMB_EINVAL; /* beyond its reach */
    }
    fake.period[channel] = nearest(period_ns);
    fake.pulse[channel] = nearest(pulse_ns);
    fake.flags[channel] = flags;
    fake.on[channel] = true;
    if (got != NULL) {
        got->period_ns = fake.period[channel];
        got->pulse_ns = fake.pulse[channel];
    }
    return EMB_OK;
}

static void fake_stop(const emb_device_t *dev, uint8_t channel)
{
    (void)dev;
    fake.on[channel] = false;
}

static const emb_device_t pwm_dev;

static emb_status_t fake_capture(const emb_device_t *dev, uint8_t channel, emb_drv_pwm_wave_t *wave,
                                 emb_timeout_t timeout)
{
    (void)dev;
    (void)timeout;
    /* while it captures, the channel cannot output */
    fake.set_during_capture = emb_drv_pwm_set(&pwm_dev, channel, 1000u, 500u, 0u, NULL);
    if (fake.capture_status == EMB_OK) {
        *wave = fake.input;
    }
    return fake.capture_status;
}

static const emb_drv_pwm_api_t fake_api = {
    {fake_init, NULL, NULL, NULL, NULL, EMB_DRV_PWM_CAP_CAPTURE},
    fake_set,
    fake_stop,
    fake_capture,
    4u,
    {0u, 0u, 0u},
};
static const emb_device_t pwm_dev = {
    "pwm", &fake_api.dev, NULL, &pwm_state, &fake, NULL, EMB_DRV_CLASS_PWM, EMB_DEVICE_KERNEL, 0u};

/* a driver with outputs alone */
static fake_pwm_t plain;
static const emb_drv_pwm_api_t plain_api = {
    {fake_init, NULL, NULL, NULL, NULL, 0u}, fake_set, fake_stop, NULL, 4u, {0u, 0u, 0u},
};
static const emb_device_t plain_dev = {
    "plain",           &plain_api.dev,    NULL, &plain_state, &plain, NULL,
    EMB_DRV_CLASS_PWM, EMB_DEVICE_KERNEL, 0u};

/* The device started once, as at boot: its lock is made once. Each case after resets the fake
 * and stops what the last one left running. */
static void ready(void)
{
    static bool started;
    uint8_t ch;
    if (!started) {
        emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u,          0u, 0u, EMB_OK,
                                   {NULL, NULL},      {NULL, NULL}};
        pwm_state = idle;
        EMB_ASSERT_OK(emb_device_init(&pwm_dev));
        started = true;
    }
    for (ch = 0u; ch < 2u; ch++) { /* bounded */
        (void)emb_drv_pwm_stop(&pwm_dev, ch);
    }
    (void)memset((uint8_t *)&fake + sizeof(fake.pwm), 0, sizeof(fake) - sizeof(fake.pwm));
}

EMB_TEST(pwm_set_rounded_and_reported)
{
    emb_drv_pwm_wave_t got = {0u, 0u};
    uint8_t n = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_pwm_channels(&pwm_dev, &n));
    EMB_ASSERT_EQ(n, 2u); /* the device's, fewer than the driver's four */
    EMB_ASSERT_OK(emb_drv_pwm_set(&pwm_dev, 0u, 1000040u, 250060u, 0u, &got));
    EMB_ASSERT_EQ(got.period_ns, 1000000u);
    EMB_ASSERT_EQ(got.pulse_ns, 250100u);
    EMB_ASSERT_TRUE(fake.on[0]);
    EMB_ASSERT_OK(emb_drv_pwm_set(&pwm_dev, 0u, 1000040u, 500000u, EMB_DRV_PWM_INVERTED, NULL));
    EMB_ASSERT_EQ(fake.flags[0], EMB_DRV_PWM_INVERTED); /* a running channel takes a new pulse */
    EMB_ASSERT_STATUS(emb_drv_pwm_set(&pwm_dev, 0u, 1000u, 1001u, 0u, NULL), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_drv_pwm_set(&pwm_dev, 0u, 0u, 0u, 0u, NULL), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_drv_pwm_set(&pwm_dev, 2u, 1000u, 500u, 0u, NULL), EMB_EINVAL);
    EMB_ASSERT_OK(emb_drv_pwm_stop(&pwm_dev, 0u)); /* alone: the period is the driver's to refuse */
    EMB_ASSERT_STATUS(emb_drv_pwm_set(&pwm_dev, 1u, 2000000000u, 0u, 0u, NULL), EMB_EINVAL);
    EMB_ASSERT_TRUE(!fake.on[1]); /* the driver's refusal: nothing runs */
}

EMB_TEST(pwm_channels_share_the_period)
{
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_pwm_set(&pwm_dev, 0u, 20000u, 5000u, 0u, NULL));
    EMB_ASSERT_STATUS(emb_drv_pwm_set(&pwm_dev, 1u, 30000u, 5000u, 0u, NULL), EMB_EBUSY);
    EMB_ASSERT_OK(emb_drv_pwm_set(&pwm_dev, 1u, 20000u, 15000u, 0u, NULL)); /* the same period */
    EMB_ASSERT_STATUS(emb_drv_pwm_set(&pwm_dev, 0u, 30000u, 5000u, 0u, NULL), EMB_EBUSY);
    EMB_ASSERT_OK(emb_drv_pwm_stop(&pwm_dev, 1u));
    EMB_ASSERT_TRUE(!fake.on[1]);
    EMB_ASSERT_STATUS(emb_drv_pwm_stop(&pwm_dev, 1u), EMB_ESTATE);         /* not running */
    EMB_ASSERT_OK(emb_drv_pwm_set(&pwm_dev, 0u, 30000u, 5000u, 0u, NULL)); /* alone: its own */
}

EMB_TEST(pwm_capture_and_its_channel)
{
    emb_drv_pwm_wave_t wave = {0u, 0u};
    EMB_TEST_REQ("DRV-001");
    ready();
    fake.input.period_ns = 1000000u;
    fake.input.pulse_ns = 250000u;
    EMB_ASSERT_OK(emb_drv_pwm_capture(&pwm_dev, 1u, &wave, EMB_TIMEOUT(EMB_MS(100))));
    EMB_ASSERT_EQ(wave.period_ns, 1000000u);
    EMB_ASSERT_EQ(wave.pulse_ns, 250000u);
    EMB_ASSERT_STATUS(fake.set_during_capture, EMB_EBUSY); /* it captured: no output meanwhile */
    EMB_ASSERT_OK(emb_drv_pwm_set(&pwm_dev, 1u, 1000u, 500u, 0u, NULL)); /* free again after */
    EMB_ASSERT_STATUS(emb_drv_pwm_capture(&pwm_dev, 1u, &wave, EMB_NO_WAIT),
                      EMB_EBUSY); /* it outputs */
    EMB_ASSERT_OK(emb_drv_pwm_stop(&pwm_dev, 1u));
    fake.capture_status = EMB_ENOSIGNAL;
    EMB_ASSERT_STATUS(emb_drv_pwm_capture(&pwm_dev, 1u, &wave, EMB_TIMEOUT(EMB_MS(10))),
                      EMB_ENOSIGNAL);
    EMB_ASSERT_STATUS(emb_drv_pwm_capture(&pwm_dev, 2u, &wave, EMB_NO_WAIT), EMB_EINVAL);
}

EMB_TEST(pwm_without_capture_and_misuse)
{
    emb_drv_pwm_wave_t wave = {0u, 0u};
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    EMB_TEST_REQ("DRV-001");
    plain_state = idle;
    EMB_ASSERT_STATUS(emb_drv_pwm_set(&plain_dev, 0u, 1000u, 500u, 0u, NULL),
                      EMB_ESTATE); /* not READY */
    EMB_ASSERT_OK(emb_device_init(&plain_dev));
    EMB_ASSERT_STATUS(emb_drv_pwm_capture(&plain_dev, 0u, &wave, EMB_NO_WAIT), EMB_ENOTSUP);
    EMB_ASSERT_OK(emb_drv_pwm_set(&plain_dev, 0u, 1000u, 500u, 0u, NULL));
    ready();
    EMB_ASSERT_FAULTS((void)emb_drv_pwm_set(&pwm_dev, 0u, 1000u, 500u, 0x80u, NULL)); /* a flag */
    EMB_ASSERT_FAULTS((void)emb_drv_pwm_capture(&pwm_dev, 0u, NULL, EMB_NO_WAIT));
    EMB_ASSERT_FAULTS((void)emb_drv_pwm_stop(NULL, 0u));
    EMB_ASSERT_FAULTS((void)emb_drv_pwm_channels(&pwm_dev, NULL));
}
