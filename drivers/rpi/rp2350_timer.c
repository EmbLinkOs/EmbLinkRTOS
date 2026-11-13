/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_timer.c
 * @brief   The RP2350's TIMER0 and TIMER1 as counters (datasheet §12.8; SPEC-017 §11a, A129).
 *
 * @details
 * Each timer counts the ticks of its generator in the TICKS block, 1 MHz from the crystal
 * unless the board asks another rate, in 64 bits. Its four alarms compare the count's low 32
 * bits: an alarm more than 2^32 counts away matches early, and its interrupt arms it again
 * until the whole count is reached. Each alarm has its own interrupt line.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_rp2350_devices.h>
#include <emb_rp2350_drivers.h>
#include <hw_config.h>

#if CONFIG_EMB_DRV_COUNTER

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
/* every peripheral has atomic aliases: write ones to set (+0x2000) or clear (+0x3000) bits */
#define SET_ALIAS 0x2000u
#define CLR_ALIAS 0x3000u

#define TIMER_ALARM0   0x10u /* ALARM1 to ALARM3 follow, 4 bytes apart */
#define TIMER_ARMED    0x20u /* write 1 to disarm */
#define TIMER_TIMERAWH 0x24u
#define TIMER_TIMERAWL 0x28u
#define TIMER_PAUSE    0x30u
#define TIMER_SOURCE   0x38u /* 0: the tick */
#define TIMER_INTR     0x3Cu /* write 1 to clear */
#define TIMER_INTE     0x40u

/* the TICKS block's generators: TIMER0's at 0x18, TIMER1's 0x0C after */
#define TICKS_TIMER0_CTRL 0x18u
#define TICKS_STRIDE      0x0Cu
#define TICKS_CYCLES      0x04u /* after its CTRL */
#define TICKS_ENABLE      1u
#define TICKS_CYCLES_MAX  511u
#define CHANNELS          4u
#define ALL_ALARMS        0xFu

/* MISRA Dev CS-12: rules 11.3 and 11.5, a device's configuration and RAM by its driver's type. */
static const emb_rp2350_timer_config_t *timer_config(const emb_device_t *dev)
{
    return (const emb_rp2350_timer_config_t *)dev->config;
}

static emb_rp2350_timer_data_t *timer_data(const emb_device_t *dev)
{
    return (emb_rp2350_timer_data_t *)dev->data;
}

/* The count: its high word read again after the low one, and the low again if it moved. */
static uint64_t count_of(uintptr_t base)
{
    uint32_t high = REG32(base + TIMER_TIMERAWH);
    uint32_t low = REG32(base + TIMER_TIMERAWL);
    uint32_t again = REG32(base + TIMER_TIMERAWH);
    if (again != high) {
        low = REG32(base + TIMER_TIMERAWL);
        high = again;
    }
    return ((uint64_t)high << 32) | low;
}

static void rp2350_alarm_isr(void *arg)
{
    const emb_rp2350_alarm_t *alarm = (const emb_rp2350_alarm_t *)arg;
    const emb_rp2350_timer_config_t *cfg = timer_config(alarm->dev);
    emb_rp2350_timer_data_t *d = timer_data(alarm->dev);
    uint32_t bit = 1u << alarm->channel;
    bool due = false;
    emb_irq_key_t key = emb_irq_lock(); /* an alarm armed from a higher interrupt waits */
    REG32(cfg->base + TIMER_INTR) = bit;
    if ((REG32(cfg->base + TIMER_INTE) & bit) != 0u) {
        if (count_of(cfg->base) >= d->target[alarm->channel]) {
            REG32(cfg->base + TIMER_INTE + CLR_ALIAS) = bit;
            due = true;
        } else { /* its low 32 bits matched a wrap early: again, at the next */
            REG32(cfg->base + TIMER_ALARM0 + (4u * (uint32_t)alarm->channel)) =
                (uint32_t)d->target[alarm->channel];
        }
    }
    emb_irq_unlock(key);
    if (due) {
        (void)emb_drv_counter_fired(alarm->dev, alarm->channel);
    }
}

