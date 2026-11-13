/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_adc.c
 * @brief   The ADC class (SPEC-017 §11c, A129).
 *
 * @details
 * Its checks over a driver's operations, through a fake whose inputs hold set counts: a
 * conversion waiting and polled; millivolts; the temperature; a sampling's halves handed to its
 * callback, the device the sampling's meanwhile, an overrun ending it; a stop.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <string.h>

typedef struct fake_adc {
    emb_drv_adc_data_t adc; /* the class's, first */
    uint16_t value[4];      /* each input's count */
    bool sampling;
    uint8_t first; /* the sampling's first input */
} fake_adc_t;

static fake_adc_t fake;
static emb_device_state_t adc_state;

static emb_status_t fake_init(const emb_device_t *dev)
{
    return emb_drv_adc_started(dev);
}

static emb_status_t fake_read(const emb_device_t *dev, uint8_t input, uint16_t *count)
{
    (void)dev;
    if (input == EMB_DRV_ADC_TEMPERATURE) {
        *count = 2500u;
        return EMB_OK;
    }
    if (input >= 4u) {
        return EMB_EINVAL;
    }
    *count = fake.value[input];
    return EMB_OK;
}

static emb_status_t fake_sample_start(const emb_device_t *dev, const emb_drv_adc_sampling_t *s)
{
    (void)dev;
    if (s->inputs[0] >= 4u) {
        return EMB_EINVAL;
    }
    fake.first = s->inputs[0];
    fake.sampling = true;
    return EMB_OK;
}

static void fake_sample_stop(const emb_device_t *dev)
{
    (void)dev;
    fake.sampling = false;
}

static emb_status_t fake_to_mdegc(const emb_device_t *dev, uint16_t count, int32_t *mdegc)
{
    (void)dev;
    *mdegc = (int32_t)count * 10; /* 2500: 25 C */
    return EMB_OK;
}

static uint32_t fake_vref(const emb_device_t *dev)
{
    (void)dev;
    return 3300u;
}

static const emb_drv_adc_api_t fake_api = {
    {fake_init, NULL, NULL, NULL, NULL, EMB_DRV_ADC_CAP_TEMPERATURE | EMB_DRV_ADC_CAP_SAMPLE},
    fake_read,
    fake_sample_start,
    fake_sample_stop,
    fake_to_mdegc,
    fake_vref,
    12u,
    {0u, 0u, 0u},
};
static const emb_device_t adc_dev = {
    "adc", &fake_api.dev, NULL, &adc_state, &fake, NULL, EMB_DRV_CLASS_ADC, EMB_DEVICE_KERNEL, 0u};

static uint16_t buffer[8];
static unsigned halves;
static const uint16_t *last;
static uint32_t last_count;
static emb_status_t last_status;

static void on_half(void *arg, const uint16_t *samples, uint32_t count, emb_status_t status)
{
    (void)arg;
    halves++;
    last = samples;
    last_count = count;
    last_status = status;
}

/* The device started once, as at boot: its lock is made once; the fake reset each case. */
static void ready(void)
{
    static bool started;
    if (!started) {
        emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u,          0u, 0u, EMB_OK,
                                   {NULL, NULL},      {NULL, NULL}};
        adc_state = idle;
        EMB_ASSERT_OK(emb_device_init(&adc_dev));
        started = true;
    }
    (void)emb_drv_adc_sample_stop(&adc_dev);
    (void)memset((uint8_t *)&fake + sizeof(fake.adc), 0, sizeof(fake) - sizeof(fake.adc));
    halves = 0u;
    last = NULL;
    last_count = 0u;
    last_status = EMB_OK;
}

EMB_TEST(adc_read_millivolts_and_temperature)
{
    uint16_t count = 0u;
    uint32_t mv = 0u;
    int32_t mdegc = 0;
    EMB_TEST_REQ("DRV-001");
    ready();
    fake.value[2] = 4095u;
    EMB_ASSERT_OK(emb_drv_adc_read(&adc_dev, 2u, &count, EMB_TIMEOUT(EMB_MS(10))));
    EMB_ASSERT_EQ(count, 4095u);
    EMB_ASSERT_OK(emb_drv_adc_read_polled(&adc_dev, 2u, &count));
    EMB_ASSERT_STATUS(emb_drv_adc_read(&adc_dev, 9u, &count, EMB_NO_WAIT), EMB_EINVAL);
    EMB_ASSERT_OK(emb_drv_adc_to_mv(&adc_dev, 4095u, &mv));
    EMB_ASSERT_EQ(mv, 3300u); /* full scale: the reference */
    EMB_ASSERT_OK(emb_drv_adc_to_mv(&adc_dev, 2048u, &mv));
    EMB_ASSERT_EQ(mv, 1650u);
    EMB_ASSERT_OK(emb_drv_adc_to_mv(&adc_dev, 0u, &mv));
    EMB_ASSERT_EQ(mv, 0u);
    EMB_ASSERT_STATUS(emb_drv_adc_to_mv(&adc_dev, 4096u, &mv), EMB_EINVAL); /* past 12 bits */
    EMB_ASSERT_OK(emb_drv_adc_temperature(&adc_dev, &mdegc, EMB_NO_WAIT));
    EMB_ASSERT_TRUE(mdegc == 25000);
}

