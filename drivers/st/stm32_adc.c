/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    stm32_adc.c
 * @brief   The STM32F4's ADC1 as the ADC class's (RM0390 Rev 6 §13; SPEC-017 §11c, A129).
 *
 * @details
 * Each request sets the hardware up for itself: initialization only gates the clock, so an
 * application that drives ADC1 by hand (the reference controller) keeps it as it set it, until
 * it uses the class. A conversion is one regular conversion started by software and polled. A
 * sampling is a regular sequence, scanned, started by a timer's TRGO at the rate, its results
 * moved by a circular DMA job; an overrun ends it. The temperature sensor is read against the
 * factory calibration, its count scaled first to the 3.3 V the calibration was taken at.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/arch.h>
#include <emb/drv_dma.h>
#include <emb/irq.h>

#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_ADC

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define ADC_SR    0x00u
#define ADC_CR1   0x04u
#define ADC_CR2   0x08u
#define ADC_SMPR1 0x0Cu /* channels 10 to 18 */
#define ADC_SMPR2 0x10u /* channels 0 to 9 */
#define ADC_SQR1  0x2Cu /* the sequence's length, SQ13 to SQ16 */
#define ADC_SQR2  0x30u /* SQ7 to SQ12 */
#define ADC_SQR3  0x34u /* SQ1 to SQ6 */
#define ADC_DR    0x4Cu
#define ADC_CCR   0x04u /* in the common block */

#define SR_EOC     ((uint32_t)1u << 1)
#define SR_OVR     ((uint32_t)1u << 5)
#define CR1_SCAN   ((uint32_t)1u << 8)
#define CR1_OVRIE  ((uint32_t)1u << 26)
#define CR2_ADON   ((uint32_t)1u << 0)
#define CR2_DMA    ((uint32_t)1u << 8)
#define CR2_DDS    ((uint32_t)1u << 9) /* DMA requests go on: the circular job's */
#define CR2_EXTSEL 24u
#define CR2_EXTEN  ((uint32_t)1u << 28) /* the trigger's rising edge */
#define CR2_START  ((uint32_t)1u << 30) /* SWSTART */
#define CCR_PRE    16u                  /* ADCPRE: PCLK2 / 2, 4, 6 or 8 */
#define CCR_TSVREF ((uint32_t)1u << 23) /* the temperature sensor and VREFINT on */
#define CCR_VBAT   ((uint32_t)1u << 22) /* off: the sensor shares its input with VBAT */
#define ADC_MAX_HZ 36000000u            /* the ADC's clock, at most (DS10693 Table 74) */
#define SMP_LONG   7u                   /* 480 cycles: the internal inputs' 10 us at least */
#define SMP_EXT    4u                   /* 84 cycles: an external input's, at a few kilohms */
#define CONV_EXTRA 12u                  /* cycles a conversion takes beyond its sampling */
#define POLL_LIMIT 200000u              /* bounded: a conversion's 22 us, many times over */
#define SETTLE_US  10u                  /* the sensor's start-up (DS10693 §6.3.22) */
#define CAL_MV     3300u                /* the calibration's VDDA */

/* the timer's registers, for the trigger alone */
#define TIM_CR1  0x00u
#define TIM_CR2  0x04u
#define TIM_SR   0x10u
#define TIM_EGR  0x14u
#define TIM_PSC  0x28u
#define TIM_ARR  0x2Cu
#define TIM_CEN  ((uint32_t)1u << 0)
#define TIM_MMS  ((uint32_t)2u << 4) /* the update event as TRGO */
#define TIM_UG   ((uint32_t)1u << 0)
#define TIM_SPAN 65536u /* a 16-bit timer's reload and prescaler: what every one takes */

/* A sampling's sample times, in cycles, by SMP code. */
static const uint16_t smp_cycles[8] = {3u, 15u, 28u, 56u, 84u, 112u, 144u, 480u};

/* MISRA Dev CS-12: rules 11.3 and 11.5, a device's configuration and RAM by its driver's type. */
static const emb_stm32_adc_config_t *stm32_adc_cfg(const emb_device_t *dev)
{
    return (const emb_stm32_adc_config_t *)dev->config;
}

static emb_stm32_adc_data_t *stm32_adc_ram(const emb_device_t *dev)
{
    return (emb_stm32_adc_data_t *)dev->data;
}

