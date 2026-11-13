/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_pwm.c
 * @brief   The RP2350's PWM slices (datasheet §12.5; SPEC-017 §11b, A129).
 *
 * @details
 * Outputs: a slice's A and B, channels 0 and 1, count clk_sys through its fractional divider
 * up to TOP, and are high while the count is below their compare. TOP and the compares are
 * double-buffered: a change takes effect at the next wrap, without a glitch. The divider is the
 * smallest that brings the period within TOP's 16 bits, in sixteenths.
 *
 * Capture: the slice's B input, averaged over a gate time, the slice having no capture
 * register. First the period: the counter counts B's rising edges, through a divider that keeps
 * the gate's count within 16 bits, and the gate starts and ends at a count's change, timed by
 * the CPU's cycle counter, so it holds a whole number of edges. Then the pulse: the counter
 * counts clk_sys while B is high, over a gate of exactly a whole number of the periods just
 * measured, wherever it starts, so the high time it holds is that many pulses.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/arch.h>
#include <emb/irq.h>
#include <emb/thread.h>

#include <emb_rp2350_devices.h>
#include <emb_rp2350_drivers.h>

#if CONFIG_EMB_DRV_PWM

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define PWM_CSR 0x00u
#define PWM_DIV 0x04u /* INT bits 11:4, FRAC bits 3:0: sixteenths */
#define PWM_CTR 0x08u
#define PWM_CC  0x0Cu /* A bits 15:0, B bits 31:16 */
#define PWM_TOP 0x10u

#define CSR_EN      (1u << 0)
#define CSR_A_INV   (1u << 2)
#define CSR_B_INV   (1u << 3)
#define CSR_LEVEL   (1u << 4) /* DIVMODE 01: count while B is high */
#define CSR_RISE    (2u << 4) /* DIVMODE 10: count B's rising edges */
#define DIV16_MIN   16u       /* 1.0 */
#define DIV16_MAX   4095u     /* 255 and 15/16 */
#define COUNT_MAX   65536u    /* TOP's 16 bits */
#define GATE_MIN_MS 4u
#define GATE_MAX_MS 50u
#define PROBE_US    500u /* the first, short count of edges, which sizes the divider */
#define POLL_SLACK  200u /* cycles: a poll longer than this was preempted, and not trusted */
#define NS          1000000000u

/* MISRA Dev CS-12: rules 11.3 and 11.5, a device's configuration and RAM by its driver's type. */
static const emb_rp2350_pwm_config_t *pwm_config(const emb_device_t *dev)
{
    return (const emb_rp2350_pwm_config_t *)dev->config;
}

static emb_rp2350_pwm_data_t *pwm_data(const emb_device_t *dev)
{
    return (emb_rp2350_pwm_data_t *)dev->data;
}

static uint64_t clocks_of(uint32_t ns, uint32_t hz)
{
    return (((uint64_t)ns * hz) + (NS / 2u)) / NS;
}

static uint32_t ns_of(uint64_t clocks, uint64_t hz)
{
    uint64_t ns = ((clocks * NS) + (hz / 2u)) / hz;
    return (ns > UINT32_MAX) ? UINT32_MAX : (uint32_t)ns;
}

static emb_status_t rp2350_pwm_init(const emb_device_t *dev)
{
    const emb_rp2350_pwm_config_t *cfg = pwm_config(dev);
    emb_rp2350_pwm_data_t *d = pwm_data(dev);
    if (emb_rp2350_unreset(EMB_RP2350_RESET_PWM) != 0) {
        return EMB_EIO;
    }
    d->div16 = DIV16_MIN;
    REG32(cfg->base + PWM_CSR) = 0u;
    REG32(cfg->base + PWM_CC) = 0u;
    return emb_drv_pwm_started(dev, 2u);
}

/* Interrupts are locked by the class. Alone, the slice takes the new period's divider and TOP;
 * with the other output running, it keeps theirs and changes the compare alone. */
