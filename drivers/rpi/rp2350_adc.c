/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_adc.c
 * @brief   The RP2350's ADC (datasheet §12.4; SPEC-017 §11c, A129).
 *
 * @details
 * A 12-bit SAR converter on clk_adc, which PLL_USB makes at 48 MHz: 96 clocks a conversion,
 * 500 kS/s at most. A conversion is START_ONCE, polled. A sampling is START_MANY, paced by the
 * ADC's own divider, round robin over the sequence's inputs, which therefore are in ascending
 * order (CAP_ASCENDING); its results go through the FIFO to a circular DMA job, and a FIFO
 * overflow ends it. The temperature sensor is read against the datasheet's typical slope.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_dma.h>
#include <emb/irq.h>

#include <emb_rp2350_devices.h>
#include <emb_rp2350_drivers.h>

#if CONFIG_EMB_DRV_ADC

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define ADC_CS     0x00u
#define ADC_RESULT 0x04u
#define ADC_FCS    0x08u
#define ADC_FIFO   0x0Cu
#define ADC_DIV    0x10u /* INT bits 23:8, FRAC bits 7:0 */

#define CS_EN         ((uint32_t)1u << 0)
#define CS_TS_EN      ((uint32_t)1u << 1)
#define CS_START_ONCE ((uint32_t)1u << 2)
#define CS_START_MANY ((uint32_t)1u << 3)
#define CS_READY      ((uint32_t)1u << 8)
#define CS_ERR        ((uint32_t)1u << 9)
#define CS_AINSEL     12u
#define CS_RROBIN     16u
#define FCS_EN        ((uint32_t)1u << 0)
#define FCS_DREQ_EN   ((uint32_t)1u << 3)
#define FCS_EMPTY     ((uint32_t)1u << 8)
#define FCS_UNDER     ((uint32_t)1u << 10)
#define FCS_OVER      ((uint32_t)1u << 11)
#define FCS_THRESH    24u
#define RESULT_MASK   0xFFFu
#define CONVERSION    96u     /* clk_adc cycles */
#define POLL_LIMIT    100000u /* bounded: a conversion's 2 us, many times over */
#define DRAIN_LIMIT   16u     /* the FIFO's 8, twice */
#define TEMP_MV_27    706u    /* the sensor at 27 C (datasheet §12.4.6) */
#define TEMP_UV_PER_C 1721u   /* its slope, falling */

/* MISRA Dev CS-12: rules 11.3 and 11.5, a device's configuration and RAM by its driver's type. */
static const emb_rp2350_adc_config_t *rp2350_adc_cfg(const emb_device_t *dev)
{
    return (const emb_rp2350_adc_config_t *)dev->config;
}

static emb_rp2350_adc_data_t *rp2350_adc_ram(const emb_device_t *dev)
{
    return (emb_rp2350_adc_data_t *)dev->data;
}

/* An input's AINSEL; false for one it does not have. */
static bool adc_ainsel(const emb_rp2350_adc_config_t *cfg, uint8_t input, uint32_t *ainsel)
{
    if (input == EMB_DRV_ADC_TEMPERATURE) {
        *ainsel = cfg->temperature;
        return true;
    }
    *ainsel = input;
    return input < cfg->inputs;
}

static bool adc_ready(uintptr_t base)
{
    uint32_t n;
    for (n = 0u; n < POLL_LIMIT; n++) { /* bounded */
        if ((REG32(base + ADC_CS) & CS_READY) != 0u) {
            return true;
        }
    }
    return false;
}

static void adc_drain(uintptr_t base)
{
    uint32_t n;
    for (n = 0u; (n < DRAIN_LIMIT) && ((REG32(base + ADC_FCS) & FCS_EMPTY) == 0u); n++) {
        (void)REG32(base + ADC_FIFO); /* bounded: the FIFO's depth */
    }
    REG32(base + ADC_FCS) |= FCS_OVER | FCS_UNDER; /* write one to clear */
}

