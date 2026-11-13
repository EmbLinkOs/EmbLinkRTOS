/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_pwm_loop.c
 * @brief   A PWM output wired to a capture input (SPEC-017 §11b, A129).
 *
 * @details
 * EMB_TEST_PWM_OUT's channel drives EMB_TEST_PWM_IN's: each wave set is captured back within
 * TOLERANCE of what the hardware reported making, from 50 kHz to 100 Hz; inverted, the pulse is
 * the rest of the period; a steady output has no cycle; a running output is not captured.
 * Bench: the NUCLEO-F446RE's D12 (PA6, TIM3 channel 1) to D10 (PB6, TIM4 channel 1); the Pico 2
 * W's pin 19 (GPIO14, slice 7 A) to pin 22 (GPIO17, slice 0 B).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <soc_devices.h>

#define OUT    EMB_TEST_PWM_OUT
#define OUT_CH EMB_TEST_PWM_OUT_CH
#define IN     EMB_TEST_PWM_IN
#define IN_CH  EMB_TEST_PWM_IN_CH
#define WAIT   EMB_TIMEOUT(EMB_MS(400u))

/* A capture's error: a thousandth of the period, and 60 ns, a few clocks of either timer. */
static uint32_t tolerance(uint32_t period_ns)
{
    return (period_ns / 1000u) + 60u;
}

static bool near(uint32_t a, uint32_t b, uint32_t tol)
{
    return ((a > b) ? (a - b) : (b - a)) <= tol;
}

static void loop_once(uint32_t period_ns, uint32_t pulse_ns, uint32_t flags)
{
    emb_drv_pwm_wave_t got = {0u, 0u};
    emb_drv_pwm_wave_t wave = {0u, 0u};
    uint32_t high;
    EMB_ASSERT_OK(emb_drv_pwm_set(OUT, OUT_CH, period_ns, pulse_ns, flags, &got));
    EMB_ASSERT_TRUE(near(got.period_ns, period_ns, tolerance(period_ns))); /* its nearest */
    (void)emb_thread_sleep(EMB_MS(2u));                                    /* a few periods out */
    EMB_ASSERT_OK(emb_drv_pwm_capture(IN, IN_CH, &wave, WAIT));
    high = ((flags & EMB_DRV_PWM_INVERTED) != 0u) ? (got.period_ns - got.pulse_ns) : got.pulse_ns;
    EMB_ASSERT_TRUE(near(wave.period_ns, got.period_ns, tolerance(got.period_ns)));
    EMB_ASSERT_TRUE(near(wave.pulse_ns, high, tolerance(got.period_ns)));
    EMB_ASSERT_OK(emb_drv_pwm_stop(OUT, OUT_CH));
}

EMB_TEST(pwm_loop_waves_captured_back)
{
    EMB_TEST_REQ("DRV-001");
    EMB_ASSERT_TRUE(emb_device_ready(OUT) && emb_device_ready(IN));
    loop_once(1000000u, 250000u, 0u);   /* 1 kHz, a quarter */
    loop_once(100000u, 50000u, 0u);     /* 10 kHz, a half */
    loop_once(20000u, 15000u, 0u);      /* 50 kHz, three quarters */
    loop_once(10000000u, 1000000u, 0u); /* 100 Hz, a tenth */
}

EMB_TEST(pwm_loop_inverted)
{
    EMB_TEST_REQ("DRV-001");
    loop_once(1000000u, 250000u, EMB_DRV_PWM_INVERTED); /* high for three quarters */
}

EMB_TEST(pwm_loop_steady_and_busy)
{
    emb_drv_pwm_wave_t wave = {0u, 0u};
    EMB_TEST_REQ("DRV-001");
    EMB_ASSERT_OK(emb_drv_pwm_set(OUT, OUT_CH, 1000000u, 0u, 0u, NULL)); /* low throughout */
    EMB_ASSERT_STATUS(emb_drv_pwm_capture(IN, IN_CH, &wave, EMB_TIMEOUT(EMB_MS(100u))),
                      EMB_ENOSIGNAL);
    EMB_ASSERT_OK(emb_drv_pwm_set(OUT, OUT_CH, 1000000u, 1000000u, 0u, NULL)); /* high throughout */
    EMB_ASSERT_STATUS(emb_drv_pwm_capture(IN, IN_CH, &wave, EMB_TIMEOUT(EMB_MS(100u))),
                      EMB_ENOSIGNAL);
    EMB_ASSERT_STATUS(emb_drv_pwm_capture(OUT, OUT_CH, &wave, EMB_NO_WAIT),
                      EMB_EBUSY); /* it outputs */
    EMB_ASSERT_OK(emb_drv_pwm_stop(OUT, OUT_CH));
    EMB_ASSERT_STATUS(emb_drv_pwm_stop(OUT, OUT_CH), EMB_ESTATE);
}
