/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    drv_pwm.h
 * @brief   The PWM class (SPEC-017 §11b, A129).
 *
 * @details
 * A device's output channels, each a pulse of a width within a period, in nanoseconds, rounded
 * to the nearest the hardware makes, which is reported back; the channels of one device share
 * its period. And an input's period and pulse measured, waiting up to a timeout: one cycle
 * exactly where the hardware captures edges, an average over many where it counts them
 * (CAP_CAPTURE_AVERAGED).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_DRV_PWM_H
#define EMB_DRV_PWM_H

#include <emb/device.h>
#include <emb/drv_bus.h>
#include <emb/time.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The most channels a device has: the STM32's four, the RP2350 slice's two (A and B). */
#define EMB_DRV_PWM_CHANNELS_MAX 4u

/* An output's flags. */
#define EMB_DRV_PWM_INVERTED 0x01u /* low for the pulse, high for the rest of the period */

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_PWM_CAP_CAPTURE          0x01u /* it measures an input */
#define EMB_DRV_PWM_CAP_CAPTURE_AVERAGED 0x02u /* averaged over many cycles, not one exactly */

/* A wave: its period, and the width of its pulse, the time it is high. */
typedef struct emb_drv_pwm_wave {
    uint32_t period_ns;
    uint32_t pulse_ns;
} emb_drv_pwm_wave_t;

/* The class's part of a driver's RAM, its first members: the device's lock, held for a
 * capture; the period its channels share while one runs; the channels running and those
 * capturing, a bit each; the device's channels, which emb_drv_pwm_started() sets. */
typedef struct emb_drv_pwm_data {
    emb_drv_bus_t lock;
    uint32_t period_ns;
    uint8_t running;
    uint8_t capturing;
    uint8_t channels;
    uint8_t reserved_;
} emb_drv_pwm_data_t;

/* A PWM driver's operations: the device's, then a channel's output set at a period and a pulse,
 * rounded to the nearest the hardware makes and reported in @got, its output stopped, and an
 * input captured. set() and stop() run with interrupts locked; set() refuses a period beyond
 * the hardware's reach with EMB_EINVAL. capture() runs in the caller's thread under the
 * device's lock, NULL where the driver captures nothing; it refuses a channel it cannot capture
 * on with EMB_ENOTSUP, and gives EMB_ENOSIGNAL when no whole cycle came within @timeout. */
typedef struct emb_drv_pwm_api {
    emb_device_api_t dev;
    emb_status_t (*set)(const emb_device_t *dev, uint8_t channel, uint32_t period_ns,
                        uint32_t pulse_ns, uint32_t flags, emb_drv_pwm_wave_t *got);
    void (*stop)(const emb_device_t *dev, uint8_t channel);
    emb_status_t (*capture)(const emb_device_t *dev, uint8_t channel, emb_drv_pwm_wave_t *wave,
                            emb_timeout_t timeout);
    uint8_t channels; /* the most a device of the driver has */
    uint8_t reserved_[3];
} emb_drv_pwm_api_t;

/**
 * emb_drv_pwm_set() - Output a pulse on a channel, every period.
 * @dev:       A device of the PWM class.
 * @channel:   Which, from 0.
 * @period_ns: The period; the device's other running channels' too.
 * @pulse_ns:  The pulse's width, at most the period: 0 for a low output, the period for a high
 *             one (inverted, the reverse).
 * @flags:     EMB_DRV_PWM_INVERTED, or 0.
 * @got:       Receives the period and pulse the hardware makes, the nearest to those asked;
 *             may be NULL.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_PWM
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * A running channel takes the new pulse at its next period, without a glitch.
 *
 * Return: EMB_OK; EMB_EBUSY (another channel runs at another period, or the channel captures);
 * EMB_EINVAL (no such channel, a period of 0 or beyond the hardware's reach, or a pulse longer
 * than the period); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_pwm_set(const emb_device_t *dev, uint8_t channel, uint32_t period_ns,
                             uint32_t pulse_ns, uint32_t flags, emb_drv_pwm_wave_t *got);

/**
 * emb_drv_pwm_stop() - Stop a channel's output.
 * @dev:     A device of the PWM class.
 * @channel: Which.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_PWM
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Its pin is left at its inactive level: low, or high where it was inverted.
 *
 * Return: EMB_OK; EMB_ESTATE (it was not running); EMB_EINVAL (no such channel).
 */
emb_status_t emb_drv_pwm_stop(const emb_device_t *dev, uint8_t channel);

/**
 * emb_drv_pwm_capture() - Measure an input's period and pulse.
 * @dev:     A device of the PWM class with CAP_CAPTURE.
 * @channel: The input's channel.
 * @wave:    Receives its period and the width of its high pulse.
 * @timeout: How long to wait for the device and for whole cycles of the input.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(1) plus the input's cycles: one and a part, or the gate time an averaging driver
 *           takes, up to @timeout
 * @owns     none
 * @config   CONFIG_EMB_DRV_PWM
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Without CAP_CAPTURE_AVERAGED one cycle is measured exactly, at the timer's clock. With it, the
 * period and the pulse are averages over the cycles that fit in the driver's gate time, a
 * fraction of @timeout. An input that stays high or low has no cycle.
 *
 * Return: EMB_OK; EMB_ENOSIGNAL (no whole cycle came within @timeout); EMB_ETIMEDOUT (the device
 * stayed taken); EMB_ENOTSUP (no capture, or not on this channel); EMB_EBUSY (the channel
 * outputs); EMB_EINVAL (no such channel); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_pwm_capture(const emb_device_t *dev, uint8_t channel, emb_drv_pwm_wave_t *wave,
                                 emb_timeout_t timeout);

/**
 * emb_drv_pwm_channels() - The device's channels.
 * @dev:   A device of the PWM class.
 * @count: Receives how many; channels are numbered from 0.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_PWM
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK.
 */
emb_status_t emb_drv_pwm_channels(const emb_device_t *dev, uint8_t *count);

/**
 * emb_drv_pwm_started() - For drivers, at the end of init(): the class's part of the RAM made
 * ready, its lock made, no channel running.
 * @dev:      A device of the PWM class.
 * @channels: The device's channels; 0 for the driver's most (its operations' channels).
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_PWM
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; what making the lock returns otherwise.
 */
emb_status_t emb_drv_pwm_started(const emb_device_t *dev, uint8_t channels);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_PWM_H */
