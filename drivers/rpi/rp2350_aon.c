/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The RP2350's always-on timer (datasheet §6.5; registers and sequences as pico-sdk 2.1.1's
 * hardware_powman) as an RTC class device (SPEC-017 §10, A123): a 64-bit millisecond counter in
 * POWMAN, ticked at 1 kHz from the crystal, holding milliseconds since the Unix epoch once set.
 * It has no calendar and no backup registers. It runs on across resets and stops with power, so
 * while it is not running the time is not set. Every write to POWMAN carries its password in the
 * upper half. */
#include <emb/irq.h>

#include <emb_rp2350_devices.h>
#include <hw_config.h>

#if CONFIG_EMB_DRV_RTC
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define SET_ALIAS 0x2000u
#define CLR_ALIAS 0x3000u
#define PASSWORD  ((uint32_t)0x5AFEu << 16) /* pico-sdk's POWMAN_PASSWORD_BITS */

#define XOSC_FREQ_KHZ_INT  0x58u
#define XOSC_FREQ_KHZ_FRAC 0x5Cu
#define SET_TIME_63TO48    0x60u /* then 47TO32, 31TO16, 15TO0, a word apart */
#define READ_TIME_UPPER    0x70u
#define READ_TIME_LOWER    0x74u
#define ALARM_TIME_63TO48  0x78u /* then 47TO32, 31TO16, 15TO0 */
#define TIMER              0x88u
#define INTE               0xE4u

#define TIMER_RUN        ((uint32_t)1u << 1)
#define TIMER_ALARM_ENAB ((uint32_t)1u << 4)
#define TIMER_ALARM      ((uint32_t)1u << 6)
#define TIMER_USE_XOSC   ((uint32_t)1u << 9)
#define INTE_TIMER       ((uint32_t)1u << 1)
#define NS_PER_MS        1000000

static const emb_rp2350_aon_config_t *rp2350_aon_config(const emb_device_t *dev)
{
    return (const emb_rp2350_aon_config_t *)dev->config;
}

static emb_rp2350_aon_data_t *rp2350_aon_data(const emb_device_t *dev)
{
    return (emb_rp2350_aon_data_t *)dev->data;
}

static void rp2350_aon_write(uintptr_t reg, uint32_t value)
{
    REG32(reg) = PASSWORD | (value & 0xFFFFu);
}

static void rp2350_aon_set(uintptr_t reg, uint32_t bits)
{
    REG32(reg + SET_ALIAS) = PASSWORD | bits;
}

static void rp2350_aon_clear(uintptr_t reg, uint32_t bits)
{
    REG32(reg + CLR_ALIAS) = PASSWORD | bits;
}

/* Four 16-bit writes, the most significant first. */
static void rp2350_aon_write64(uintptr_t first, uint64_t v)
{
    rp2350_aon_write(first, (uint32_t)(v >> 48));
    rp2350_aon_write(first + 4u, (uint32_t)(v >> 32));
    rp2350_aon_write(first + 8u, (uint32_t)(v >> 16));
    rp2350_aon_write(first + 12u, (uint32_t)v);
}

static bool rp2350_aon_running(uintptr_t base)
{
    return (REG32(base + TIMER) & TIMER_RUN) != 0u;
}

static void rp2350_aon_disarm(uintptr_t base)
{
    rp2350_aon_clear(base + INTE, INTE_TIMER);
    rp2350_aon_clear(base + TIMER, TIMER_ALARM_ENAB);
    rp2350_aon_clear(base + TIMER, TIMER_ALARM);
}

static void rp2350_aon_isr(void *arg)
{
    emb_rp2350_aon_data_t *d = (emb_rp2350_aon_data_t *)arg;
    uintptr_t base = rp2350_aon_config(d->dev)->base;
    if ((REG32(base + TIMER) & TIMER_ALARM) == 0u) {
        return;
    }
    rp2350_aon_disarm(base);
    (void)emb_drv_rtc_alarm_fired(d->dev);
}