/* The ADC's clock divider: the smallest that keeps it within ADC_MAX_HZ. */
static uint32_t adc_prescaler(uint32_t pclk_hz, uint32_t *code)
{
    uint32_t div;
    for (div = 2u; div < 8u; div += 2u) { /* bounded: 2, 4, 6 */
        if ((pclk_hz / div) <= ADC_MAX_HZ) {
            break;
        }
    }
    *code = (div / 2u) - 1u;
    return div;
}

/* An input's channel, and whether it is an internal one; false for one it does not have. */
static bool adc_channel(const emb_stm32_adc_config_t *cfg, uint8_t input, uint32_t *ch,
                        bool *internal)
{
    *internal = (input == EMB_DRV_ADC_TEMPERATURE) || (input == EMB_DRV_ADC_VREFINT);
    if (input == EMB_DRV_ADC_TEMPERATURE) {
        *ch = cfg->temperature;
    } else if (input == EMB_DRV_ADC_VREFINT) {
        *ch = cfg->vrefint;
    } else {
        *ch = input;
    }
    return *internal || (input < cfg->inputs);
}

static void adc_sample_time(uintptr_t base, uint32_t ch, uint32_t code)
{
    uintptr_t reg = base + ((ch >= 10u) ? ADC_SMPR1 : ADC_SMPR2);
    uint32_t shift = 3u * ((ch >= 10u) ? (ch - 10u) : ch);
    REG32(reg) = (REG32(reg) & ~((uint32_t)7u << shift)) | (code << shift);
}

/* The common block: the prescaler, and the internal inputs powered, settled once. */
static void adc_power(const emb_stm32_adc_config_t *cfg, emb_stm32_adc_data_t *d, bool internal)
{
    uint32_t code = 0u;
    (void)adc_prescaler(cfg->clock_hz, &code);
    if (internal) {
        REG32(cfg->common + ADC_CCR) =
            (REG32(cfg->common + ADC_CCR) & ~((uint32_t)3u << CCR_PRE) & ~CCR_VBAT) |
            (code << CCR_PRE) | CCR_TSVREF;
        if (d->sensors_on == 0u) {
            uint32_t cps = emb_arch_cycles_hz();
            emb_cycle_t start = emb_arch_cycles();
            while ((emb_cycle_t)(emb_arch_cycles() - start) < ((cps / 1000000u) * SETTLE_US)) {
                /* bounded: the sensor's start-up */
            }
            d->sensors_on = 1u;
        }
    } else {
        REG32(cfg->common + ADC_CCR) = (REG32(cfg->common + ADC_CCR) & ~((uint32_t)3u << CCR_PRE)) |
                                       (code << CCR_PRE);
    }
}

static void stm32_adc_sample_stop(const emb_device_t *dev);

static void stm32_adc_isr(void *arg)
{
    emb_stm32_adc_data_t *d = (emb_stm32_adc_data_t *)arg;
    const emb_stm32_adc_config_t *cfg = stm32_adc_cfg(d->dev);
    if ((REG32(cfg->base + ADC_SR) & SR_OVR) == 0u) {
        return; /* ADC2's or ADC3's, which share the line */
    }
    REG32(cfg->base + ADC_SR) = ~SR_OVR;
    stm32_adc_sample_stop(d->dev); /* the hardware lost samples: stopped, then the class told */
    (void)emb_drv_adc_sampled(d->dev, 0u, EMB_EOVERRUN);
}

static void stm32_adc_dma_done(void *arg, emb_status_t status, uint32_t moved)
{
    emb_stm32_adc_data_t *d = (emb_stm32_adc_data_t *)arg;
    if (status != EMB_OK) {
        stm32_adc_sample_stop(d->dev);
        (void)emb_drv_adc_sampled(d->dev, 0u, status);
        return;
    }
    (void)emb_drv_adc_sampled(d->dev, (moved == d->adc.length) ? 1u : 0u, EMB_OK);
}

static emb_status_t stm32_adc_init(const emb_device_t *dev)
{
    const emb_stm32_adc_config_t *cfg = stm32_adc_cfg(dev);
    emb_stm32_adc_data_t *d = stm32_adc_ram(dev);
    d->dev = dev;
    d->connected = 0u;
    d->sensors_on = 0u;
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit); /* the registers left as they are */
    return emb_drv_adc_started(dev);
}

