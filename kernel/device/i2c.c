/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The I2C class (SPEC-017 §6): its requests and blocking wrappers over the device core. A
 * controller's transactions run on lane 0, the transactions it answers as a target on
 * lane 1 (A114). */
#include <emb/drv_i2c.h>

#include <embk/drv_bus.h>
#include <embk/kernel.h>

#define ADDR_FIRST 0x08u /* 0x00 to 0x07 and 0x78 to 0x7F are reserved (UM10204 §3.1.12) */
#define ADDR_LAST  0x77u

static bool is_i2c(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_I2C) &&
           (dev->data != NULL);
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_bus_t *bus_of(const emb_device_t *dev)
{
    return (emb_drv_bus_t *)dev->data;
}

/* What is left of @t since @start: a finite timeout spent waiting for the bus is not spent
 * again on the transaction. */
static emb_timeout_t remaining(emb_timeout_t t, emb_instant_t start)
{
    emb_tick_t spent;
    if ((t.ticks == 0u) || (t.ticks == EMB_TICK_MAX)) {
        return t;
    }
    spent = emb_instant_sub(emb_time_now(), start).ticks;
    return (spent >= t.ticks) ? EMB_NO_WAIT : EMB_TIMEOUT_(t.ticks - spent);
}

emb_status_t emb_drv_i2c_bus_init(const emb_device_t *dev)
{
    if (!is_i2c(dev)) {
        return EMB_EINVAL;
    }
    return embk_bus_init(bus_of(dev), dev->name);
}

static bool address_ok(uint32_t a)
{
    return (a >= ADDR_FIRST) && (a <= ADDR_LAST);
}

static bool segs_ok(const emb_drv_i2c_seg_t *segs, size_t count)
{
    size_t i;
    if ((segs == NULL) || (count == 0u)) {
        return false;
    }
    for (i = 0u; i < count; i++) { /* bounded by count */
        bool wr = segs[i].wr != NULL;
        bool rd = segs[i].rd != NULL;
        if ((wr == rd) || (segs[i].len == 0u)) {
            return false; /* one buffer, and something in it */
        }
    }
    return true;
}

static bool request_ok(const emb_drv_i2c_req_t *req)
{
    if (req == NULL) {
        return false;
    }
    if (req->op == EMB_DRV_I2C_OP_LISTEN) {
        return address_ok(req->own) && ((req->rx_len == 0u) || (req->rx != NULL)) &&
               ((req->tx_len == 0u) || (req->tx != NULL));
    }
    return (req->target != NULL) && (req->target->hz != 0u) && address_ok(req->target->address) &&
           segs_ok(req->segs, req->count);
}

void emb_drv_i2c_req_transfer(emb_drv_i2c_req_t *req, const emb_drv_i2c_target_t *target,
                              const emb_drv_i2c_seg_t *segs, size_t count)
{
    if (req == NULL) {
        return;
    }
    emb_drv_request_init(&req->req);
    req->req.lane = EMB_DRV_I2C_LANE_CONTROLLER;
    req->target = target;
    req->segs = segs;
    req->count = count;
    req->rx = NULL;
    req->rx_len = 0u;
    req->tx = NULL;
    req->tx_len = 0u;
    req->sent = 0u;
    req->own = 0u;
    req->op = EMB_DRV_I2C_OP_TRANSFER;
}

void emb_drv_i2c_req_listen(emb_drv_i2c_req_t *req, uint16_t own, uint8_t *rx, size_t rx_len,
                            const uint8_t *tx, size_t tx_len)
{
    if (req == NULL) {
        return;
    }
    emb_drv_request_init(&req->req);
    req->req.lane = EMB_DRV_I2C_LANE_TARGET;
    req->target = NULL;
    req->segs = NULL;
    req->count = 0u;
    req->rx = rx;
    req->rx_len = rx_len;
    req->tx = tx;
    req->tx_len = tx_len;
    req->sent = 0u;
    req->own = own;
    req->op = EMB_DRV_I2C_OP_LISTEN;
}

