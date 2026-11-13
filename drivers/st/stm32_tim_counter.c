/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    stm32_tim_counter.c
 * @brief   STM32 general-purpose timers as counters (RM0390 Rev 6 §17, §18; SPEC-017 §11a, A129).
 *
 * @details
 * TIM2 to TIM5, and TIM9 to TIM14 with their fewer channels, count up at the device's rate from
 * its kernel clock, divided by the prescaler, and wrap at their width: 32 bits on TIM2 and TIM5,
 * 16 on the others. The update interrupt
 * counts the wraps, which widen the count to 64 bits. Each compare channel is an alarm: its
 * count is programmed once it falls in the counter's current wrap, its epoch, and the
 * interrupt checks the whole count before it fires, so an alarm many wraps away fires only at
 * its own. The channels drive no pin.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_COUNTER

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define TIM_CR1   0x00u
#define TIM_DIER  0x0Cu
#define TIM_SR    0x10u
#define TIM_EGR   0x14u
#define TIM_CCMR1 0x18u
#define TIM_CCMR2 0x1Cu
#define TIM_CCER  0x20u
#define TIM_CNT   0x24u
#define TIM_PSC   0x28u
#define TIM_ARR   0x2Cu
#define TIM_CCR1  0x34u /* CCR2 to CCR4 follow, 4 bytes apart */

#define CR1_CEN  (1u << 0)
#define CR1_URS  (1u << 2) /* only an overflow raises the update interrupt, not UG */
#define UPDATE   (1u << 0) /* DIER.UIE, SR.UIF */
#define EGR_UG   (1u << 0)
#define PSC_MAX  65536u /* the prescaler divides by 1 to 65536 */
#define CHANNELS 4u     /* the most: TIM2 to TIM5's */

/* A channel's bit in DIER (CCxIE) and in SR (CCxIF), the same: bits 1 to 4. */
static uint32_t cc_bit(uint8_t channel)
{
    return 1u << (1u + (uint32_t)channel);
}

/* MISRA Dev CS-12: rules 11.3 and 11.5, a device's configuration and RAM by its driver's type. */
static const emb_stm32_tim_config_t *tim_config(const emb_device_t *dev)
{
    return (const emb_stm32_tim_config_t *)dev->config;
}

static emb_stm32_counter_data_t *counter_data(const emb_device_t *dev)
{
    return (emb_stm32_counter_data_t *)dev->data;
}

/* The counts in one wrap of the hardware's counter. */
static uint64_t span(const emb_stm32_tim_config_t *cfg)
{
    return (uint64_t)1u << ((cfg->bits >= 32u) ? 32u : cfg->bits);
}

/* The count, interrupts locked: an overflow not yet counted by the update interrupt is counted
 * here, and the counter read again after it. */
static uint64_t now_locked(const emb_stm32_tim_config_t *cfg, const emb_stm32_counter_data_t *d)
{
    uint64_t high = d->high;
    uint32_t count = REG32(cfg->base + TIM_CNT);
    if ((REG32(cfg->base + TIM_SR) & UPDATE) != 0u) {
        count = REG32(cfg->base + TIM_CNT);
        high += span(cfg);
    }
    return high + count;
}

/* A pending channel's compare, programmed once its count is in the counter's current epoch,
 * interrupts locked. */
static void program(const emb_stm32_tim_config_t *cfg, emb_stm32_counter_data_t *d, uint8_t channel)
{
    uint64_t at = d->target[channel];
    uint8_t bit = (uint8_t)(1u << channel);
    if (((d->pending & bit) == 0u) || (at >= (d->high + span(cfg)))) {
        return; /* not pending, or in a later epoch */
    }
    REG32(cfg->base + TIM_CCR1 + (4u * (uint32_t)channel)) =
        (at > d->high) ? (uint32_t)(at - d->high) : 0u;
    REG32(cfg->base + TIM_SR) = ~cc_bit(channel); /* rc_w0: the other flags stay */
    REG32(cfg->base + TIM_DIER) |= cc_bit(channel);
    d->pending &= (uint8_t)~bit;
}

static void disarm_locked(const emb_stm32_tim_config_t *cfg, emb_stm32_counter_data_t *d,
                          uint8_t channel)
{
    REG32(cfg->base + TIM_DIER) &= ~cc_bit(channel);
    REG32(cfg->base + TIM_SR) = ~cc_bit(channel);
    d->pending &= (uint8_t) ~(1u << channel);
}

/* Whether a programmed channel's count is reached: its compare has matched, or the count is
 * past it (the match came before it was programmed, in the update interrupt). */
static bool reached(const emb_stm32_tim_config_t *cfg, const emb_stm32_counter_data_t *d,
                    uint8_t channel)
{
    return ((d->pending & (1u << channel)) == 0u) &&
           ((REG32(cfg->base + TIM_DIER) & cc_bit(channel)) != 0u) &&
           (now_locked(cfg, d) >= d->target[channel]);
}

