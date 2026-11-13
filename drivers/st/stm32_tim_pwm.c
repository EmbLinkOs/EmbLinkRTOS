/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    stm32_tim_pwm.c
 * @brief   STM32 general-purpose timers as PWM (RM0390 Rev 6 §17, §18; SPEC-017 §11b, A129).
 *
 * @details
 * Outputs: each channel in PWM mode 1, its compare and the period preloaded, so a change takes
 * effect at the next period without a glitch. The prescaler is the smallest that brings the
 * period within the counter's width, so the pulse has the finest steps. A stopped channel is
 * forced to its inactive level, which its polarity sets.
 *
 * Capture: channel 0's or channel 1's pin on both channels, as PWM input mode pairs them: its
 * rising edge captured on its own channel, its falling edge on the other. The counter runs free,
 * the update interrupt counting its wraps, which extend each capture to a count that never
 * wraps; a cycle is a rising edge, a falling edge and the next rising edge, measured exactly at
 * the timer's clock, however long. Timestamping each edge, rather than resetting the counter at
 * one, keeps a late interrupt from moving the cycle's start.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_PWM

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define TIM_CR1   0x00u
#define TIM_SMCR  0x08u
#define TIM_DIER  0x0Cu
#define TIM_SR    0x10u
#define TIM_EGR   0x14u
#define TIM_CCMR1 0x18u /* CCMR2 at 0x1C: channels 2 and 3 */
#define TIM_CCER  0x20u
#define TIM_PSC   0x28u
#define TIM_ARR   0x2Cu
#define TIM_CCR1  0x34u /* CCR2 to CCR4 follow, 4 bytes apart */

#define CR1_CEN ((uint32_t)1u << 0)
#define CR1_URS \
    ((uint32_t)1u << 2) /* only an overflow raises the update interrupt: not a slave reset */
#define CR1_ARPE   ((uint32_t)1u << 7)
#define UPDATE     ((uint32_t)1u << 0) /* DIER.UIE, SR.UIF */
#define EGR_UG     ((uint32_t)1u << 0)
#define OC_MASK    ((uint32_t)0xFFu) /* a channel's byte in CCMRx: CCxS, OCxFE, OCxPE, OCxM, OCxCE */
#define OC_PWM1    ((uint32_t)6u << 4) /* OCxM 110 */
#define OC_INACT   ((uint32_t)4u << 4) /* OCxM 100: forced to the inactive level */
#define OC_PRELOAD ((uint32_t)1u << 3) /* OCxPE */
#define IC_DIRECT  ((uint32_t)1u)      /* CCxS 01: the channel's own input */
#define IC_OTHER   ((uint32_t)2u)      /* CCxS 10: the paired channel's input */
#define PSC_MAX    65536u
#define NS         1000000000u

/* A channel's bit in DIER (CCxIE) and in SR (CCxIF): bits 1 to 4. */
static uint32_t pwm_cc_bit(uint8_t channel)
{
    return (uint32_t)1u << (1u + (uint32_t)channel);
}

/* MISRA Dev CS-12: rules 11.3 and 11.5, a device's configuration and RAM by its driver's type. */
static const emb_stm32_tim_config_t *pwm_tim_config(const emb_device_t *dev)
{
    return (const emb_stm32_tim_config_t *)dev->config;
}

static emb_stm32_pwm_data_t *pwm_data(const emb_device_t *dev)
{
    return (emb_stm32_pwm_data_t *)dev->data;
}

/* The counts in one wrap of the counter. */
static uint64_t pwm_span(const emb_stm32_tim_config_t *cfg)
{
    return (uint64_t)1u << ((cfg->bits >= 32u) ? 32u : cfg->bits);
}

static uint64_t clocks_of(uint32_t ns, uint32_t hz)
{
    return (((uint64_t)ns * hz) + (NS / 2u)) / NS;
}

