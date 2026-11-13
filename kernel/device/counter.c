/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    counter.c
 * @brief   The counter class (SPEC-017 §11a, A129).
 *
 * @details
 * Its checks over a driver's operations, each channel's one-shot or periodic alarm, and a
 * periodic alarm's next period armed without drift.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_counter.h>

#include <embk/kernel.h>

/* The tries at arming a periodic alarm's next period, each a period later, before it stops: a
 * period shorter than the counter's interrupt takes cannot be kept. */
#define PERIODIC_TRIES 4u

static bool is_counter(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_COUNTER) &&
           (dev->data != NULL);
}

/* The class's operations: the device's first. MISRA Dev CS-12: rule 11.3. */
static const emb_drv_counter_api_t *counter_api(const emb_device_t *dev)
{
    return (const emb_drv_counter_api_t *)(const void *)dev->api;
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_counter_data_t *counter_part(const emb_device_t *dev)
{
    return (emb_drv_counter_data_t *)dev->data;
}

static bool has_channel(const emb_device_t *dev, uint8_t channel)
{
    return channel < counter_part(dev)->channels;
}

/* Arm a channel's alarm, interrupts locked: its callback, its count and its period. */
static emb_status_t arm_locked(const emb_device_t *dev, uint8_t channel, uint64_t at,
                               uint64_t period, emb_drv_counter_fn_t fn, void *arg)
{
    emb_drv_counter_alarm_t *a = &counter_part(dev)->alarm[channel];
    emb_status_t st;
    if (a->armed != 0u) {
        return EMB_EBUSY;
    }
    a->fn = fn;
    a->arg = arg;
    a->at = at;
    a->period = period;
    a->armed = 1u;
    st = counter_api(dev)->arm(dev, channel, at);
    if (st != EMB_OK) {
        a->armed = 0u;
    }
    return st;
}

/* A periodic alarm's next period, interrupts locked: one period after the last was due, or the
 * first still to come when the counter's interrupt was held off past some. */
static void periodic_next(const emb_device_t *dev, uint8_t channel, emb_drv_counter_alarm_t *a)
{
    const emb_drv_counter_api_t *api = counter_api(dev);
    uint64_t now = api->now(dev);
    uint64_t next = a->at + a->period;
    uint32_t tries;
    if (next <= now) {
        next += (((now - next) / a->period) + 1u) * a->period; /* the periods passed: skipped */
    }
    for (tries = 0u; tries < PERIODIC_TRIES; tries++) { /* bounded */
        if (api->arm(dev, channel, next) == EMB_OK) {
            a->at = next;
            return;
        }
        next += a->period; /* it passed while it was armed */
    }
    a->armed = 0u; /* a period the counter cannot keep: it stops */
}

emb_status_t emb_drv_counter_now(const emb_device_t *dev, uint64_t *count)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_NOW, is_counter(dev) && (count != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    *count = counter_api(dev)->now(dev);
    return EMB_OK;
}

emb_status_t emb_drv_counter_rate(const emb_device_t *dev, uint32_t *hz)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_RATE, is_counter(dev) && (hz != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    *hz = counter_api(dev)->rate(dev);
    return EMB_OK;
}

emb_status_t emb_drv_counter_channels(const emb_device_t *dev, uint8_t *count)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_CHANNELS, is_counter(dev) && (count != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    *count = counter_part(dev)->channels;
    return EMB_OK;
}

emb_status_t emb_drv_counter_alarm_set(const emb_device_t *dev, uint8_t channel, uint64_t at,
                                       emb_drv_counter_fn_t fn, void *arg)
{
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_ALARM_SET, is_counter(dev) && (fn != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    if (!has_channel(dev, channel)) {
        return EMB_EINVAL;
    }
    key = embk_irq_lock();
    st = arm_locked(dev, channel, at, 0u, fn, arg);
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_counter_periodic(const emb_device_t *dev, uint8_t channel, uint64_t period,
                                      emb_drv_counter_fn_t fn, void *arg)
{
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_PERIODIC, is_counter(dev) && (fn != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    if (!has_channel(dev, channel) || (period < 2u)) {
        return EMB_EINVAL;
    }
    key = embk_irq_lock();
    st = arm_locked(dev, channel, counter_api(dev)->now(dev) + period, period, fn, arg);
    embk_irq_unlock(key);
    return st;
}

/* Disarm a channel's alarm of one kind: one-shot (periodic false) or periodic. */
static emb_status_t disarm(const emb_device_t *dev, uint8_t channel, bool periodic)
{
    emb_drv_counter_alarm_t *a;
    emb_irq_key_t key;
    emb_status_t st = EMB_ESTATE;
    if (!has_channel(dev, channel)) {
        return EMB_EINVAL;
    }
    a = &counter_part(dev)->alarm[channel];
    key = embk_irq_lock();
    if ((a->armed != 0u) && ((a->period != 0u) == periodic)) {
        a->armed = 0u;
        counter_api(dev)->disarm(dev, channel);
        st = EMB_OK;
    }
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_counter_alarm_cancel(const emb_device_t *dev, uint8_t channel)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_ALARM_CANCEL, is_counter(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    return disarm(dev, channel, false);
}

emb_status_t emb_drv_counter_periodic_stop(const emb_device_t *dev, uint8_t channel)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_PERIODIC_STOP, is_counter(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    return disarm(dev, channel, true);
}

emb_status_t emb_drv_counter_started(const emb_device_t *dev, uint8_t channels)
{
    emb_drv_counter_data_t *d;
    uint8_t most;
    uint32_t i;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_STARTED, is_counter(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    d = counter_part(dev);
    most = counter_api(dev)->channels;
    if (most > EMB_DRV_COUNTER_CHANNELS_MAX) {
        most = (uint8_t)EMB_DRV_COUNTER_CHANNELS_MAX;
    }
    d->channels = ((channels == 0u) || (channels > most)) ? most : channels;
    for (i = 0u; i < EMB_DRV_COUNTER_CHANNELS_MAX; i++) { /* bounded */
        d->alarm[i].fn = NULL;
        d->alarm[i].arg = NULL;
        d->alarm[i].at = 0u;
        d->alarm[i].period = 0u;
        d->alarm[i].armed = 0u;
    }
    return EMB_OK;
}

emb_status_t emb_drv_counter_fired(const emb_device_t *dev, uint8_t channel)
{
    emb_drv_counter_alarm_t *a;
    emb_drv_counter_fn_t fn = NULL;
    void *arg = NULL;
    uint64_t at = 0u;
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_COUNTER_FIRED, is_counter(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    if (!has_channel(dev, channel)) {
        return EMB_EINVAL;
    }
    a = &counter_part(dev)->alarm[channel];
    key = embk_irq_lock();
    if (a->armed != 0u) {
        fn = a->fn;
        arg = a->arg;
        at = a->at;
        if (a->period == 0u) {
            a->armed = 0u; /* first: the callback may arm it again */
        } else {
            periodic_next(dev, channel, a); /* first: the callback may stop it */
        }
    }
    embk_irq_unlock(key);
    if (fn == NULL) {
        return EMB_ESTATE;
    }
    fn(arg, at);
    return EMB_OK;
}
