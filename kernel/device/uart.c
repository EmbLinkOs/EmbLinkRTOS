/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The UART class (SPEC-017 §4): its requests and wrappers over the device core, and the
 * receive core its drivers share. The receive state's lock domain is the device's critical
 * section; a read is completed outside it, as the core delivers every completion.
 */
#include <emb/drv_uart.h>
#include <emb/irq.h>

#include <embk/kernel.h>

/* The class's table: a UART device's operations begin with the device's (A114's request
 * lanes run its writes and reads). MISRA Dev CS-12: rule 11.3, the table's first member. */
static const emb_drv_uart_api_t *uart_api(const emb_device_t *dev)
{
    return (const emb_drv_uart_api_t *)(const void *)dev->api;
}

static bool is_uart(const emb_device_t *dev)
{
    return dev != NULL && dev->api != NULL && dev->class_id == EMB_DRV_CLASS_UART;
}

static void prepare(emb_drv_uart_req_t *req, uint8_t op, size_t len)
{
    emb_drv_request_init(&req->req);
    req->req.lane = (op == EMB_DRV_UART_OP_READ) ? EMB_DRV_UART_LANE_RX : EMB_DRV_UART_LANE_TX;
    req->tx = NULL;
    req->rx = NULL;
    req->len = len;
    req->op = op;
    req->flags = 0u;
}

void emb_drv_uart_req_write(emb_drv_uart_req_t *req, const void *buf, size_t len)
{
    if (req == NULL) {
        return;
    }
    prepare(req, EMB_DRV_UART_OP_WRITE, len);
    req->tx = (const uint8_t *)buf;
}

void emb_drv_uart_req_read(emb_drv_uart_req_t *req, void *buf, size_t len, uint8_t flags)
{
    if (req == NULL) {
        return;
    }
    prepare(req, EMB_DRV_UART_OP_READ, len);
    req->rx = (uint8_t *)buf;
    req->flags = flags;
}

emb_status_t emb_drv_uart_submit(const emb_device_t *dev, emb_drv_uart_req_t *req)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_UART_SUBMIT, is_uart(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_UART_SUBMIT,
                     req != NULL && req->op <= EMB_DRV_UART_OP_READ &&
                         (req->len == 0u ||
                          (req->op == EMB_DRV_UART_OP_WRITE ? req->tx != NULL : req->rx != NULL)),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    return emb_drv_submit(dev, &req->req);
}

emb_status_t emb_drv_uart_write(const emb_device_t *dev, const void *buf, size_t len,
                                emb_timeout_t timeout, size_t *done)
{
    emb_drv_uart_req_t req;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_UART_WRITE);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_UART_WRITE, is_uart(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_UART_WRITE, buf != NULL || len == 0u, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    emb_drv_uart_req_write(&req, buf, len);
    st = emb_drv_wait(dev, &req.req, timeout);
    if (done != NULL) {
        *done = (size_t)req.req.done;
    }
    return st;
}

emb_status_t emb_drv_uart_read(const emb_device_t *dev, void *buf, size_t len, uint8_t flags,
                               emb_timeout_t timeout, size_t *got)
{
    emb_drv_uart_req_t req;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_UART_READ);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_UART_READ, is_uart(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_UART_READ, buf != NULL || len == 0u, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    emb_drv_uart_req_read(&req, buf, len, flags);
    st = emb_drv_wait(dev, &req.req, timeout);
    if (got != NULL) {
        *got = (size_t)req.req.done;
    }
    return st;
}

emb_status_t emb_drv_uart_poll_write(const emb_device_t *dev, const void *buf, size_t len)
{
    if (!is_uart(dev) || (buf == NULL && len != 0u)) {
        return EMB_EINVAL; /* no fault: the fault report itself calls this */
    }
    if (uart_api(dev)->poll_write == NULL) {
        return EMB_ENOTSUP;
    }
    return uart_api(dev)->poll_write(dev, (const uint8_t *)buf, len);
}

emb_status_t emb_drv_uart_poll_read(const emb_device_t *dev, void *buf, size_t len, size_t *got)
{
    if (!is_uart(dev) || got == NULL || (buf == NULL && len != 0u)) {
        return EMB_EINVAL;
    }
    if (uart_api(dev)->poll_read == NULL) {
        return EMB_ENOTSUP;
    }
    *got = uart_api(dev)->poll_read(dev, (uint8_t *)buf, len);
    return EMB_OK;
}

/* ---- the receive core ----------------------------------------------------------------- */