static emb_status_t rp2350_pwm_set(const emb_device_t *dev, uint8_t channel, uint32_t period_ns,
                                   uint32_t pulse_ns, uint32_t flags, emb_drv_pwm_wave_t *got)
{
    const emb_rp2350_pwm_config_t *cfg = pwm_config(dev);
    emb_rp2350_pwm_data_t *d = pwm_data(dev);
    uintptr_t base = cfg->base;
    bool alone = (d->pwm.running & (uint8_t) ~(1u << channel)) == 0u;
    uint32_t div16 = d->div16;
    uint64_t top;
    uint64_t compare;
    uint32_t inv = (channel == 0u) ? CSR_A_INV : CSR_B_INV;
    uint32_t shift = 16u * (uint32_t)channel;
    if (alone) {
        uint64_t sixteenths = clocks_of(period_ns, cfg->clock_hz) * 16u;
        div16 = (uint32_t)((sixteenths + COUNT_MAX - 1u) / COUNT_MAX);
        if (div16 < DIV16_MIN) {
            div16 = DIV16_MIN;
        }
        if ((div16 > DIV16_MAX) || (((sixteenths + (div16 / 2u)) / div16) < 2u)) {
            return EMB_EINVAL; /* longer than the divider reaches, or shorter than two counts */
        }
        top = (sixteenths + (div16 / 2u)) / div16;
    } else {
        top = (uint64_t)REG32(base + PWM_TOP) + 1u;
    }
    compare = (pulse_ns >= period_ns)
                  ? top
                  : (((clocks_of(pulse_ns, cfg->clock_hz) * 16u) + (div16 / 2u)) / div16);
    if (compare > top) {
        compare = top;
    }
    REG32(base + PWM_CC) =
        (REG32(base + PWM_CC) & ~(0xFFFFu << shift)) |
        ((uint32_t)((compare >= COUNT_MAX) ? (COUNT_MAX - 1u) : compare) << shift);
    if (compare >= COUNT_MAX) {
        /* high throughout at the longest TOP: the compare cannot pass it, so invert a low */
        REG32(base + PWM_CC) &= ~(0xFFFFu << shift);
        flags ^= EMB_DRV_PWM_INVERTED;
    }
    if (alone) {
        d->div16 = div16;
        REG32(base + PWM_CSR) = 0u; /* a capture's mode, gone */
        REG32(base + PWM_DIV) = div16;
        REG32(base + PWM_TOP) = (uint32_t)(top - 1u);
        REG32(base + PWM_CTR) = 0u;
    }
    REG32(base + PWM_CSR) = (REG32(base + PWM_CSR) & ~inv) |
                            (((flags & EMB_DRV_PWM_INVERTED) != 0u) ? inv : 0u) | CSR_EN;
    if (got != NULL) {
        got->period_ns = ns_of((top * div16) / 16u, cfg->clock_hz);
        got->pulse_ns = ns_of((compare * div16) / 16u, cfg->clock_hz);
    }
    return EMB_OK;
}

static void rp2350_pwm_stop(const emb_device_t *dev, uint8_t channel)
{
    uintptr_t base = pwm_config(dev)->base;
    REG32(base + PWM_CC) &= ~(0xFFFFu << (16u * (uint32_t)channel)); /* low: or high, inverted */
}

/* The count's next change after @from, polled, before @limit cycles from @start: its value in
 * @value and the cycle count it was seen at, within a poll, in @at. A poll a preemption
 * stretched is not trusted: the change after the next is waited for instead. */
static bool next_change(uintptr_t base, uint32_t from, emb_cycle_t start, uint32_t limit,
                        uint32_t *value, emb_cycle_t *at)
{
    uint32_t last = from;
    emb_cycle_t before = emb_arch_cycles();
    while ((emb_cycle_t)(before - start) < limit) { /* bounded: limit cycles */
        uint32_t v = REG32(base + PWM_CTR);
        emb_cycle_t after = emb_arch_cycles();
        if (v != last) {
            if ((emb_cycle_t)(after - before) <= POLL_SLACK) {
                *value = v;
                *at = after;
                return true;
            }
            last = v; /* preempted around it: wait for the next */
        }
        before = after;
    }
    return false;
}

/* Spin for @cycles of the CPU's counter. */
static void spin(uint32_t cycles)
{
    emb_cycle_t start = emb_arch_cycles();
    while ((emb_cycle_t)(emb_arch_cycles() - start) < cycles) { /* bounded: cycles */
    }
}

/* A divider, in sixteenths, of whole steps, that keeps @count's worth within 16 bits. */
static uint32_t divider_for(uint64_t count)
{
    uint32_t div16 = (uint32_t)((count + (COUNT_MAX - 2u)) / (COUNT_MAX - 1u)) * 16u;
    if (div16 < DIV16_MIN) {
        return DIV16_MIN;
    }
    return (div16 > (DIV16_MAX & ~15u)) ? (DIV16_MAX & ~15u) : div16;
}

/* Run the slice in @mode with @div16 from zero. */
static void count_from_zero(uintptr_t base, uint32_t mode, uint32_t div16)
{
    REG32(base + PWM_CSR) = 0u;
    REG32(base + PWM_DIV) = div16;
    REG32(base + PWM_TOP) = COUNT_MAX - 1u;
    REG32(base + PWM_CTR) = 0u;
    REG32(base + PWM_CSR) = mode | CSR_EN;
}