static uint32_t ns_of(uint64_t clocks, uint32_t hz)
{
    uint64_t ns = ((clocks * NS) + (hz / 2u)) / hz;
    return (ns > UINT32_MAX) ? UINT32_MAX : (uint32_t)ns;
}

/* A channel's output mode, its byte in CCMR1 or CCMR2. */
static void pwm_oc_mode(uintptr_t base, uint8_t channel, uint32_t mode)
{
    uintptr_t reg = base + TIM_CCMR1 + (4u * ((uint32_t)channel >> 1));
    uint32_t shift = 8u * ((uint32_t)channel & 1u);
    REG32(reg) = (REG32(reg) & ~(OC_MASK << shift)) | (mode << shift);
}

/* An edge's capture, extended by the wraps: a wrap the interrupt has not counted yet came before
 * the edge when the edge's count is in the counter's lower half. */
static uint64_t pwm_extended(const emb_stm32_pwm_data_t *d, uint64_t wrap, bool wrapped, uint32_t v)
{
    return ((d->wraps + ((wrapped && ((uint64_t)v < (wrap / 2u))) ? 1u : 0u)) * wrap) + v;
}

static void pwm_falling(emb_stm32_pwm_data_t *d, uint64_t at)
{
    if ((d->rises != 0u) && (at > d->rise_at)) {
        d->fall_at = at;
        d->fallen = 1u;
    }
}

static void pwm_rising(emb_stm32_pwm_data_t *d, const emb_stm32_tim_config_t *cfg, uint64_t at)
{
    if ((d->rises != 0u) && (d->fallen != 0u)) { /* a whole cycle: rise, fall, rise */
        d->period = at - d->rise_at;
        d->pulse = d->fall_at - d->rise_at;
        REG32(cfg->base + TIM_DIER) = 0u;
        (void)emb_sem_give(d->done);
    }
    d->rise_at = at;
    d->rises = 1u;
    d->fallen = 0u;
}

static void stm32_pwm_isr(void *arg)
{
    emb_stm32_pwm_data_t *d = (emb_stm32_pwm_data_t *)arg;
    const emb_stm32_tim_config_t *cfg = pwm_tim_config(d->dev);
    uintptr_t base = cfg->base;
    uint8_t rise_ch = d->input;
    uint8_t fall_ch = (uint8_t)(1u - d->input);
    uint32_t flags = REG32(base + TIM_SR) & REG32(base + TIM_DIER);
    bool wrapped = (flags & UPDATE) != 0u;
    bool falling = (flags & pwm_cc_bit(fall_ch)) != 0u;
    bool rising = (flags & pwm_cc_bit(rise_ch)) != 0u;
    uint64_t wrap = pwm_span(cfg);
    uint64_t fall_at = 0u;
    uint64_t rise_at = 0u;
    if (falling) {
        fall_at = pwm_extended(d, wrap, wrapped, REG32(base + TIM_CCR1 + (4u * (uint32_t)fall_ch)));
    }
    if (rising) {
        rise_at = pwm_extended(d, wrap, wrapped, REG32(base + TIM_CCR1 + (4u * (uint32_t)rise_ch)));
    }
    /* both edges at once, the interrupt late: taken in the order they came */
    if (falling && rising && (fall_at > rise_at)) {
        pwm_rising(d, cfg, rise_at);
        pwm_falling(d, fall_at);
    } else {
        if (falling) {
            pwm_falling(d, fall_at);
        }
        if (rising) {
            pwm_rising(d, cfg, rise_at);
        }
    }
    if (wrapped) {
        REG32(base + TIM_SR) = ~UPDATE;
        d->wraps++;
    }
}

