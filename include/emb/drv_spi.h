/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    drv_spi.h
 * @brief   The SPI controller class (SPEC-017 §5).
 *
 * @details
 * Transfers to a target in its mode, at its speed and word size, their blocking wrapper, and what a
 * driver shares to serve them.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_DRV_SPI_H
#define EMB_DRV_SPI_H

#include <emb/device.h>
#include <emb/drv_bus.h>
#include <emb/drv_gpio.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A target's mode: clock polarity (CPOL, bit 1) and phase (CPHA, bit 0). */
#define EMB_DRV_SPI_MODE_0 0u /* clock idles low, data sampled on the rising edge */
#define EMB_DRV_SPI_MODE_1 1u
#define EMB_DRV_SPI_MODE_2 2u
#define EMB_DRV_SPI_MODE_3 3u
#define EMB_DRV_SPI_CPHA   1u
#define EMB_DRV_SPI_CPOL   2u

/* A target's flags. */
#define EMB_DRV_SPI_LOOPBACK 0x01u /* the controller's internal loopback (CAP_LOOPBACK) */

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_SPI_CAP_DMA      0x01u
#define EMB_DRV_SPI_CAP_CRC      0x02u
#define EMB_DRV_SPI_CAP_LOOPBACK 0x04u

/* How to talk to one target: the controller is set up for it at each transfer, and its chip
 * select asserted around it, or across several while the bus is held (emb_drv_spi_acquire()).
 * A board's described chip selects are EMB_SOC_<DEVICE>_CS<n> (soc_devices.h), set up at boot;
 * any other line the application sets up as an output, deasserted, first. */
typedef struct emb_drv_spi_target {
    uint32_t hz;   /* the clock, at most: the driver takes the nearest rate not above it */
    uint8_t mode;  /* EMB_DRV_SPI_MODE_* */
    uint8_t bits;  /* frame size: 8 */
    uint8_t flags; /* EMB_DRV_SPI_LOOPBACK */
    uint8_t reserved_;
    emb_drv_gpio_line_t cs; /* its chip select, or none (gpio NULL) */
} emb_drv_spi_target_t;

/* An SPI request: the core's request first. The caller owns it, the target and the buffers
 * until it completes; req.done is the frames transferred. */
typedef struct emb_drv_spi_req {
    emb_drv_request_t req;
    const emb_drv_spi_target_t *target;
    const uint8_t *tx; /* the frames out, or NULL for ones (0xFF) */
    uint8_t *rx;       /* where the frames in go, or NULL to drop them */
    size_t len;        /* frames */
} emb_drv_spi_req_t;

/* The SPI request a core request belongs to, in a driver's start() and stop(). MISRA Dev
 * CS-12: rule 11.3, the request's first member. */
#define EMB_DRV_SPI_REQ(r) ((emb_drv_spi_req_t *)(void *)(r))

/**
 * emb_drv_spi_bus_init() - Set up the class's part of a controller's RAM: for drivers.
 * @dev: A device of the SPI class whose data begins with an emb_drv_bus_t (<emb/drv_bus.h>).
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1); O(active mutexes) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     mutex
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * The driver's initialization calls it, at the KERNEL level, before any transfer.
 *
 * Return: EMB_OK; EMB_EINVAL (not an SPI controller with RAM); the statuses of
 * emb_mutex_init().
 */
emb_status_t emb_drv_spi_bus_init(const emb_device_t *dev);

/**
 * emb_drv_spi_begin() - Assert a transfer's chip select: for drivers.
 * @dev: The SPI device.
 * @req: The transfer starting.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * A driver's start() calls it once the controller is set up for the target (its clock's
 * polarity in place), before the first frame. A bus held for the target has it asserted
 * already.
 */
void emb_drv_spi_begin(const emb_device_t *dev, const emb_drv_spi_req_t *req);

/**
 * emb_drv_spi_end() - Deassert a transfer's chip select: for drivers.
 * @dev: The SPI device.
 * @req: The transfer ending.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * Every end of a transfer emb_drv_spi_begin() began, in start() or later, calls it before the
 * transfer completes, after its last frame. A bus held for the target keeps it asserted.
 */
