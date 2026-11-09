/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The I2C class (SPEC-017 §6): its requests and blocking wrappers over the device core. A
 * controller's transactions run on lane 0, the transactions it answers as a target on
 * lane 1 (A114). */
#include <emb/drv_i2c.h>

#include <embk/kernel.h>

#define ADDR_FIRST 0x08u /* 0x00 to 0x07 and 0x78 to 0x7F are reserved (UM10204 §3.1.12) */
#define ADDR_LAST  0x77u

static bool is_i2c(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_I2C);
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
    return emb_drv_submit(dev, &req->req);
}

emb_status_t emb_drv_i2c_transfer(const emb_device_t *dev, const emb_drv_i2c_target_t *target,
                                  const emb_drv_i2c_seg_t *segs, size_t count,
                                  emb_timeout_t timeout, size_t *done)
{
    emb_drv_i2c_req_t req;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_I2C_TRANSFER);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_TRANSFER, is_i2c(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    emb_drv_i2c_req_transfer(&req, target, segs, count);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_I2C_TRANSFER, request_ok(&req), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    st = emb_drv_wait(dev, &req.req, timeout);
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
