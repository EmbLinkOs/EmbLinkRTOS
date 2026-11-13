/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    drv_uart.h
 * @brief   The UART class (SPEC-017 §4).
 *
 * @details
 * Requests to write and read bytes, their blocking wrappers, the polled calls the console and the
 * fault report use, and the receive core every UART driver shares: a ring that keeps bytes no read
 * is waiting for, and the read in progress.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_DRV_UART_H
#define EMB_DRV_UART_H

#include <emb/device.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A request's operation, and the lane it runs on (A114): writes on 0, reads on 1. */
#define EMB_DRV_UART_OP_WRITE 0u
#define EMB_DRV_UART_OP_READ  1u
#define EMB_DRV_UART_OP_BREAK 2u /* a break on the line, on the write lane */
#define EMB_DRV_UART_LANE_TX  0u
#define EMB_DRV_UART_LANE_RX  1u

/* A read's flags. */
#define EMB_DRV_UART_IDLE 0x01u /* end early, with what arrived, when the line goes idle */

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_UART_CAP_DMA          0x01u
#define EMB_DRV_UART_CAP_IDLE_DETECT  0x02u
#define EMB_DRV_UART_CAP_FLOW_CONTROL 0x04u
#define EMB_DRV_UART_CAP_BREAK        0x08u

/* A UART request: the core's request first, so a driver's start() can reach the rest with
 * EMB_DRV_UART_REQ(). The caller owns it, and the buffer, until it completes; its
 * req.done is the bytes written or read so far. */
typedef struct emb_drv_uart_req {
    emb_drv_request_t req;
    const uint8_t *tx; /* a write's bytes */
    uint8_t *rx;       /* where a read's go */
    size_t len;
    uint8_t op;    /* EMB_DRV_UART_OP_* */
    uint8_t flags; /* EMB_DRV_UART_IDLE */
} emb_drv_uart_req_t;

/* The UART request a core request belongs to, in a driver's start() and stop(). MISRA Dev
 * CS-12: rule 11.3, the request's first member. */
#define EMB_DRV_UART_REQ(r) ((emb_drv_uart_req_t *)(void *)(r))

/* The class's operations: the device's first, then the polled calls. */
typedef struct emb_drv_uart_api {
    emb_device_api_t dev;
    /* Sends the bytes and returns once the last is in the transmitter, with interrupts in
     * any state and before the kernel runs. */
    emb_status_t (*poll_write)(const struct emb_device *dev, const uint8_t *buf, size_t len);
    /* Takes what the receiver holds now, up to len bytes, without waiting. */
    size_t (*poll_read)(const struct emb_device *dev, uint8_t *buf, size_t len);
} emb_drv_uart_api_t;

/* A driver's receive state, in its RAM: the ring, the read in progress and the bytes lost.
 * Lock domain: the device's critical section. */
typedef struct emb_drv_uart_rx {
    uint8_t *ring;
    uint16_t size;   /* the ring's bytes */
    uint16_t head;   /* the next byte to take */
    uint16_t count;  /* bytes in the ring */
    uint8_t overrun; /* bytes were lost since the last read began */
    uint8_t reserved_;
    emb_drv_uart_req_t *active; /* the read in progress, or NULL */
    uint32_t lost;              /* bytes lost because the ring was full, since init */
} emb_drv_uart_rx_t;

/**
 * emb_drv_uart_req_write() - Prepare a write request.
 * @req: The request.
 * @buf: The bytes; they must stay put until the request completes.
 * @len: How many.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * The request is reset with its completion target polled; set another with
 * emb_drv_on_notify() and the like before submitting it. A NULL @req is ignored.
 */
void emb_drv_uart_req_write(emb_drv_uart_req_t *req, const void *buf, size_t len);

/**
 * emb_drv_uart_req_read() - Prepare a read request.
 * @req:   The request.
 * @buf:   Where the bytes go; it must stay put until the request completes.
 * @len:   How many to read at most.
 * @flags: EMB_DRV_UART_IDLE to end early when the line goes idle, or 0.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * The request is reset with its completion target polled. A NULL @req is ignored.
 */
void emb_drv_uart_req_read(emb_drv_uart_req_t *req, void *buf, size_t len, uint8_t flags);

/**
 * emb_drv_uart_req_break() - Prepare a break request: the line held low.
 * @req: The request.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * The break runs on the write lane, after the writes before it, and lasts at least one
 * character time. A driver without EMB_DRV_UART_CAP_BREAK completes it with EMB_ENOTSUP. A
 * NULL @req is ignored.
 */
void emb_drv_uart_req_break(emb_drv_uart_req_t *req);

/**
 * emb_drv_uart_submit() - Queue a UART request.
 * @dev: A device of the UART class.
 * @req: A request prepared by emb_drv_uart_req_write() or emb_drv_uart_req_read().
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus the driver's start when its lane was idle
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * A write and a read run at once; further requests queue behind their own kind. A read
 * takes the bytes the ring holds first, then the ones that arrive.
 *
 * Return: EMB_OK; the statuses of emb_drv_submit(); EMB_EINVAL (not a UART, or not a
 * UART request).
 */
emb_status_t emb_drv_uart_submit(const emb_device_t *dev, emb_drv_uart_req_t *req);

/**
 * emb_drv_uart_write() - Write bytes and wait until they are sent.
 * @dev:     A device of the UART class.
 * @buf:     The bytes.
 * @len:     How many.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 * @done:    Receives the bytes written, also on error and timeout; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(len) of the line's time; bounded by the timeout and the driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-004 DRV-006
 * @since    0.3
 * @stable   no
 *
 * The blocking wrapper of SPEC-016 §5.2. On timeout the write stops after the byte in
 * progress.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; the statuses of emb_drv_uart_submit().
 */
