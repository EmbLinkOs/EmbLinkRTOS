/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_adc_board.c
 * @brief   The board's ADC, an input wired high and one low (SPEC-017 §11c, A129).
 *
 * @details
 * EMB_TEST_ADC's input EMB_TEST_ADC_HIGH is wired to the 3.3 V rail, EMB_TEST_ADC_LOW to ground:
 * one conversion of each, and their millivolts; the internal temperature sensor in a room's
 * range; the internal reference where it has one; and both sampled in turn by DMA at 1 kHz, every
 * half's samples in their order, the halves on time, none after the stop. Bench: the
 * NUCLEO-F446RE's A2 (PA4, input 4) to 3V3 and A3 (PB0, input 8) to GND; the Pico 2 W's pin 31
 * (GPIO26, input 0) to 3V3 OUT and pin 32 (GPIO27, input 1) to AGND.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <soc_devices.h>

#define ADC     EMB_TEST_ADC
#define HIGH    EMB_TEST_ADC_HIGH
#define LOW     EMB_TEST_ADC_LOW
#define WAIT    EMB_TIMEOUT(EMB_MS(100u))
#define RAIL_MV 3300u
#define SLACK   100u /* mV: the rail's tolerance and the converter's offset */
#define RATE    1000u
#define SAMPLES 64u /* 32 sequences: 16 a half, one each 16 ms at RATE */

/* a power of two bytes, aligned to its size: the RP2350's DMA ring wraps it */
static EMB_ALIGNED(128) uint16_t buffer[SAMPLES];
static volatile unsigned halves;
static volatile unsigned wrong; /* samples out of their order: high where low is, or the reverse */
static volatile emb_status_t ended;

static void on_half(void *arg, const uint16_t *samples, uint32_t count, emb_status_t status)
{
    uint32_t i;
    (void)arg;
    if (status != EMB_OK) {
        ended = status;
        return;
    }
    for (i = 0u; i < count; i += 2u) { /* bounded: a half */
        if ((samples[i] < 3500u) || (samples[i + 1u] > 300u)) {
            wrong++;
        }
    }
    halves++;
}

static uint32_t mv_of(uint8_t input)
{
    uint16_t count = 0u;
    uint32_t mv = 0u;
    EMB_ASSERT_OK(emb_drv_adc_read(ADC, input, &count, WAIT));
    EMB_ASSERT_OK(emb_drv_adc_to_mv(ADC, count, &mv));
    return mv;
}

EMB_TEST(adc_board_high_and_low)
{
    uint32_t high;
    uint32_t low;
    uint16_t count = 0u;
    EMB_TEST_REQ("DRV-001");
    EMB_ASSERT_TRUE(emb_device_ready(ADC));
    high = mv_of(HIGH);
    low = mv_of(LOW);
    EMB_ASSERT_TRUE((high + SLACK) >= RAIL_MV);
    EMB_ASSERT_TRUE(low <= SLACK);
    EMB_ASSERT_OK(emb_drv_adc_read_polled(ADC, LOW, &count));
    EMB_ASSERT_TRUE(count < 200u);
}

EMB_TEST(adc_board_internal_inputs)
{
    int32_t mdegc = 0;
    uint16_t count = 0u;
    uint32_t mv = 0u;
    EMB_TEST_REQ("DRV-001");
    EMB_ASSERT_OK(emb_drv_adc_temperature(ADC, &mdegc, WAIT));
    EMB_ASSERT_TRUE((mdegc > 5000) && (mdegc < 70000)); /* a bench's room, and the part's warmth */
    if ((emb_device_caps(ADC) & EMB_DRV_ADC_CAP_VREFINT) != 0u) {
        EMB_ASSERT_OK(emb_drv_adc_read(ADC, EMB_DRV_ADC_VREFINT, &count, WAIT));
        EMB_ASSERT_OK(emb_drv_adc_to_mv(ADC, count, &mv));
        EMB_ASSERT_TRUE((mv >= 1150u) && (mv <= 1250u)); /* 1.21 V, typically (DS10693) */
    } else {
        EMB_ASSERT_STATUS(emb_drv_adc_read(ADC, EMB_DRV_ADC_VREFINT, &count, WAIT), EMB_EINVAL);
    }
}

EMB_TEST(adc_board_sampled_in_turn)
{
    static const uint8_t inputs[2] = {HIGH, LOW};
    emb_drv_adc_sampling_t s = {inputs, buffer, SAMPLES, RATE, on_half, NULL, 2u, {0u, 0u, 0u}};
    uint16_t count = 0u;
    unsigned seen;
    EMB_TEST_REQ("DRV-001");
    halves = 0u;
    wrong = 0u;
    ended = EMB_OK;
    EMB_ASSERT_OK(emb_drv_adc_sample_start(ADC, &s));
    EMB_ASSERT_STATUS(emb_drv_adc_read(ADC, LOW, &count, EMB_NO_WAIT), EMB_EBUSY);
    (void)emb_thread_sleep(EMB_MS(200u)); /* twelve and a half halves */
    EMB_ASSERT_OK(emb_drv_adc_sample_stop(ADC));
    seen = halves;
    EMB_ASSERT_TRUE((seen >= 11u) && (seen <= 13u)); /* on time: a half each 16 ms */
    EMB_ASSERT_EQ(wrong, 0u);                        /* each sequence high, then low */
    EMB_ASSERT_TRUE(ended == EMB_OK);                /* no overrun */
    (void)emb_thread_sleep(EMB_MS(40u));
    EMB_ASSERT_EQ(halves, seen);                     /* none after the stop */
    EMB_ASSERT_TRUE(mv_of(HIGH) + SLACK >= RAIL_MV); /* the device's again */
}