static emb_status_t stm32_pwm_init(const emb_device_t *dev)
{
    const emb_stm32_tim_config_t *cfg = pwm_tim_config(dev);
    emb_stm32_pwm_data_t *d = pwm_data(dev);
    emb_sem_attr_t attr;
    emb_status_t st;
    d->dev = dev;
    d->prescale = 1u;
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit);
    REG32(cfg->base + TIM_CR1) = CR1_URS;
    REG32(cfg->base + TIM_SMCR) = 0u;
    REG32(cfg->base + TIM_DIER) = 0u;
    REG32(cfg->base + TIM_CCER) = 0u;
    st = emb_drv_pwm_started(dev, (uint8_t)cfg->channels);
    if (st != EMB_OK) {
        return st;
    }
    emb_sem_attr_default(&attr);
    attr.name = dev->name;
    attr.initial = 0u;
    attr.max = 1u;
    attr.flags = EMB_SEM_SATURATE;
    st = emb_sem_init(&d->done_storage, &attr, &d->done);
    if (st != EMB_OK) {
        return st;
    }
    if ((emb_irq_connect((emb_irq_t)cfg->irq, stm32_pwm_isr, d) != EMB_OK) ||
        (emb_irq_enable((emb_irq_t)cfg->irq) != EMB_OK)) {
        return EMB_EINVAL;
    }
    return EMB_OK;
}

/* Interrupts are locked by the class. With no other channel running, the period is the new one:
 * the prescaler and the reload, loaded at once. With one, it is theirs, and only the pulse
 * changes, at the next period. */
static emb_status_t stm32_pwm_set(const emb_device_t *dev, uint8_t channel, uint32_t period_ns,
                                  uint32_t pulse_ns, uint32_t flags, emb_drv_pwm_wave_t *got)
{
    const emb_stm32_tim_config_t *cfg = pwm_tim_config(dev);
    emb_stm32_pwm_data_t *d = pwm_data(dev);
    uintptr_t base = cfg->base;
    bool alone = (d->pwm.running & (uint8_t) ~((uint32_t)1u << channel)) == 0u;
    uint32_t prescale = d->prescale;
    uint64_t top;
    uint64_t compare;
    uint32_t polarity = 2u << (4u * (uint32_t)channel); /* CCxP */
    if (alone) {
        uint64_t clocks = clocks_of(period_ns, cfg->clock_hz);
        prescale = (uint32_t)((clocks + pwm_span(cfg) - 1u) / pwm_span(cfg));
        if (prescale == 0u) {
            prescale = 1u;
        }
        if ((prescale > PSC_MAX) || (((clocks + (prescale / 2u)) / prescale) < 2u)) {
            return EMB_EINVAL; /* longer than the counter reaches, or shorter than two counts */
        }
        top = (clocks + (prescale / 2u)) / prescale;
    } else {
        top = (uint64_t)REG32(base + TIM_ARR) + 1u;
    }
    compare = (pulse_ns >= period_ns)
                  ? top
                  : ((clocks_of(pulse_ns, cfg->clock_hz) + (prescale / 2u)) / prescale);
    if (compare > top) {
        compare = top;
    }
    REG32(base + TIM_CCR1 + (4u * (uint32_t)channel)) = (uint32_t)compare;
    pwm_oc_mode(base, channel, OC_PWM1 | OC_PRELOAD);
    REG32(base + TIM_CCER) = (REG32(base + TIM_CCER) & ~(polarity)) |
                             (((flags & EMB_DRV_PWM_INVERTED) != 0u) ? polarity : 0u) |
                             ((uint32_t)1u << (4u * (uint32_t)channel)); /* CCxE */
    if (alone) {
        d->prescale = prescale;
        REG32(base + TIM_SMCR) = 0u; /* a capture's slave reset, gone */
        REG32(base + TIM_PSC) = prescale - 1u;
        REG32(base + TIM_ARR) = (uint32_t)(top - 1u);
        REG32(base + TIM_CR1) = CR1_URS | CR1_ARPE;
        REG32(base + TIM_EGR) = EGR_UG; /* all loaded now */
        REG32(base + TIM_CR1) = CR1_URS | CR1_ARPE | CR1_CEN;
    }
    if (got != NULL) {
        got->period_ns = ns_of(top * prescale, cfg->clock_hz);
        got->pulse_ns = ns_of(compare * prescale, cfg->clock_hz);
    }
    return EMB_OK;
}