/* Under the device's lock. B only: A is an output alone. */
static emb_status_t rp2350_pwm_capture(const emb_device_t *dev, uint8_t channel,
                                       emb_drv_pwm_wave_t *wave, emb_timeout_t timeout)
{
    const emb_rp2350_pwm_config_t *cfg = pwm_config(dev);
    emb_rp2350_pwm_data_t *d = pwm_data(dev);
    uintptr_t base = cfg->base;
    uint32_t cps = emb_arch_cycles_hz();
    uint32_t wait_ms = EMB_TIMEOUT_IS_FOREVER(timeout) ? UINT32_MAX : 0u;
    uint32_t gate_ms;
    uint32_t limit;
    uint32_t v0 = 0u;
    uint32_t v1 = 0u;
    uint32_t div16;
    uint64_t edges;
    uint64_t span;
    uint64_t gate;
    uint64_t periods;
    uint64_t high;
    emb_cycle_t t0 = 0u;
    emb_cycle_t t1 = 0u;
    emb_irq_key_t key;
    if (channel != 1u) {
        return EMB_ENOTSUP;
    }
    if ((d->pwm.running != 0u) || (cps == 0u)) {
        return EMB_EBUSY; /* the slice outputs; or no cycle counter to time a gate with */
    }
    if (wait_ms == 0u) {
        wait_ms = (uint32_t)((timeout.ticks * (uint64_t)CONFIG_EMB_TICK_NS) / 1000000u);
    }
    gate_ms = wait_ms / 4u;
    gate_ms = (gate_ms > GATE_MAX_MS) ? GATE_MAX_MS : gate_ms;
    if (gate_ms < GATE_MIN_MS) {
        return EMB_ENOSIGNAL; /* no time for a gate */
    }
    limit = (uint32_t)(((uint64_t)cps * gate_ms) / 1000u); /* an edge's longest wait */
    /* the edges in a short probe size the divider, so that the gate's count fits in 16 bits */
    count_from_zero(base, CSR_RISE, DIV16_MIN);
    spin((uint32_t)(((uint64_t)cps * PROBE_US) / 1000000u));
    edges = REG32(base + PWM_CTR);
    div16 = divider_for(edges * ((gate_ms * 1000u) / PROBE_US) * 2u); /* twice: a margin */
    /* the period: whole counts of edges between two of the counter's changes */
    count_from_zero(base, CSR_RISE, div16);
    if (!next_change(base, 0u, emb_arch_cycles(), limit, &v0, &t0)) {
        REG32(base + PWM_CSR) = 0u;
        return EMB_ENOSIGNAL;
    }
    (void)emb_thread_sleep(EMB_MS(gate_ms));
    if (!next_change(base, REG32(base + PWM_CTR), emb_arch_cycles(), limit, &v1, &t1)) {
        REG32(base + PWM_CSR) = 0u;
        return EMB_ENOSIGNAL;
    }
    edges = (uint64_t)((v1 - v0) & (COUNT_MAX - 1u)) * (div16 / 16u);
    span = (emb_cycle_t)(t1 - t0); /* CPU cycles */
    if (edges == 0u) {
        REG32(base + PWM_CSR) = 0u;
        return EMB_ENOSIGNAL;
    }
    /* the pulse: clk_sys counted while B is high, over exactly a whole number of periods */
    periods = edges;
    gate = span; /* those same periods, in CPU cycles */
    div16 = divider_for(((uint64_t)cfg->clock_hz * gate) / cps); /* high throughout, at most */
    REG32(base + PWM_CSR) = 0u;
    REG32(base + PWM_DIV) = div16;
    REG32(base + PWM_TOP) = COUNT_MAX - 1u;
    REG32(base + PWM_CTR) = 0u;
    key = emb_irq_lock();
    REG32(base + PWM_CSR) = CSR_LEVEL | CSR_EN;
    t0 = emb_arch_cycles();
    emb_irq_unlock(key);
    if (((gate * NS) / cps) > (3u * (uint64_t)CONFIG_EMB_TICK_NS)) { /* sleep all but the end */
        (void)emb_thread_sleep(EMB_TICKS(((gate * NS) / cps) / CONFIG_EMB_TICK_NS - 2u));
    }
    key = emb_irq_lock();
    while ((emb_cycle_t)(emb_arch_cycles() - t0) < gate) { /* bounded: the gate's end */
    }
    REG32(base + PWM_CSR) = CSR_LEVEL; /* stopped at the gate's end */
    emb_irq_unlock(key);
    high = (uint64_t)REG32(base + PWM_CTR) * div16 / 16u; /* clk_sys cycles while high */
    REG32(base + PWM_CSR) = 0u;
    /* period: the gate's CPU cycles over its edges; pulse: the high clocks over those periods */
    wave->period_ns = ns_of(span, cps * periods);
    wave->pulse_ns = ns_of(high, cfg->clock_hz * periods);
    return EMB_OK;
}

const emb_drv_pwm_api_t emb_rp2350_pwm_api = {
    {rp2350_pwm_init, NULL, NULL, NULL, NULL,
     EMB_DRV_PWM_CAP_CAPTURE | EMB_DRV_PWM_CAP_CAPTURE_AVERAGED},
    rp2350_pwm_set,
    rp2350_pwm_stop,
    rp2350_pwm_capture,
    2u,
    {0u, 0u, 0u},
};

#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_pwm_off_t;
#endif