emb_status_t emb_drv_i2c_submit(const emb_device_t *dev, emb_drv_i2c_req_t *req)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_SUBMIT, is_i2c(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_SUBMIT, request_ok(req), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     2u);
    if (req->op == EMB_DRV_I2C_OP_TRANSFER) {
        emb_status_t st = embk_bus_admit(bus_of(dev), req->target);
        if (st != EMB_OK) {
            return st; /* the bus is held for another target */
        }
    }
    return emb_drv_submit(dev, &req->req);
}

emb_status_t emb_drv_i2c_transfer(const emb_device_t *dev, const emb_drv_i2c_target_t *target,
                                  const emb_drv_i2c_seg_t *segs, size_t count,
                                  emb_timeout_t timeout, size_t *done)
{
    emb_drv_i2c_req_t req;
    emb_instant_t start;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_I2C_TRANSFER);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_TRANSFER, is_i2c(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    emb_drv_i2c_req_transfer(&req, target, segs, count);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_TRANSFER, request_ok(&req), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    if (done != NULL) {
        *done = 0u;
    }
    start = emb_time_now();
    st = embk_bus_enter(bus_of(dev), target, timeout); /* another thread's hold ends first */
    if (st != EMB_OK) {
        return st;
    }
    st = emb_drv_wait(dev, &req.req, remaining(timeout, start));
    embk_bus_exit(bus_of(dev));
    if (done != NULL) {
        *done = (size_t)req.req.done;
    }
    return st;
}

emb_status_t emb_drv_i2c_write_read(const emb_device_t *dev, const emb_drv_i2c_target_t *target,
                                    const void *wr, size_t wr_len, void *rd, size_t rd_len,
                                    emb_timeout_t timeout)
{
    emb_drv_i2c_seg_t segs[2];
    size_t n = 0u;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_I2C_WRITE_READ);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_WRITE_READ,
                     ((wr != NULL) || (rd != NULL)) && ((wr == NULL) == (wr_len == 0u)) &&
                         ((rd == NULL) == (rd_len == 0u)),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    if (wr != NULL) {
        segs[n].wr = (const uint8_t *)wr;
        segs[n].rd = NULL;
        segs[n].len = wr_len;
        n++;
    }
    if (rd != NULL) {
        segs[n].wr = NULL;
        segs[n].rd = (uint8_t *)rd;
        segs[n].len = rd_len;
        n++;
    }
    return emb_drv_i2c_transfer(dev, target, segs, n, timeout, NULL);
}

emb_status_t emb_drv_i2c_listen(const emb_device_t *dev, uint16_t own, void *rx, size_t rx_len,
                                const void *tx, size_t tx_len, emb_timeout_t timeout,
                                size_t *received, size_t *sent)
{
    emb_drv_i2c_req_t req;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_I2C_LISTEN);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_LISTEN, is_i2c(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    emb_drv_i2c_req_listen(&req, own, (uint8_t *)rx, rx_len, (const uint8_t *)tx, tx_len);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_LISTEN, request_ok(&req), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    st = emb_drv_wait(dev, &req.req, timeout);
    if (received != NULL) {
        *received = (size_t)req.req.done;
    }
    if (sent != NULL) {
        *sent = req.sent;
    }
    return st;
}

emb_status_t emb_drv_i2c_acquire(const emb_device_t *dev, const emb_drv_i2c_target_t *target,
                                 emb_timeout_t timeout)
{
    bool first = false;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_I2C_ACQUIRE);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_ACQUIRE, is_i2c(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_ACQUIRE,
                     (target != NULL) && (target->hz != 0u) && address_ok(target->address),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    return embk_bus_acquire(bus_of(dev), target, timeout, &first);
}

emb_status_t emb_drv_i2c_release(const emb_device_t *dev)
{
    const void *ended = NULL;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_I2C_RELEASE);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_RELEASE, is_i2c(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    return embk_bus_release(bus_of(dev), &ended);
}
