/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    adc.c
 * @brief   The ADC class (SPEC-017 §11c, A129).
 *
 * @details
 * Its checks over a driver's operations: the hardware a conversion's or a sampling's, never both;
 * readers queued on the device's lock; a count's millivolts; a sampling's halves handed to its
 * callback.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_adc.h>

#include <embk/drv_bus.h>
#include <embk/kernel.h>

static bool is_adc(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_ADC) &&
           (dev->data != NULL);
}

/* The class's operations: the device's first. MISRA Dev CS-12: rule 11.3. */
static const emb_drv_adc_api_t *adc_api(const emb_device_t *dev)
{
    return (const emb_drv_adc_api_t *)(const void *)dev->api;
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_adc_data_t *adc_part(const emb_device_t *dev)
{
    return (emb_drv_adc_data_t *)dev->data;
}

/* The hardware taken for one conversion: refused while another converts or a sampling runs. */
static emb_status_t adc_claim(emb_drv_adc_data_t *d)
{
    emb_status_t st = EMB_OK;
    emb_irq_key_t key = embk_irq_lock();
    if ((d->busy != 0u) || (d->sampling != 0u)) {
        st = EMB_EBUSY;
    } else {
        d->busy = 1u;
    }
    embk_irq_unlock(key);
    return st;
}

static void adc_release(emb_drv_adc_data_t *d)
{
    emb_irq_key_t key = embk_irq_lock();
    d->busy = 0u;
    embk_irq_unlock(key);
}

static emb_status_t adc_convert(const emb_device_t *dev, uint8_t input, uint16_t *count)
{
    emb_drv_adc_data_t *d = adc_part(dev);
    emb_status_t st = adc_claim(d);
    if (st == EMB_OK) {
        st = adc_api(dev)->read(dev, input, count);
        adc_release(d);
    }
    return st;
}

emb_status_t emb_drv_adc_read(const emb_device_t *dev, uint8_t input, uint16_t *count,
                              emb_timeout_t timeout)
{
    emb_drv_adc_data_t *d;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_READ, is_adc(dev) && (count != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_ADC_READ);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    d = adc_part(dev);
    st = embk_bus_enter(&d->lock, NULL, timeout); /* readers queue here */
    if (st != EMB_OK) {
        return st;
    }
    st = adc_convert(dev, input, count);
    embk_bus_exit(&d->lock);
    return st;
}

emb_status_t emb_drv_adc_read_polled(const emb_device_t *dev, uint8_t input, uint16_t *count)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_READ_POLLED, is_adc(dev) && (count != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    return adc_convert(dev, input, count);
}

emb_status_t emb_drv_adc_to_mv(const emb_device_t *dev, uint16_t count, uint32_t *mv)
{
    uint32_t top;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_TO_MV, is_adc(dev) && (mv != NULL), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    top = ((uint32_t)1u << adc_api(dev)->bits) - 1u;
    if (count > top) {
        return EMB_EINVAL;
    }
    *mv = (uint32_t)((((uint64_t)count * adc_api(dev)->vref_mv(dev)) + (top / 2u)) / top);
    return EMB_OK;
}

emb_status_t emb_drv_adc_temperature(const emb_device_t *dev, int32_t *mdegc, emb_timeout_t timeout)
{
    uint16_t count = 0u;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_TEMPERATURE, is_adc(dev) && (mdegc != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_ADC_TEMPERATURE);
    if (((emb_device_caps(dev) & EMB_DRV_ADC_CAP_TEMPERATURE) == 0u) ||
        (adc_api(dev)->to_mdegc == NULL)) {
        return EMB_ENOTSUP;
    }
    st = emb_drv_adc_read(dev, EMB_DRV_ADC_TEMPERATURE, &count, timeout);
    if (st == EMB_OK) {
        st = adc_api(dev)->to_mdegc(dev, count, mdegc);
    }
    return st;
}

emb_status_t emb_drv_adc_sample_start(const emb_device_t *dev, const emb_drv_adc_sampling_t *s)
{
    emb_drv_adc_data_t *d;
    emb_irq_key_t key;
    emb_status_t st = EMB_OK;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_SAMPLE_START,
                     is_adc(dev) && (s != NULL) && (s->fn != NULL) && (s->buffer != NULL) &&
                         (s->inputs != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_ADC_SAMPLE_START);
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    if (((emb_device_caps(dev) & EMB_DRV_ADC_CAP_SAMPLE) == 0u) ||
        (adc_api(dev)->sample_start == NULL)) {
        return EMB_ENOTSUP;
    }
    if ((s->count == 0u) || (s->count > EMB_DRV_ADC_SEQUENCE_MAX) || (s->rate_hz == 0u) ||
        (s->length < (2u * s->count)) || ((s->length % (2u * (uint32_t)s->count)) != 0u)) {
        return EMB_EINVAL; /* each half a whole number of sequences */
    }
    d = adc_part(dev);
    key = embk_irq_lock();
    if ((d->busy != 0u) || (d->sampling != 0u)) {
        st = EMB_EBUSY;
    } else {
        d->fn = s->fn;
        d->arg = s->arg;
        d->buffer = s->buffer;
        d->length = s->length;
        d->sampling = 1u;
    }
    embk_irq_unlock(key);
    if (st == EMB_OK) {
        st = adc_api(dev)->sample_start(dev, s);
        if (st != EMB_OK) {
            key = embk_irq_lock();
            d->sampling = 0u;
            embk_irq_unlock(key);
        }
    }
    return st;
}

emb_status_t emb_drv_adc_sample_stop(const emb_device_t *dev)
{
    emb_drv_adc_data_t *d;
    emb_irq_key_t key;
    emb_status_t st = EMB_ESTATE;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_SAMPLE_STOP, is_adc(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    d = adc_part(dev);
    key = embk_irq_lock();
    if (d->sampling != 0u) {
        d->sampling = 0u; /* first: a half reported meanwhile is dropped */
        adc_api(dev)->sample_stop(dev);
        st = EMB_OK;
    }
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_drv_adc_started(const emb_device_t *dev)
{
    emb_drv_adc_data_t *d;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_STARTED, is_adc(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    d = adc_part(dev);
    d->fn = NULL;
    d->arg = NULL;
    d->buffer = NULL;
    d->length = 0u;
    d->busy = 0u;
    d->sampling = 0u;
    return embk_bus_init(&d->lock, dev->name);
}

emb_status_t emb_drv_adc_sampled(const emb_device_t *dev, uint8_t half, emb_status_t status)
{
    emb_drv_adc_data_t *d;
    emb_drv_adc_fn_t fn = NULL;
    void *arg = NULL;
    const uint16_t *samples = NULL;
    uint32_t count = 0u;
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ADC_SAMPLED, is_adc(dev) && (half <= 1u),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    d = adc_part(dev);
    key = embk_irq_lock();
    if (d->sampling != 0u) {
        fn = d->fn;
        arg = d->arg;
        if (status == EMB_OK) {
            count = d->length / 2u;
            samples = &d->buffer[(half != 0u) ? count : 0u];
        } else {
            d->sampling = 0u; /* the hardware ended it */
        }
    }
    embk_irq_unlock(key);
    if (fn == NULL) {
        return EMB_ESTATE;
    }
    fn(arg, samples, count, status);
    return EMB_OK;
}
