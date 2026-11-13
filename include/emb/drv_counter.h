/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    drv_counter.h
 * @brief   The counter class (SPEC-017 §11a, A129).
 *
 * @details
 * A free-running counter at a known rate, its count widened to 64 bits by the driver; one-shot
 * alarms on its channels at an absolute count; and periodic callbacks without drift. Each
 * callback runs in the counter's interrupt. The class is named counter so that it is not taken
 * for the kernel's software timers (<emb/timer.h>).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_DRV_COUNTER_H
#define EMB_DRV_COUNTER_H

#include <emb/device.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The most channels a counter has: the STM32's four compare channels, the RP2350's four alarms. */
#define EMB_DRV_COUNTER_CHANNELS_MAX 4u

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_COUNTER_CAP_64BIT 0x01u /* the hardware counts 64 bits: nothing to widen */

/* A callback, in the counter's interrupt: the count it was due at, which a periodic callback
 * advances by whole periods, so the gap between two is the periods a late one skipped. */
typedef void (*emb_drv_counter_fn_t)(void *arg, uint64_t at);

/* One channel's alarm, one-shot or periodic. */
typedef struct emb_drv_counter_alarm {
    emb_drv_counter_fn_t fn;
    void *arg;
    uint64_t at;     /* the count it fires at next */
    uint64_t period; /* 0 for a one-shot */
    uint8_t armed;
    uint8_t reserved_[7];
} emb_drv_counter_alarm_t;

/* The class's part of a driver's RAM, its first member: each channel's alarm, and the device's
 * channels, which emb_drv_counter_started() sets. */
typedef struct emb_drv_counter_data {
    emb_drv_counter_alarm_t alarm[EMB_DRV_COUNTER_CHANNELS_MAX];
    uint8_t channels;
    uint8_t reserved_[7];
} emb_drv_counter_data_t;

/* A counter driver's operations: the device's, then the count, its rate, and a channel's
 * hardware alarm armed at a full 64-bit count and disarmed. arm() and disarm() run with
 * interrupts locked. arm() refuses a count that has already passed with EMB_EPAST, checked
 * after it is armed so that a count passing meanwhile is not missed; it fires only once the
 * whole count is reached, whatever the hardware's width. A driver's interrupt calls
 * emb_drv_counter_fired() for each channel whose count was reached. */
typedef struct emb_drv_counter_api {
    emb_device_api_t dev;
    uint64_t (*now)(const emb_device_t *dev);
    uint32_t (*rate)(const emb_device_t *dev);
    emb_status_t (*arm)(const emb_device_t *dev, uint8_t channel, uint64_t at);
    void (*disarm)(const emb_device_t *dev, uint8_t channel);
    uint8_t channels; /* the most a device of the driver has */
    uint8_t reserved_[3];
} emb_drv_counter_api_t;

/**
 * emb_drv_counter_now() - The counter's count.
 * @dev:   A device of the counter class.
 * @count: Receives the count since the device started, widened to 64 bits.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_counter_now(const emb_device_t *dev, uint64_t *count);

/**
 * emb_drv_counter_rate() - The counter's rate.
 * @dev: A device of the counter class.
 * @hz:  Receives its counts per second.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_counter_rate(const emb_device_t *dev, uint32_t *hz);

/**
 * emb_drv_counter_channels() - The counter's alarm channels.
 * @dev:   A device of the counter class.
 * @count: Receives how many; channels are numbered from 0.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK.
 */
emb_status_t emb_drv_counter_channels(const emb_device_t *dev, uint8_t *count);

/**
 * emb_drv_counter_alarm_set() - Arm a channel's one-shot alarm.
 * @dev:     A device of the counter class.
 * @channel: Which, from 0.
 * @at:      The count it fires at, after the count now.
 * @fn:      Called in the counter's interrupt when the count reaches @at.
 * @arg:     Passed to @fn.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * It fires once, never before @at; @fn may arm it again.
 *
 * Return: EMB_OK; EMB_EPAST (@at has already passed, now or while it was armed: nothing is
 * armed); EMB_EBUSY (the channel is armed); EMB_EINVAL (no such channel); EMB_ESTATE (not
 * READY).
 */
emb_status_t emb_drv_counter_alarm_set(const emb_device_t *dev, uint8_t channel, uint64_t at,
                                       emb_drv_counter_fn_t fn, void *arg);

/**
 * emb_drv_counter_alarm_cancel() - Disarm a channel's one-shot alarm.
 * @dev:     A device of the counter class.
 * @channel: Which.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * No callback runs after it returns.
 *
 * Return: EMB_OK; EMB_ESTATE (no one-shot alarm armed: it fired, was never armed, or the channel
 * is periodic); EMB_EINVAL (no such channel).
 */
emb_status_t emb_drv_counter_alarm_cancel(const emb_device_t *dev, uint8_t channel);

/**
 * emb_drv_counter_periodic() - Call a function every period, without drift.
 * @dev:     A device of the counter class.
 * @channel: Which, from 0.
 * @period:  In counts; at least 2.
 * @fn:      Called in the counter's interrupt at each period's end.
 * @arg:     Passed to @fn.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * The first call is one period from now. Each next one is due one period after the last was
 * due, not after it ran, so the periods do not drift; one due while the counter's interrupt was
 * held off past it is skipped, and @fn's @at shows the gap.
 *
 * Return: EMB_OK; EMB_EBUSY (the channel is armed); EMB_EINVAL (no such channel, or a period of
 * 0 or 1); EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_counter_periodic(const emb_device_t *dev, uint8_t channel, uint64_t period,
                                      emb_drv_counter_fn_t fn, void *arg);

/**
 * emb_drv_counter_periodic_stop() - Stop a channel's periodic callback.
 * @dev:     A device of the counter class.
 * @channel: Which.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * No callback runs after it returns; the callback may stop itself.
 *
 * Return: EMB_OK; EMB_ESTATE (the channel is not periodic); EMB_EINVAL (no such channel).
 */
emb_status_t emb_drv_counter_periodic_stop(const emb_device_t *dev, uint8_t channel);

/**
 * emb_drv_counter_started() - For drivers, at the end of init(): the class's part of the RAM made
 * ready, no alarm armed.
 * @dev:      A device of the counter class.
 * @channels: The device's channels; 0 for the driver's most (its operations' channels).
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK.
 */
emb_status_t emb_drv_counter_started(const emb_device_t *dev, uint8_t channels);

/**
 * emb_drv_counter_fired() - For drivers, in the counter's interrupt: a channel's count reached.
 * @dev:     A device of the counter class.
 * @channel: The channel whose hardware alarm fired, the whole count reached.
 *
 * @ctx      isr
 * @blocks   no
 * @time     O(1) plus the callback
 * @owns     none
 * @config   CONFIG_EMB_DRV_COUNTER
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * A one-shot alarm is disarmed first, so its callback may arm it again; a periodic one is armed
 * for its next period first, skipping any already passed. Nothing runs if it was canceled.
 *
 * Return: EMB_OK; EMB_ESTATE (none armed); EMB_EINVAL (no such channel).
 */
emb_status_t emb_drv_counter_fired(const emb_device_t *dev, uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_COUNTER_H */