static emb_status_t rp2350_adc_init(const emb_device_t *dev)
{
    const emb_rp2350_adc_config_t *cfg = rp2350_adc_cfg(dev);
    emb_rp2350_adc_data_t *d = rp2350_adc_ram(dev);
    d->dev = dev;
    if ((emb_rp2350_clk_adc_start() != 0) || (emb_rp2350_unreset(EMB_RP2350_RESET_ADC) != 0)) {
        return EMB_EIO;
    }
    REG32(cfg->base + ADC_FCS) = 0u;
    REG32(cfg->base + ADC_CS) = CS_EN;
    if (!adc_ready(cfg->base)) {
        return EMB_EIO;
    }
    return emb_drv_adc_started(dev);
}

static emb_status_t rp2350_adc_read(const emb_device_t *dev, uint8_t input, uint16_t *count)
{
    const emb_rp2350_adc_config_t *cfg = rp2350_adc_cfg(dev);
    uintptr_t base = cfg->base;
    uint32_t ainsel = 0u;
    uint32_t cs;
    if (!adc_ainsel(cfg, input, &ainsel)) {
        return EMB_EINVAL;
    }
    cs = CS_EN | (ainsel << CS_AINSEL) | ((input == EMB_DRV_ADC_TEMPERATURE) ? CS_TS_EN : 0u);
    REG32(base + ADC_CS) = cs;
    REG32(base + ADC_CS) = cs | CS_START_ONCE;
    if (!adc_ready(base)) {
        return EMB_EIO;
    }
    if ((REG32(base + ADC_CS) & CS_ERR) != 0u) {
        return EMB_EIO; /* the conversion's own error flag */
    }
    *count = (uint16_t)(REG32(base + ADC_RESULT) & RESULT_MASK);
    return EMB_OK;
}

static void rp2350_adc_sample_stop(const emb_device_t *dev);

static void rp2350_adc_dma_done(void *arg, emb_status_t status, uint32_t moved)
{
    emb_rp2350_adc_data_t *d = (emb_rp2350_adc_data_t *)arg;
    uintptr_t base = rp2350_adc_cfg(d->dev)->base;
    emb_status_t ended = status;
    if ((ended == EMB_OK) && ((REG32(base + ADC_FCS) & FCS_OVER) != 0u)) {
        ended = EMB_EOVERRUN; /* the FIFO overflowed: samples lost */
    }
    if (ended != EMB_OK) {
        rp2350_adc_sample_stop(d->dev);
        (void)emb_drv_adc_sampled(d->dev, 0u, ended);
        return;
    }
    (void)emb_drv_adc_sampled(d->dev, (moved == d->adc.length) ? 1u : 0u, EMB_OK);
}