EMB_TEST(adc_sampling_halves_and_stop)
{
    static const uint8_t inputs[2] = {0u, 1u};
    emb_drv_adc_sampling_t s = {inputs, buffer, 8u, 1000u, on_half, NULL, 2u, {0u, 0u, 0u}};
    uint16_t count = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_adc_sample_start(&adc_dev, &s));
    EMB_ASSERT_TRUE(fake.sampling);
    EMB_ASSERT_STATUS(emb_drv_adc_sample_start(&adc_dev, &s), EMB_EBUSY);
    EMB_ASSERT_STATUS(emb_drv_adc_read(&adc_dev, 0u, &count, EMB_NO_WAIT), EMB_EBUSY);
    EMB_ASSERT_STATUS(emb_drv_adc_read_polled(&adc_dev, 0u, &count), EMB_EBUSY);
    EMB_ASSERT_OK(emb_drv_adc_sampled(&adc_dev, 0u, EMB_OK));
    EMB_ASSERT_TRUE((halves == 1u) && (last == &buffer[0]) && (last_count == 4u));
    EMB_ASSERT_OK(emb_drv_adc_sampled(&adc_dev, 1u, EMB_OK));
    EMB_ASSERT_TRUE((halves == 2u) && (last == &buffer[4]) && (last_count == 4u));
    EMB_ASSERT_OK(emb_drv_adc_sample_stop(&adc_dev));
    EMB_ASSERT_TRUE(!fake.sampling);
    EMB_ASSERT_STATUS(emb_drv_adc_sampled(&adc_dev, 0u, EMB_OK), EMB_ESTATE); /* nothing after */
    EMB_ASSERT_EQ(halves, 2u);
    EMB_ASSERT_STATUS(emb_drv_adc_sample_stop(&adc_dev), EMB_ESTATE);
    EMB_ASSERT_OK(emb_drv_adc_read(&adc_dev, 0u, &count, EMB_NO_WAIT)); /* the device's again */
}

EMB_TEST(adc_sampling_overrun_and_its_shape)
{
    static const uint8_t inputs[3] = {0u, 1u, 9u};
    emb_drv_adc_sampling_t s = {inputs, buffer, 8u, 1000u, on_half, NULL, 2u, {0u, 0u, 0u}};
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_adc_sample_start(&adc_dev, &s));
    fake.sampling = false; /* a driver stops its hardware, then reports the overrun */
    EMB_ASSERT_OK(emb_drv_adc_sampled(&adc_dev, 0u, EMB_EOVERRUN));
    EMB_ASSERT_TRUE((halves == 1u) && (last == NULL) && (last_status == EMB_EOVERRUN));
    EMB_ASSERT_STATUS(emb_drv_adc_sample_stop(&adc_dev), EMB_ESTATE);
    s.length = 6u; /* halves of one and a half sequences */
    EMB_ASSERT_STATUS(emb_drv_adc_sample_start(&adc_dev, &s), EMB_EINVAL);
    s.length = 8u;
    s.count = 0u;
    EMB_ASSERT_STATUS(emb_drv_adc_sample_start(&adc_dev, &s), EMB_EINVAL);
    s.count = (uint8_t)(EMB_DRV_ADC_SEQUENCE_MAX + 1u);
    EMB_ASSERT_STATUS(emb_drv_adc_sample_start(&adc_dev, &s), EMB_EINVAL);
    s.count = 1u;
    s.rate_hz = 0u;
    EMB_ASSERT_STATUS(emb_drv_adc_sample_start(&adc_dev, &s), EMB_EINVAL);
    s.rate_hz = 1000u;
    s.inputs = &inputs[2]; /* the driver refuses an input it lacks */
    EMB_ASSERT_STATUS(emb_drv_adc_sample_start(&adc_dev, &s), EMB_EINVAL);
    EMB_ASSERT_TRUE(!fake.sampling);
    s.inputs = inputs;
    EMB_ASSERT_OK(emb_drv_adc_sample_start(&adc_dev, &s)); /* the refusal took nothing */
}

EMB_TEST(adc_misuse)
{
    uint16_t count = 0u;
    uint32_t mv = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_FAULTS((void)emb_drv_adc_read(&adc_dev, 0u, NULL, EMB_NO_WAIT));
    EMB_ASSERT_FAULTS((void)emb_drv_adc_read_polled(NULL, 0u, &count));
    EMB_ASSERT_FAULTS((void)emb_drv_adc_to_mv(&adc_dev, 0u, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_adc_sample_start(&adc_dev, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_adc_sampled(&adc_dev, 2u, EMB_OK)); /* no third half */
    (void)mv;
}