static emb_status_t stm32_adc_read(const emb_device_t *dev, uint8_t input, uint16_t *count)
{
    const emb_stm32_adc_config_t *cfg = stm32_adc_cfg(dev);
    uintptr_t base = cfg->base;
    uint32_t ch = 0u;
    uint32_t n;
    bool internal = false;
    if (!adc_channel(cfg, input, &ch, &internal)) {
        return EMB_EINVAL;
    }
    adc_power(cfg, stm32_adc_ram(dev), internal);
    REG32(base + ADC_CR1) = 0u; /* 12 bits, one conversion */
    REG32(base + ADC_CR2) = CR2_ADON;
    adc_sample_time(base, ch, internal ? SMP_LONG : SMP_EXT);
    REG32(base + ADC_SQR1) = 0u;
    REG32(base + ADC_SQR3) = ch;
    REG32(base + ADC_SR) = 0u;
    REG32(base + ADC_CR2) = CR2_ADON | CR2_START;
    for (n = 0u; n < POLL_LIMIT; n++) { /* bounded */
        if ((REG32(base + ADC_SR) & SR_EOC) != 0u) {
            *count = (uint16_t)REG32(base + ADC_DR); /* the read clears EOC */
            return EMB_OK;
        }
    }
    return EMB_EIO;
}

/* The pacing timer at the rate, nearest: its update event, TRGO, starts each sequence. */
static void adc_trigger_at(const emb_stm32_adc_config_t *cfg, uint32_t rate_hz)
{
    uint64_t ticks = ((uint64_t)cfg->trigger_hz + (rate_hz / 2u)) / rate_hz;
    uint64_t psc = (ticks + TIM_SPAN - 1u) / TIM_SPAN;
    uint64_t arr;
    if (psc == 0u) {
        psc = 1u;
    }
    arr = (ticks + (psc / 2u)) / psc;
    emb_stm32_clock_enable(cfg->trigger_clken_offset, cfg->trigger_clken_bit);
    REG32(cfg->trigger + TIM_CR1) = 0u;
    REG32(cfg->trigger + TIM_PSC) = (uint32_t)(psc - 1u);
    REG32(cfg->trigger + TIM_ARR) = (uint32_t)((arr > 1u) ? (arr - 1u) : 1u);
    REG32(cfg->trigger + TIM_CR2) = TIM_MMS;
    REG32(cfg->trigger + TIM_EGR) = TIM_UG;
    REG32(cfg->trigger + TIM_SR) = 0u;
}

