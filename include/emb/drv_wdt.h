/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    drv_wdt.h
 * @brief   The watchdog class (SPEC-017 §8).
 *
 * @details
 * A hardware watchdog started once with its timeout, fed from any context, never stopped; and
 * whether it caused the last reset. FLT-004's software watchdog is its feeder in a system that uses
 * one.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_DRV_WDT_H
#define EMB_DRV_WDT_H

#include <emb/device.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_WDT_CAP_WINDOW 0x01u /* fed too early is a fault too */

/* A watchdog driver's operations: the device's, then start(), feed() and caused_reset(). The
 * driver reads the reset's cause at its initialization; start() refuses a timeout it cannot
 * reach with EMB_EINVAL. */
typedef struct emb_drv_wdt_api {
    emb_device_api_t dev;
    emb_status_t (*start)(const emb_device_t *dev, uint32_t timeout_ms);
    void (*feed)(const emb_device_t *dev);
    bool (*caused_reset)(const emb_device_t *dev);
} emb_drv_wdt_api_t;

/**
 * emb_drv_wdt_start() - Start the watchdog: from here it must be fed.
 * @dev:        A device of the watchdog class.
 * @timeout_ms: How long it waits for a feed before it resets the part.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_WDT
 * @req      FLT-004
 * @since    0.3
 * @stable   no
 *
 * The timeout is a floor: a driver counts it at its clock's fastest (the STM32's LSI at
 * 47 kHz, its datasheet's upper bound), so it never resets the part before; a slower clock lets
 * it run longer, up to some 2.8 times on the STM32F446. There is no stop: the STM32's IWDG cannot
 * stop once started, and the class does not stop the RP2350's either, so both behave alike.
 *
 * Return: EMB_OK; EMB_ESTATE (started already, or the device is not READY); EMB_EINVAL (a
 * timeout of 0, or longer than the watchdog counts).
 */
emb_status_t emb_drv_wdt_start(const emb_device_t *dev, uint32_t timeout_ms);

/**
 * emb_drv_wdt_feed() - Feed the watchdog: its timeout starts again.
 * @dev: A device of the watchdog class.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_WDT
 * @req      FLT-004
 * @since    0.3
 * @stable   no
 *
 * Feeding one not started does nothing.
 *
 * Return: EMB_OK; EMB_ESTATE (the device is not READY).
 */
emb_status_t emb_drv_wdt_feed(const emb_device_t *dev);

/**
 * emb_drv_wdt_caused_reset() - Whether this watchdog caused the last reset.
 * @dev: A device of the watchdog class.
 * @yes: Receives true when it did.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_WDT
 * @req      FLT-004
 * @since    0.3
 * @stable   no
 *
 * The driver reads the cause once, at its initialization; the answer stays this boot's.
 *
 * Return: EMB_OK; EMB_ESTATE (the device is not READY).
 */
emb_status_t emb_drv_wdt_caused_reset(const emb_device_t *dev, bool *yes);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_WDT_H */