static void stm32_pwm_stop(const emb_device_t *dev, uint8_t channel)
{
    pwm_oc_mode(pwm_tim_config(dev)->base, channel, OC_INACT); /* its polarity still applies */
}

/* Under the device's lock; no channel of the device outputs, as the capture resets the counter. */
static emb_status_t stm32_pwm_capture(const emb_device_t *dev, uint8_t channel,
                                      emb_drv_pwm_wave_t *wave, emb_timeout_t timeout)
{
    const emb_stm32_tim_config_t *cfg = pwm_tim_config(dev);
    emb_stm32_pwm_data_t *d = pwm_data(dev);
    uintptr_t base = cfg->base;
    emb_irq_key_t key;
    emb_status_t st;
    if (channel > 1u) {
        return EMB_ENOTSUP; /* PWM input mode pairs channels 0 and 1 */
    }
    key = emb_irq_lock();
    if (d->pwm.running != 0u) {
        emb_irq_unlock(key);
        return EMB_EBUSY;
    }
    REG32(base + TIM_CR1) = CR1_URS;
    REG32(base + TIM_DIER) = 0u;
    REG32(base + TIM_CCER) = 0u; /* the inputs chosen with their channels off */
    REG32(base + TIM_CCMR1) = (channel == 0u) ? (IC_DIRECT | (IC_OTHER << 8))
                                              : (IC_OTHER | (IC_DIRECT << 8));
    /* the rising edge on the input's own channel, the falling edge on the other: CCxP */
    REG32(base + TIM_CCER) =
        (channel == 0u) ? (((uint32_t)1u << 0) | ((uint32_t)1u << 4) | ((uint32_t)1u << 5))
                        : (((uint32_t)1u << 0) | ((uint32_t)1u << 1) | ((uint32_t)1u << 4));
    REG32(base + TIM_SMCR) = 0u; /* the counter runs free: each edge is timestamped */
    REG32(base + TIM_PSC) = 0u;
    REG32(base + TIM_ARR) = (uint32_t)(pwm_span(cfg) - 1u);
    REG32(base + TIM_EGR) = EGR_UG;
    REG32(base + TIM_SR) = 0u;
    d->prescale = 1u;
    d->input = channel;
    d->rises = 0u;
    d->fallen = 0u;
    d->wraps = 0u;
    d->rise_at = 0u;
    d->fall_at = 0u;
    (void)emb_sem_take(d->done, EMB_NO_WAIT); /* a give a timed-out capture left */
    REG32(base + TIM_DIER) = UPDATE | pwm_cc_bit(0u) | pwm_cc_bit(1u);
    REG32(base + TIM_CR1) = CR1_URS | CR1_CEN;
    emb_irq_unlock(key);
    st = emb_sem_take(d->done, timeout);
    key = emb_irq_lock();
    REG32(base + TIM_DIER) = 0u;
    REG32(base + TIM_CR1) = CR1_URS;
    REG32(base + TIM_SMCR) = 0u;
    REG32(base + TIM_CCER) = 0u;
    REG32(base + TIM_CCMR1) = 0u;
    emb_irq_unlock(key);
    if (st != EMB_OK) {
        return (st == EMB_ETIMEDOUT) ? EMB_ENOSIGNAL : st;
    }
    wave->period_ns = ns_of(d->period, cfg->clock_hz);
    wave->pulse_ns = ns_of(d->pulse, cfg->clock_hz);
    return EMB_OK;
}

const emb_drv_pwm_api_t emb_stm32_pwm_api = {
    {stm32_pwm_init, NULL, NULL, NULL, NULL, EMB_DRV_PWM_CAP_CAPTURE},
    stm32_pwm_set,
    stm32_pwm_stop,
    stm32_pwm_capture,
    4u,
    {0u, 0u, 0u},
};

#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_tim_pwm_off_t;
#endif
