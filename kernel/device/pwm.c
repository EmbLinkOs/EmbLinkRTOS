/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    pwm.c
 * @brief   The PWM class (SPEC-017 §11b, A129).
 *
 * @details
 * Its checks over a driver's operations: the period a device's running channels share, a
 * channel outputting or capturing but not both, and a capture under the device's lock.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_pwm.h>

#include <embk/drv_bus.h>
#include <embk/kernel.h>

static bool is_pwm(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_PWM) &&
           (dev->data != NULL);
}

/* The class's operations: the device's first. MISRA Dev CS-12: rule 11.3. */
static const emb_drv_pwm_api_t *pwm_api(const emb_device_t *dev)
{
    return (const emb_drv_pwm_api_t *)(const void *)dev->api;
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_pwm_data_t *pwm_part(const emb_device_t *dev)
{
    return (emb_drv_pwm_data_t *)dev->data;
}

static uint8_t bit_of(uint8_t channel)
{
    return (uint8_t)(1u << channel);
}

emb_status_t emb_drv_pwm_set(const emb_device_t *dev, uint8_t channel, uint32_t period_ns,
                             uint32_t pulse_ns, uint32_t flags, emb_drv_pwm_wave_t *got)
{
    emb_drv_pwm_data_t *d;
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_PWM_SET,
                     is_pwm(dev) && ((flags & ~(uint32_t)EMB_DRV_PWM_INVERTED) == 0u),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    d = pwm_part(dev);
    if ((channel >= d->channels) || (period_ns == 0u) || (pulse_ns > period_ns)) {
        return EMB_EINVAL;
    }
    key = embk_irq_lock();
    /* the device's period is its running channels'; a capturing channel cannot output */
    if ((((d->running & (uint8_t)~bit_of(channel)) != 0u) && (period_ns != d->period_ns)) ||
        ((d->capturing & bit_of(channel)) != 0u)) {
        st = EMB_EBUSY;
    } else {
        st = pwm_api(dev)->set(dev, channel, period_ns, pulse_ns, flags, got);
        if (st == EMB_OK) {
            d->running |= bit_of(channel);
            d->period_ns = period_ns;
        }
    }
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_pwm_stop(const emb_device_t *dev, uint8_t channel)
{
    emb_drv_pwm_data_t *d;
    emb_irq_key_t key;
    emb_status_t st = EMB_ESTATE;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_PWM_STOP, is_pwm(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    d = pwm_part(dev);
    if (channel >= d->channels) {
        return EMB_EINVAL;
    }
    key = embk_irq_lock();
    if ((d->running & bit_of(channel)) != 0u) {
        pwm_api(dev)->stop(dev, channel);
        d->running &= (uint8_t)~bit_of(channel);
        st = EMB_OK;
    }
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_pwm_capture(const emb_device_t *dev, uint8_t channel, emb_drv_pwm_wave_t *wave,
                                 emb_timeout_t timeout)
{
    emb_drv_pwm_data_t *d;
    emb_instant_t start;
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_PWM_CAPTURE, is_pwm(dev) && (wave != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_PWM_CAPTURE);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    d = pwm_part(dev);
    if (channel >= d->channels) {
        return EMB_EINVAL;
    }
    if ((pwm_api(dev)->capture == NULL) ||
        ((emb_device_caps(dev) & EMB_DRV_PWM_CAP_CAPTURE) == 0u)) {
        return EMB_ENOTSUP;
    }
    start = emb_time_now();
    st = embk_bus_enter(&d->lock, NULL, timeout);
    if (st != EMB_OK) {
        return st;
    }
    key = embk_irq_lock();
    if ((d->running & bit_of(channel)) != 0u) {
        st = EMB_EBUSY; /* it outputs */
    } else {
        d->capturing |= bit_of(channel);
    }
    embk_irq_unlock(key);
    if (st == EMB_OK) {
        st = pwm_api(dev)->capture(dev, channel, wave, embk_bus_time_left(timeout, start));
        key = embk_irq_lock();
        d->capturing &= (uint8_t)~bit_of(channel);
        embk_irq_unlock(key);
    }
    embk_bus_exit(&d->lock);
    return st;
}

emb_status_t emb_drv_pwm_channels(const emb_device_t *dev, uint8_t *count)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_PWM_CHANNELS, is_pwm(dev) && (count != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    *count = pwm_part(dev)->channels;
    return EMB_OK;
}

emb_status_t emb_drv_pwm_started(const emb_device_t *dev, uint8_t channels)
{
    emb_drv_pwm_data_t *d;
    uint8_t most;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_PWM_STARTED, is_pwm(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    d = pwm_part(dev);
    most = pwm_api(dev)->channels;
    if (most > EMB_DRV_PWM_CHANNELS_MAX) {
        most = (uint8_t)EMB_DRV_PWM_CHANNELS_MAX;
    }
    d->channels = ((channels == 0u) || (channels > most)) ? most : channels;
    d->period_ns = 0u;
    d->running = 0u;
    d->capturing = 0u;
    return embk_bus_init(&d->lock, dev->name);
}