static emb_status_t rp2350_timer_init(const emb_device_t *dev)
{
    const emb_rp2350_timer_config_t *cfg = timer_config(dev);
    emb_rp2350_timer_data_t *d = timer_data(dev);
    const uint32_t irqs[CHANNELS] = {cfg->irq0, cfg->irq1, cfg->irq2, cfg->irq3};
    uintptr_t tick = EMB_HW_TICKS_BASE + TICKS_TIMER0_CTRL + (TICKS_STRIDE * cfg->index);
    uint32_t cycles;
    uint8_t ch;
    if ((cfg->rate_hz == 0u) || ((EMB_HW_XOSC_HZ % cfg->rate_hz) != 0u) ||
        ((EMB_HW_XOSC_HZ / cfg->rate_hz) > TICKS_CYCLES_MAX) || (cfg->index > 1u)) {
        return EMB_EINVAL; /* a rate the tick generator cannot make from clk_ref, the crystal */
    }
    cycles = EMB_HW_XOSC_HZ / cfg->rate_hz;
    if (emb_rp2350_unreset((cfg->index == 0u) ? EMB_RP2350_RESET_TIMER0
                                              : EMB_RP2350_RESET_TIMER1) != 0) {
        return EMB_EIO;
    }
    /* its tick, left as it is when it already runs at the rate: the count goes on */
    if (((REG32(tick) & TICKS_ENABLE) == 0u) || (REG32(tick + TICKS_CYCLES) != cycles)) {
        REG32(tick) = 0u;
        REG32(tick + TICKS_CYCLES) = cycles;
        REG32(tick) = TICKS_ENABLE;
    }
    REG32(cfg->base + TIMER_SOURCE) = 0u;
    REG32(cfg->base + TIMER_PAUSE) = 0u;
    REG32(cfg->base + TIMER_INTE) = 0u;
    REG32(cfg->base + TIMER_ARMED) = ALL_ALARMS;
    REG32(cfg->base + TIMER_INTR) = ALL_ALARMS;
    for (ch = 0u; ch < CHANNELS; ch++) { /* bounded */
        d->alarm[ch].dev = dev;
        d->alarm[ch].channel = ch;
        d->target[ch] = 0u;
        if ((emb_irq_connect((emb_irq_t)irqs[ch], rp2350_alarm_isr, &d->alarm[ch]) != EMB_OK) ||
            (emb_irq_enable((emb_irq_t)irqs[ch]) != EMB_OK)) {
            return EMB_EINVAL;
        }
    }
    return emb_drv_counter_started(dev, 0u);
}

static uint64_t rp2350_timer_now(const emb_device_t *dev)
{
    return count_of(timer_config(dev)->base);
}

static uint32_t rp2350_timer_rate(const emb_device_t *dev)
{
    return timer_config(dev)->rate_hz;
}

static void disarm_alarm(uintptr_t base, uint8_t channel)
{
    uint32_t bit = 1u << channel;
    REG32(base + TIMER_INTE + CLR_ALIAS) = bit;
    REG32(base + TIMER_ARMED) = bit;
    REG32(base + TIMER_INTR) = bit;
}

/* Interrupts are locked by the class. Armed, then checked: a count that passed before the alarm
 * was armed is refused, as it would match only at the next wrap of the low 32 bits; one that
 * matched meanwhile fires. */
static emb_status_t rp2350_timer_arm(const emb_device_t *dev, uint8_t channel, uint64_t at)
{
    uintptr_t base = timer_config(dev)->base;
    uint32_t bit = 1u << channel;
    timer_data(dev)->target[channel] = at;
    REG32(base + TIMER_INTR) = bit;
    REG32(base + TIMER_INTE + SET_ALIAS) = bit;
    REG32(base + TIMER_ALARM0 + (4u * (uint32_t)channel)) = (uint32_t)at; /* arms it */
    if (count_of(base) >= at) {
        if (((REG32(base + TIMER_ARMED) & bit) == 0u) && ((REG32(base + TIMER_INTR) & bit) != 0u)) {
            return EMB_OK; /* matched as it was armed: the interrupt fires it */
        }
        disarm_alarm(base, channel);
        return EMB_EPAST;
    }
    return EMB_OK;
}

static void rp2350_timer_disarm(const emb_device_t *dev, uint8_t channel)
{
    disarm_alarm(timer_config(dev)->base, channel);
}

const emb_drv_counter_api_t emb_rp2350_timer_api = {
    {rp2350_timer_init, NULL, NULL, NULL, NULL, EMB_DRV_COUNTER_CAP_64BIT},
    rp2350_timer_now,
    rp2350_timer_rate,
    rp2350_timer_arm,
    rp2350_timer_disarm,
    (uint8_t)CHANNELS,
    {0u, 0u, 0u},
};

#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_timer_off_t;
#endif