static void stm32_counter_isr(void *arg)
{
    emb_stm32_counter_data_t *d = (emb_stm32_counter_data_t *)arg;
    const emb_stm32_tim_config_t *cfg = tim_config(d->dev);
    uint32_t due = 0u;
    uint8_t ch;
    emb_irq_key_t key = emb_irq_lock(); /* an alarm armed from a higher interrupt waits */
    uint32_t flags = REG32(cfg->base + TIM_SR) & REG32(cfg->base + TIM_DIER);
    if ((flags & UPDATE) != 0u) {
        REG32(cfg->base + TIM_SR) = ~UPDATE;
        d->high += span(cfg);
        for (ch = 0u; ch < d->counter.channels; ch++) { /* bounded by CHANNELS */
            program(cfg, d, ch);                        /* those whose epoch begins */
        }
    }
    for (ch = 0u; ch < d->counter.channels; ch++) { /* bounded by CHANNELS */
        if ((flags & cc_bit(ch)) != 0u) {
            REG32(cfg->base + TIM_SR) = ~cc_bit(ch);
        }
        if (reached(cfg, d, ch)) {
            REG32(cfg->base + TIM_DIER) &= ~cc_bit(ch);
            due |= 1u << ch;
        }
    }
    emb_irq_unlock(key);
    for (ch = 0u; ch < CHANNELS; ch++) { /* bounded */
        if ((due & (1u << ch)) != 0u) {
            (void)emb_drv_counter_fired(d->dev, ch);
        }
    }
}

static emb_status_t stm32_counter_init(const emb_device_t *dev)
{
    const emb_stm32_tim_config_t *cfg = tim_config(dev);
    emb_stm32_counter_data_t *d = counter_data(dev);
    uintptr_t base = cfg->base;
    if ((cfg->rate_hz == 0u) || ((cfg->clock_hz % cfg->rate_hz) != 0u) ||
        ((cfg->clock_hz / cfg->rate_hz) > PSC_MAX)) {
        return EMB_EINVAL; /* a rate the prescaler cannot make exactly */
    }
    d->dev = dev;
    d->high = 0u;
    d->pending = 0u;
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit);
    REG32(base + TIM_CR1) = CR1_URS;
    REG32(base + TIM_DIER) = 0u;
    REG32(base + TIM_CCMR1) = 0u; /* the channels compare, frozen: no output */
    if (cfg->channels > 2u) {
        REG32(base + TIM_CCMR2) = 0u; /* channels 3 and 4: TIM2 to TIM5 alone */
    }
    REG32(base + TIM_CCER) = 0u;
    REG32(base + TIM_PSC) = (cfg->clock_hz / cfg->rate_hz) - 1u;
    REG32(base + TIM_ARR) = (uint32_t)(span(cfg) - 1u);
    REG32(base + TIM_CNT) = 0u;
    REG32(base + TIM_EGR) = EGR_UG; /* the prescaler loaded now */
    REG32(base + TIM_SR) = 0u;
    REG32(base + TIM_DIER) = UPDATE;
    if ((emb_irq_connect((emb_irq_t)cfg->irq, stm32_counter_isr, d) != EMB_OK) ||
        (emb_irq_enable((emb_irq_t)cfg->irq) != EMB_OK)) {
        return EMB_EINVAL;
    }
    REG32(base + TIM_CR1) = CR1_URS | CR1_CEN;
    return emb_drv_counter_started(dev, (uint8_t)cfg->channels);
}

static uint64_t stm32_counter_now(const emb_device_t *dev)
{
    emb_irq_key_t key = emb_irq_lock();
    uint64_t count = now_locked(tim_config(dev), counter_data(dev));
    emb_irq_unlock(key);
    return count;
}

static uint32_t stm32_counter_rate(const emb_device_t *dev)
{
    return tim_config(dev)->rate_hz;
}

/* Interrupts are locked by the class. Armed, then checked: a count that passed before its
 * compare was programmed is refused; one whose compare matched meanwhile fires. */
static emb_status_t stm32_counter_arm(const emb_device_t *dev, uint8_t channel, uint64_t at)
{
    const emb_stm32_tim_config_t *cfg = tim_config(dev);
    emb_stm32_counter_data_t *d = counter_data(dev);
    d->target[channel] = at;
    d->pending |= (uint8_t)(1u << channel);
    program(cfg, d, channel);
    if (now_locked(cfg, d) >= at) {
        if (((d->pending & (1u << channel)) == 0u) &&
            ((REG32(cfg->base + TIM_SR) & cc_bit(channel)) != 0u)) {
            return EMB_OK; /* matched as it was armed: the interrupt fires it */
        }
        disarm_locked(cfg, d, channel);
        return EMB_EPAST;
    }
    return EMB_OK;
}

static void stm32_counter_disarm(const emb_device_t *dev, uint8_t channel)
{
    disarm_locked(tim_config(dev), counter_data(dev), channel);
}

const emb_drv_counter_api_t emb_stm32_counter_api = {
    {stm32_counter_init, NULL, NULL, NULL, NULL, 0u},
    stm32_counter_now,
    stm32_counter_rate,
    stm32_counter_arm,
    stm32_counter_disarm,
    (uint8_t)CHANNELS,
    {0u, 0u, 0u},
};

#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_tim_counter_off_t;
#endif
