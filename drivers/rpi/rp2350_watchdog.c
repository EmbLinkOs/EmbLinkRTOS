/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_watchdog.c
 * @brief   The RP2350's watchdog (datasheet §12.9; registers and the enable sequence as pico-sdk
 *          2.1.1's hardware/regs/watchdog.h and hardware_watchdog name them) as a watchdog class
 *          device (SPEC-017 §8).
 *
 * @details
 * It counts down a microsecond tick from clk_ref, the TICKS block's, and resets everything but the
 * oscillators (PSM WDSEL). It caused a reset when REASON says its timer did and SCRATCH4 still
 * holds the mark start() left there, as pico-sdk's watchdog_enable_caused_reboot() checks: the boot
 * ROM's own reboots (picotool's, BOOTSEL's) go through the watchdog too, with a mark of their own.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_rp2350_devices.h>
#include <hw_config.h>

#if CONFIG_EMB_DRV_WDT
#define REG32(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#define SET_ALIAS    0x2000u /* write ones to set bits */
#define CLR_ALIAS    0x3000u /* write ones to clear bits */
#define WD_CTRL      0x00u
#define WD_LOAD      0x04u
#define WD_REASON    0x08u
#define WD_SCRATCH4  0x1Cu
#define CTRL_ENABLE  ((uint32_t)1u << 30)
#define CTRL_PAUSE   ((uint32_t)7u << 24) /* PAUSE_DBG1, PAUSE_DBG0, PAUSE_JTAG */
#define REASON_TIMER (1u << 0)
#define LOAD_MAX     (((uint32_t)1u << 24) - 1u) /* LOAD is 24 bits */
/* pico-sdk's WATCHDOG_NON_REBOOT_MAGIC, in decimal: not a reboot the boot ROM was asked for */
#define NON_REBOOT 1790390561u

#define TICKS_WD_CTRL   0x30u /* in the TICKS block */
#define TICKS_WD_CYCLES 0x34u
#define PSM_WDSEL       0x08u                       /* in the PSM */
#define WDSEL_ALL       (((uint32_t)1u << 25) - 1u) /* the PSM's 25 blocks */
#define WDSEL_OSC       ((uint32_t)3u << 2)         /* ROSC and XOSC: kept running */
#define US_PER_MS       1000u
#define HZ_PER_MHZ      1000000u

static const emb_rp2350_watchdog_config_t *rp2350_wd_config(const emb_device_t *dev)
{
    return (const emb_rp2350_watchdog_config_t *)dev->config;
}

static emb_rp2350_watchdog_data_t *rp2350_wd_data(const emb_device_t *dev)
{
    return (emb_rp2350_watchdog_data_t *)dev->data;
}

static emb_status_t rp2350_wd_init(const emb_device_t *dev)
{
    uintptr_t base = rp2350_wd_config(dev)->base;
    emb_rp2350_watchdog_data_t *d = rp2350_wd_data(dev);
    d->started = false;
    d->caused = ((REG32(base + WD_REASON) & REASON_TIMER) != 0u) &&
                (REG32(base + WD_SCRATCH4) == NON_REBOOT);
    REG32(base + WD_SCRATCH4) = 0u; /* read once: a later reboot of another kind is not ours */
    return EMB_OK;
}

static emb_status_t rp2350_wd_start(const emb_device_t *dev, uint32_t timeout_ms)
{
    uintptr_t base = rp2350_wd_config(dev)->base;
    uintptr_t ticks = EMB_HW_TICKS_BASE;
    uintptr_t psm = EMB_HW_PSM_BASE;
    emb_rp2350_watchdog_data_t *d = rp2350_wd_data(dev);
    if (d->started) {
        return EMB_ESTATE;
    }
    if (timeout_ms > (LOAD_MAX / US_PER_MS)) {
        return EMB_EINVAL; /* past some 16.7 s */
    }
    REG32(base + WD_CTRL + CLR_ALIAS) = CTRL_ENABLE;
    REG32(ticks + TICKS_WD_CYCLES) = EMB_HW_XOSC_HZ / HZ_PER_MHZ; /* clk_ref is the crystal */
    REG32(ticks + TICKS_WD_CTRL) = 1u;
    REG32(psm + PSM_WDSEL + SET_ALIAS) = WDSEL_ALL & ~WDSEL_OSC;
    REG32(base + WD_SCRATCH4) = NON_REBOOT;
    REG32(base + WD_CTRL + CLR_ALIAS) = CTRL_PAUSE; /* it counts under a debugger too */
    d->load = timeout_ms * US_PER_MS;
    REG32(base + WD_LOAD) = d->load;
    REG32(base + WD_CTRL + SET_ALIAS) = CTRL_ENABLE;
    d->started = true;
    return EMB_OK;
}

static void rp2350_wd_feed(const emb_device_t *dev)
{
    const emb_rp2350_watchdog_data_t *d = rp2350_wd_data(dev);
    if (d->started) {
        REG32(rp2350_wd_config(dev)->base + WD_LOAD) =
            d->load; /* the count it started with, again */
    }
}

static bool rp2350_wd_caused_reset(const emb_device_t *dev)
{
    return rp2350_wd_data(dev)->caused;
}

const emb_drv_wdt_api_t emb_rp2350_watchdog_api = {
    {rp2350_wd_init, NULL, NULL, NULL, NULL, 0u},
    rp2350_wd_start,
    rp2350_wd_feed,
    rp2350_wd_caused_reset,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_watchdog_off_t;
#endif