/* The 1 kHz tick from the crystal (pico-sdk's powman_timer_set_1khz_tick_source_xosc()). */
static void rp2350_aon_xosc(uintptr_t base)
{
    rp2350_aon_write(base + XOSC_FREQ_KHZ_INT, EMB_HW_XOSC_HZ / 1000u);
    rp2350_aon_write(base + XOSC_FREQ_KHZ_FRAC,
                     (((EMB_HW_XOSC_HZ / 1000u) % 1000u) * 65536u) / 1000u);
    rp2350_aon_set(base + TIMER, TIMER_USE_XOSC);
}

static emb_status_t rp2350_aon_init(const emb_device_t *dev)
{
    const emb_rp2350_aon_config_t *cfg = rp2350_aon_config(dev);
    emb_rp2350_aon_data_t *d = rp2350_aon_data(dev);
    d->dev = dev;
    rp2350_aon_disarm(cfg->base); /* a previous boot's alarm */
    if (!rp2350_aon_running(cfg->base)) {
        rp2350_aon_xosc(cfg->base); /* stopped: the source may change */
    }
    if ((emb_irq_connect((emb_irq_t)cfg->irq, rp2350_aon_isr, d) != EMB_OK) ||
        (emb_irq_enable((emb_irq_t)cfg->irq) != EMB_OK)) {
        return EMB_EINVAL;
    }
    return emb_drv_rtc_started(dev);
}

static emb_status_t rp2350_aon_get(const emb_device_t *dev, int64_t *ns)
{
    uintptr_t base = rp2350_aon_config(dev)->base;
    uint32_t hi;
    uint32_t lo;
    uint32_t n;
    if (!rp2350_aon_running(base)) {
        return EMB_ENOTSET;
    }
    hi = REG32(base + READ_TIME_UPPER);
    lo = REG32(base + READ_TIME_LOWER);
    for (n = 0u; n < 2u; n++) { /* bounded: the upper half moves once at most meanwhile */
        uint32_t again = REG32(base + READ_TIME_UPPER);
        if (again == hi) {
            break;
        }
        hi = again;
        lo = REG32(base + READ_TIME_LOWER);
    }
    *ns = (int64_t)((((uint64_t)hi << 32) | lo) * (uint64_t)NS_PER_MS);
    return EMB_OK;
}

static emb_status_t rp2350_aon_set_time(const emb_device_t *dev, int64_t ns)
{
    uintptr_t base = rp2350_aon_config(dev)->base;
    rp2350_aon_clear(base + TIMER, TIMER_RUN);
    rp2350_aon_xosc(base);
    rp2350_aon_write64(base + SET_TIME_63TO48, (uint64_t)(ns / NS_PER_MS));
    rp2350_aon_set(base + TIMER, TIMER_RUN);
    return EMB_OK;
}

static emb_status_t rp2350_aon_alarm(const emb_device_t *dev, int64_t at_ns)
{
    uintptr_t base = rp2350_aon_config(dev)->base;
    uint64_t ms = (uint64_t)((at_ns + NS_PER_MS - 1) / NS_PER_MS); /* up: never early */
    rp2350_aon_clear(base + TIMER, TIMER_ALARM_ENAB); /* disabled while the time is written */
    rp2350_aon_write64(base + ALARM_TIME_63TO48, ms);
    rp2350_aon_clear(base + TIMER, TIMER_ALARM);
    rp2350_aon_set(base + INTE, INTE_TIMER);
    rp2350_aon_set(base + TIMER, TIMER_ALARM_ENAB);
    return EMB_OK;
}

static void rp2350_aon_alarm_cancel(const emb_device_t *dev)
{
    rp2350_aon_disarm(rp2350_aon_config(dev)->base);
}

const emb_drv_rtc_api_t emb_rp2350_aon_api = {
    {rp2350_aon_init, NULL, NULL, NULL, NULL, EMB_DRV_RTC_CAP_SUBSECONDS},
    rp2350_aon_get,
    rp2350_aon_set_time,
    rp2350_aon_alarm,
    rp2350_aon_alarm_cancel,
    NULL,
    NULL,
    0u,
    {0u, 0u, 0u},
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_aon_off_t;
#endif