emb_status_t emb_drv_uart_write(const emb_device_t *dev, const void *buf, size_t len,
                                emb_timeout_t timeout, size_t *done);

/**
 * emb_drv_uart_read() - Read bytes, waiting for them.
 * @dev:     A device of the UART class.
 * @buf:     Where the bytes go.
 * @len:     How many to read at most.
 * @flags:   EMB_DRV_UART_IDLE to return early when the line goes idle, or 0.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 * @got:     Receives the bytes read, also on error and timeout; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(len) of the line's time; bounded by the timeout and the driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-004 DRV-006
 * @since    0.3
 * @stable   no
 *
 * On timeout the read returns what it received.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_EFRAMING, EMB_EPARITY (a byte arrived damaged; the
 * bytes before it are in @buf); EMB_EOVERRUN (bytes were lost before this read; what
 * arrived after is in @buf); the statuses of emb_drv_uart_submit().
 */
emb_status_t emb_drv_uart_read(const emb_device_t *dev, void *buf, size_t len, uint8_t flags,
                               emb_timeout_t timeout, size_t *got);

/**
 * emb_drv_uart_poll_write() - Send bytes by polling the transmitter.
 * @dev: A device of the UART class.
 * @buf: The bytes.
 * @len: How many.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(len) of the line's time
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-007
 * @since    0.3
 * @stable   no
 *
 * For the console before the kernel and for the fault report (SPEC-016 §7); it uses no
 * kernel service. Once the kernel runs, a device's polled and request calls are not mixed.
 *
 * Return: EMB_OK; EMB_EINVAL (not a UART); EMB_ENOTSUP (the driver has no polled write).
 */
emb_status_t emb_drv_uart_poll_write(const emb_device_t *dev, const void *buf, size_t len);

/**
 * emb_drv_uart_poll_read() - Take what the receiver holds, without waiting.
 * @dev: A device of the UART class.
 * @buf: Where the bytes go.
 * @len: How many to take at most.
 * @got: Receives how many were taken.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(len)
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-007
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL (not a UART, or @got missing); EMB_ENOTSUP (the driver has
 * no polled read).
 */
emb_status_t emb_drv_uart_poll_read(const emb_device_t *dev, void *buf, size_t len, size_t *got);

/* ---- the receive core, for drivers ---------------------------------------------------- */

/**
 * emb_drv_uart_rx_init() - Start a driver's receive state on its ring.
 * @rx:   The state, in the driver's RAM.
 * @ring: The ring's storage, generated for the device (its rx_buffer).
 * @size: Its bytes, 1 to 65535.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 *
 * From the driver's init. A NULL @rx is ignored.
 */
void emb_drv_uart_rx_init(emb_drv_uart_rx_t *rx, uint8_t *ring, size_t size);

/**
 * emb_drv_uart_rx_byte() - A byte arrived: give it to the read, or keep it in the ring.
 * @dev:  The device.
 * @rx:   Its receive state.
 * @byte: The byte.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus the read's completion when this byte fills it
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 *
 * From the driver's receive interrupt. With the ring full the byte is lost and counted,
 * and the next read reports EMB_EOVERRUN.
 */
void emb_drv_uart_rx_byte(const emb_device_t *dev, emb_drv_uart_rx_t *rx, uint8_t byte);

/**
 * emb_drv_uart_rx_idle() - The line went idle.
 * @dev: The device.
 * @rx:  Its receive state.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus the read's completion
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 *
 * Ends the read in progress with what it received, when it asked for EMB_DRV_UART_IDLE
 * and has received a byte.
 */
void emb_drv_uart_rx_idle(const emb_device_t *dev, emb_drv_uart_rx_t *rx);

/**
 * emb_drv_uart_rx_error() - A byte arrived damaged.
 * @dev:    The device.
 * @rx:     Its receive state.
 * @status: EMB_EFRAMING or EMB_EPARITY.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus the read's completion
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 *
 * Ends the read in progress with @status and the bytes before the damaged one; with no
 * read in progress, the byte is dropped.
 */
void emb_drv_uart_rx_error(const emb_device_t *dev, emb_drv_uart_rx_t *rx, emb_status_t status);

/**
 * emb_drv_uart_rx_start() - Begin a read: from the driver's start() for lane 1.
 * @dev: The device.
 * @rx:  Its receive state.
 * @req: The read.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(min(len, the ring's bytes))
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 *
 * Takes the ring's bytes first. The read ends here when they fill it, when it asked for
 * EMB_DRV_UART_IDLE and got a byte, or when bytes were lost before it; otherwise it waits
 * for the bytes the driver gives emb_drv_uart_rx_byte(). The driver's start() returns what
 * this returns.
 *
 * Return: EMB_EBUSY (the read waits); EMB_OK (it ended here); EMB_EOVERRUN (bytes were lost
 * before it); EMB_EINVAL for a NULL @rx or @req.
 */
emb_status_t emb_drv_uart_rx_start(const emb_device_t *dev, emb_drv_uart_rx_t *rx,
                                   emb_drv_uart_req_t *req);

/**
 * emb_drv_uart_rx_stop() - Stop a read: from the driver's stop() for lane 1.
 * @dev: The device.
 * @rx:  Its receive state.
 * @req: The read.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus the read's completion
 * @owns     none
 * @config   CONFIG_EMB_DRV_UART
 * @req      DRV-006
 * @since    0.3
 * @stable   no
 *
 * Completes the read with EMB_ECANCELED and what it received.
 */
void emb_drv_uart_rx_stop(const emb_device_t *dev, emb_drv_uart_rx_t *rx, emb_drv_uart_req_t *req);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_UART_H */