static emb_status_t stm32_adc_sample_start(const emb_device_t *dev, const emb_drv_adc_sampling_t *s)
{
    const emb_stm32_adc_config_t *cfg = stm32_adc_cfg(dev);
    emb_stm32_adc_data_t *d = stm32_adc_ram(dev);
    uintptr_t base = cfg->base;
    uint32_t code = 0u;
    uint32_t adc_hz = cfg->clock_hz / adc_prescaler(cfg->clock_hz, &code);
    uint32_t smp = 7u;
    uint32_t sq[3] = {0u, 0u, 0u}; /* SQR3, SQR2, SQR1 */
    bool internal_any = false;
    emb_drv_dma_job_t job;
    emb_status_t st;
    uint32_t i;
    if (cfg->dma_rx.dma == NULL) {
        return EMB_EINVAL; /* the board gives it no DMA channel */
    }
    /* the longest sample time whose sequence fits a period, with a tenth to spare */
    while ((smp > 0u) && (((uint64_t)s->count * (smp_cycles[smp] + CONV_EXTRA) * s->rate_hz * 10u) >
                          ((uint64_t)adc_hz * 9u))) { /* bounded: eight codes */
        smp--;
    }
    if (((uint64_t)s->count * (smp_cycles[smp] + CONV_EXTRA) * s->rate_hz) > adc_hz) {
        return EMB_EINVAL; /* faster than the ADC converts */
    }
    for (i = 0u; i < s->count; i++) { /* bounded: EMB_DRV_ADC_SEQUENCE_MAX */
        uint32_t ch = 0u;
        bool internal = false;
        if (!adc_channel(cfg, s->inputs[i], &ch, &internal)) {
            return EMB_EINVAL;
        }
        internal_any = internal_any || internal;
        sq[i / 6u] |= ch << (5u * (i % 6u));
    }
    adc_power(cfg, d, internal_any);
    if (d->connected == 0u) { /* the line taken now, not at initialization (the controller's) */
        if ((emb_irq_connect((emb_irq_t)cfg->irq, stm32_adc_isr, d) != EMB_OK) ||
            (emb_irq_enable((emb_irq_t)cfg->irq) != EMB_OK)) {
            return EMB_EINVAL;
        }
        d->connected = 1u;
    }
    REG32(base + ADC_CR2) = 0u;
    for (i = 0u; i < s->count; i++) { /* bounded */
        uint32_t ch = 0u;
        bool internal = false;
        (void)adc_channel(cfg, s->inputs[i], &ch, &internal);
        adc_sample_time(base, ch, smp);
    }
    REG32(base + ADC_SQR3) = sq[0];
    REG32(base + ADC_SQR2) = sq[1];
    REG32(base + ADC_SQR1) = sq[2] | (((uint32_t)s->count - 1u) << 20); /* L */
    REG32(base + ADC_CR1) = ((s->count > 1u) ? CR1_SCAN : 0u) | CR1_OVRIE;
    REG32(base + ADC_SR) = 0u;
    job.src = base + ADC_DR;
    job.dst = (uintptr_t)s->buffer;
    job.count = s->length;
    job.dir = EMB_DRV_DMA_FROM_PERIPH;
    job.width = 2u;
    job.flags = EMB_DRV_DMA_CIRCULAR;
    job.reserved_ = 0u;
    st = emb_drv_dma_start(&cfg->dma_rx, &job, stm32_adc_dma_done, d);
    if (st != EMB_OK) {
        return st;
    }
    adc_trigger_at(cfg, s->rate_hz);
    REG32(base + ADC_CR2) = CR2_ADON | CR2_DMA | CR2_DDS | CR2_EXTEN |
                            ((cfg->trigger_select & 15u) << CR2_EXTSEL);
    REG32(cfg->trigger + TIM_CR1) = TIM_CEN;
    return EMB_OK;
}

/* Interrupts locked by the class; or in the driver's own interrupt, which ends the sampling. */
static void stm32_adc_sample_stop(const emb_device_t *dev)
{
    const emb_stm32_adc_config_t *cfg = stm32_adc_cfg(dev);
    uint32_t moved = 0u;
    REG32(cfg->trigger + TIM_CR1) = 0u;
    REG32(cfg->base + ADC_CR2) = CR2_ADON; /* no trigger, no DMA */
    (void)emb_drv_dma_stop(&cfg->dma_rx, &moved);
    REG32(cfg->base + ADC_CR1) = 0u;
    REG32(cfg->base + ADC_SR) = 0u;
}

/* The sensor's count at the board's VDDA, scaled to the calibration's 3.3 V, then between the
 * two calibration points (emb_stm32f4_temp_mc(): 30 and 110 C). */
static emb_status_t stm32_adc_to_mdegc(const emb_device_t *dev, uint16_t count, int32_t *mdegc)
{
    const emb_stm32_adc_config_t *cfg = stm32_adc_cfg(dev);
    uint32_t at_calibration = (((uint32_t)count * cfg->vref_mv) + (CAL_MV / 2u)) / CAL_MV;
    int32_t t = emb_stm32f4_temp_mc(
        (uint16_t)((at_calibration > 0xFFFFu) ? 0xFFFFu : at_calibration), cfg->calibration);
    if (t == INT32_MIN) {
        return EMB_EIO; /* no calibration */
    }
    *mdegc = t;
    return EMB_OK;
}

static uint32_t stm32_adc_vref_mv(const emb_device_t *dev)
{
    return stm32_adc_cfg(dev)->vref_mv;
}

const emb_drv_adc_api_t emb_stm32_adc_api = {
    {stm32_adc_init, NULL, NULL, NULL, NULL,
     EMB_DRV_ADC_CAP_TEMPERATURE | EMB_DRV_ADC_CAP_CALIBRATED | EMB_DRV_ADC_CAP_VREFINT |
         EMB_DRV_ADC_CAP_SAMPLE},
    stm32_adc_read,
    stm32_adc_sample_start,
    stm32_adc_sample_stop,
    stm32_adc_to_mdegc,
    stm32_adc_vref_mv,
    12u,
    {0u, 0u, 0u},
};

#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_adc_off_t;
#endif