void emb_drv_uart_rx_init(emb_drv_uart_rx_t *rx, uint8_t *ring, size_t size)
{
    if (rx == NULL) {
        return;
    }
    rx->ring = ring;
    rx->size = (ring != NULL && size <= 0xFFFFu) ? (uint16_t)size : 0u;
    rx->head = 0u;
    rx->count = 0u;
    rx->overrun = 0u;
    rx->reserved_ = 0u;
    rx->active = NULL;
    rx->lost = 0u;
}

/* Ends a read the caller has detached from the state, outside the device's lock. */
static void finish(const emb_device_t *dev, emb_drv_uart_req_t *req, emb_status_t status)
{
    emb_drv_complete(dev, &req->req, status, req->req.done);
}

void emb_drv_uart_rx_byte(const emb_device_t *dev, emb_drv_uart_rx_t *rx, uint8_t byte)
{
    emb_drv_uart_req_t *full = NULL;
    emb_irq_key_t key;
    if (rx == NULL) {
        return;
    }
    key = emb_irq_lock();
    if (rx->active != NULL) {
        emb_drv_uart_req_t *r = rx->active;
        r->rx[r->req.done] = byte;
        r->req.done++;
        if ((size_t)r->req.done >= r->len) {
            rx->active = NULL;
            full = r;
        }
    } else if (rx->count < rx->size) {
        uint32_t at = ((uint32_t)rx->head + rx->count) % rx->size;
        rx->ring[at] = byte;
        rx->count++;
    } else {
        rx->lost++; /* no read, and the ring full: SPEC-017 §4's overrun */
        rx->overrun = 1u;
    }
    emb_irq_unlock(key);
    if (full != NULL) {
        finish(dev, full, EMB_OK);
    }
}

void emb_drv_uart_rx_idle(const emb_device_t *dev, emb_drv_uart_rx_t *rx)
{
    emb_drv_uart_req_t *r = NULL;
    emb_irq_key_t key;
    if (rx == NULL) {
        return;
    }
    key = emb_irq_lock();
    if (rx->active != NULL && (rx->active->flags & EMB_DRV_UART_IDLE) != 0u &&
        rx->active->req.done != 0u) {
        r = rx->active;
        rx->active = NULL;
    }
    emb_irq_unlock(key);
    if (r != NULL) {
        finish(dev, r, EMB_OK);
    }
}

void emb_drv_uart_rx_error(const emb_device_t *dev, emb_drv_uart_rx_t *rx, emb_status_t status)
{
    emb_drv_uart_req_t *r;
    emb_irq_key_t key;
    if (rx == NULL) {
        return;
    }
    key = emb_irq_lock();
    r = rx->active;
    rx->active = NULL;
    emb_irq_unlock(key);
    if (r != NULL) {
        finish(dev, r, status);
    }
}

emb_status_t emb_drv_uart_rx_start(const emb_device_t *dev, emb_drv_uart_rx_t *rx,
                                   emb_drv_uart_req_t *req)
{
    emb_status_t st = EMB_EBUSY;
    emb_irq_key_t key;
    (void)dev; /* the core ends a read this returns a status for (A115) */
    if (rx == NULL || req == NULL) {
        return EMB_EINVAL;
    }
    key = emb_irq_lock();
    /* bounded by the read's length and the ring's bytes */
    while (rx->count != 0u && (size_t)req->req.done < req->len) {
        req->rx[req->req.done] = rx->ring[rx->head];
        req->req.done++;
        rx->head = (uint16_t)((rx->head + 1u) % rx->size);
        rx->count--;
    }
    if (rx->overrun != 0u) {
        rx->overrun = 0u; /* the loss is this read's to report */
        st = EMB_EOVERRUN;
    } else if ((size_t)req->req.done >= req->len ||
               ((req->flags & EMB_DRV_UART_IDLE) != 0u && req->req.done != 0u)) {
        st = EMB_OK; /* filled, or idle reads take what was waiting */
    } else {
        rx->active = req;
    }
    emb_irq_unlock(key);
    return st;
}

void emb_drv_uart_rx_stop(const emb_device_t *dev, emb_drv_uart_rx_t *rx, emb_drv_uart_req_t *req)
{
    emb_irq_key_t key;
    if (rx == NULL || req == NULL) {
        return;
    }
    key = emb_irq_lock();
    if (rx->active == req) {
        rx->active = NULL;
    }
    emb_irq_unlock(key);
    finish(dev, req, EMB_ECANCELED); /* complete-once: nothing if it had ended */
}