void emb_drv_spi_end(const emb_device_t *dev, const emb_drv_spi_req_t *req);

/**
 * emb_drv_spi_req_transfer() - Prepare a transfer.
 * @req:    The request.
 * @target: The target's mode, speed and word size; it must outlive the request.
 * @tx:     The frames to send, or NULL to send ones.
 * @rx:     Where the frames received go, or NULL to drop them.
 * @len:    How many frames.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * The request is reset with its completion target polled. A NULL @req is ignored.
 */
void emb_drv_spi_req_transfer(emb_drv_spi_req_t *req, const emb_drv_spi_target_t *target,
                              const void *tx, void *rx, size_t len);

/**
 * emb_drv_spi_submit() - Queue a transfer.
 * @dev: A device of the SPI class.
 * @req: A request prepared by emb_drv_spi_req_transfer().
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus the driver's start when the controller was idle
 * @owns     none
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * One transfer runs at a time; the others queue. A transfer needing a capability the
 * controller lacks, or a frame size it does not do, completes with EMB_ENOTSUP. While a thread
 * holds the bus for one target, a transfer for another is refused.
 *
 * Return: EMB_OK; the statuses of emb_drv_submit(); EMB_EBUSY (the bus is held for another
 * target); EMB_EINVAL (not an SPI controller, no target, a target speed of 0, or a mode above
 * 3).
 */
emb_status_t emb_drv_spi_submit(const emb_device_t *dev, emb_drv_spi_req_t *req);

/**
 * emb_drv_spi_transfer() - Transfer frames and wait until they are done.
 * @dev:     A device of the SPI class.
 * @target:  The target's mode, speed and word size.
 * @tx:      The frames to send, or NULL to send ones.
 * @rx:      Where the frames received go, or NULL to drop them.
 * @len:     How many frames.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 * @done:    Receives the frames transferred, also on error and timeout; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(len) of the bus's time, after a wait for the bus; bounded by the timeout and the
 * driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004 DRV-006
 * @since    0.3
 * @stable   no
 *
 * The blocking wrapper of SPEC-016 §5.2. It waits for a bus another thread holds, within
 * @timeout; the caller's own hold, for the same target, lets it through. On timeout the
 * transfer stops after the frames in flight.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ENOTSUP; EMB_EMODEFAULT (another controller drove
 * the bus); EMB_EBUSY (the caller holds the bus for another target); the statuses of
 * emb_drv_spi_submit().
 */
emb_status_t emb_drv_spi_transfer(const emb_device_t *dev, const emb_drv_spi_target_t *target,
                                  const void *tx, void *rx, size_t len, emb_timeout_t timeout,
                                  size_t *done);

/**
 * emb_drv_spi_acquire() - Hold the bus for one target, its chip select asserted.
 * @dev:     A device of the SPI class.
 * @target:  The target; its chip select stays asserted until the last release.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) plus the mutex's priority inheritance
 * @owns     mutex
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * For a target that needs several transfers under one chip select: a command and its data.
 * Other threads' transfers wait until it is released; the holder's run, each under the same
 * chip select. The holder may acquire again for the same target; each acquire needs its
 * release.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_EBUSY (the caller holds the bus for another target);
 * EMB_EINVAL; the statuses of emb_mutex_lock().
 */
emb_status_t emb_drv_spi_acquire(const emb_device_t *dev, const emb_drv_spi_target_t *target,
                                 emb_timeout_t timeout);

/**
 * emb_drv_spi_release() - Release the bus emb_drv_spi_acquire() held.
 * @dev: A device of the SPI class.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1) plus the mutex's priority inheritance
 * @owns     mutex
 * @config   CONFIG_EMB_DRV_SPI
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * The last release deasserts the target's chip select and lets the next thread have the bus.
 *
 * Return: EMB_OK; EMB_EPERM (the caller does not hold the bus); EMB_EINVAL.
 */
emb_status_t emb_drv_spi_release(const emb_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_SPI_H */