static emb_status_t rp2350_adc_sample_start(const emb_device_t *dev,
                                            const emb_drv_adc_sampling_t *s)
{
    const emb_rp2350_adc_config_t *cfg = rp2350_adc_cfg(dev);
    emb_rp2350_adc_data_t *d = rp2350_adc_ram(dev);
    uintptr_t base = cfg->base;
    uint32_t mask = 0u;
    uint32_t first = 0u;
    uint32_t last = 0u;
    uint64_t total = (uint64_t)s->rate_hz * s->count; /* conversions a second */
    uint64_t div256;
    bool temperature = false;
    emb_drv_dma_job_t job;
    emb_status_t st;
    uint32_t i;
    if (cfg->dma_rx.dma == NULL) {
        return EMB_EINVAL; /* the board gives it no DMA channel */
    }
    for (i = 0u; i < s->count; i++) { /* bounded: EMB_DRV_ADC_SEQUENCE_MAX */
        uint32_t ainsel = 0u;
        if (!adc_ainsel(cfg, s->inputs[i], &ainsel) || ((i != 0u) && (ainsel <= last))) {
            return EMB_EINVAL; /* one it does not have, or out of the round robin's order */
        }
        if (i == 0u) {
            first = ainsel;
        }
        last = ainsel;
        mask |= (uint32_t)1u << ainsel;
        temperature = temperature || (s->inputs[i] == EMB_DRV_ADC_TEMPERATURE);
    }
    /* a conversion every clock_hz / total cycles: DIV is that, less one, in 256ths */
    if ((total == 0u) || (((uint64_t)cfg->clock_hz / total) < CONVERSION)) {
        return EMB_EINVAL; /* faster than it converts */
    }
    div256 = ((((uint64_t)cfg->clock_hz * 256u) + (total / 2u)) / total) - 256u;
    if (div256 > 0xFFFFFFu) {
        return EMB_EINVAL; /* slower than its divider reaches */
    }
    REG32(base + ADC_CS) = CS_EN;
    adc_drain(base);
    REG32(base + ADC_DIV) = (uint32_t)div256;
    REG32(base + ADC_FCS) = FCS_EN | FCS_DREQ_EN | ((uint32_t)1u << FCS_THRESH);
    job.src = base + ADC_FIFO;
    job.dst = (uintptr_t)s->buffer;
    job.count = s->length;
    job.dir = EMB_DRV_DMA_FROM_PERIPH;
    job.width = 2u;
    job.flags = EMB_DRV_DMA_CIRCULAR;
    job.reserved_ = 0u;
    st = emb_drv_dma_start(&cfg->dma_rx, &job, rp2350_adc_dma_done, d);
    if (st != EMB_OK) {
        REG32(base + ADC_FCS) = 0u;
        return st;
    }
    REG32(base + ADC_CS) = CS_EN | (temperature ? CS_TS_EN : 0u) | (first << CS_AINSEL) |
                           ((s->count > 1u) ? (mask << CS_RROBIN) : 0u) | CS_START_MANY;
    return EMB_OK;
}

/* Interrupts locked by the class; or in the DMA's interrupt, which ends the sampling. */
static void rp2350_adc_sample_stop(const emb_device_t *dev)
{
    const emb_rp2350_adc_config_t *cfg = rp2350_adc_cfg(dev);
    uintptr_t base = cfg->base;
    uint32_t moved = 0u;
    REG32(base + ADC_CS) = CS_EN; /* START_MANY off: the conversion in flight ends */
    (void)adc_ready(base);
    (void)emb_drv_dma_stop(&cfg->dma_rx, &moved);
    REG32(base + ADC_FCS) = 0u;
    adc_drain(base);
}

/* V = count x reference / 4095; T = 27 - (V - 0.706 V) / 1.721 mV (datasheet §12.4.6). */
static emb_status_t rp2350_adc_to_mdegc(const emb_device_t *dev, uint16_t count, int32_t *mdegc)
{
    int64_t uv = ((int64_t)count * rp2350_adc_cfg(dev)->vref_mv * 1000) / (int64_t)RESULT_MASK;
    *mdegc =
        (int32_t)(27000 - (((uv - ((int64_t)TEMP_MV_27 * 1000)) * 1000) / (int64_t)TEMP_UV_PER_C));
    return EMB_OK;
}

static uint32_t rp2350_adc_vref_mv(const emb_device_t *dev)
{
    return rp2350_adc_cfg(dev)->vref_mv;
}

const emb_drv_adc_api_t emb_rp2350_adc_api = {
    {rp2350_adc_init, NULL, NULL, NULL, NULL,
     EMB_DRV_ADC_CAP_TEMPERATURE | EMB_DRV_ADC_CAP_SAMPLE | EMB_DRV_ADC_CAP_ASCENDING},
    rp2350_adc_read,
    rp2350_adc_sample_start,
    rp2350_adc_sample_stop,
    rp2350_adc_to_mdegc,
    rp2350_adc_vref_mv,
    12u,
    {0u, 0u, 0u},
};

#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_adc_off_t;
#endif
