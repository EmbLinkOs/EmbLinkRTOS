/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The SPI controller class (SPEC-017 §5): its requests and blocking wrapper over the device
 * core. A controller runs one transfer at a time, on lane 0. */
#include <emb/drv_spi.h>

#include <embk/kernel.h>

static bool is_spi(const emb_device_t *dev)
{
    return dev != NULL && dev->api != NULL && dev->class_id == EMB_DRV_CLASS_SPI;
}

void emb_drv_spi_req_transfer(emb_drv_spi_req_t *req, const emb_drv_spi_target_t *target,
                              const void *tx, void *rx, size_t len)
{
    if (req == NULL) {
        return;
    }
    emb_drv_request_init(&req->req);
    req->target = target;
    req->tx = (const uint8_t *)tx;
    req->rx = (uint8_t *)rx;
    req->len = len;
}

emb_status_t emb_drv_spi_submit(const emb_device_t *dev, emb_drv_spi_req_t *req)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_SUBMIT, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_SUBMIT,
                     req != NULL && req->target != NULL && req->target->hz != 0u &&
                         req->target->mode <= EMB_DRV_SPI_MODE_3,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    return emb_drv_submit(dev, &req->req);
}

emb_status_t emb_drv_spi_transfer(const emb_device_t *dev, const emb_drv_spi_target_t *target,
                                  const void *tx, void *rx, size_t len, emb_timeout_t timeout,
                                  size_t *done)
{
    emb_drv_spi_req_t req;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_SPI_TRANSFER);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_TRANSFER, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_TRANSFER,
                     target != NULL && target->hz != 0u && target->mode <= EMB_DRV_SPI_MODE_3,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    emb_drv_spi_req_transfer(&req, target, tx, rx, len);
    st = emb_drv_wait(dev, &req.req, timeout);
    if (done != NULL) {
        *done = (size_t)req.req.done;
    }
    return st;
}
