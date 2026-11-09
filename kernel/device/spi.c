/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The SPI controller class (SPEC-017 §5): its requests and blocking wrapper over the device
 * core, each target's chip select around its transfers, and the bus held across several
 * (SPEC-016 §6). A controller runs one transfer at a time, on lane 0. */
#include <emb/drv_spi.h>

#include <embk/drv_bus.h>
#include <embk/kernel.h>

static bool is_spi(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_SPI) &&
           (dev->data != NULL);
}

static bool target_ok(const emb_drv_spi_target_t *t)
{
    return (t != NULL) && (t->hz != 0u) && (t->mode <= EMB_DRV_SPI_MODE_3);
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_bus_t *bus_of(const emb_device_t *dev)
{
    return (emb_drv_bus_t *)dev->data;
}

/* What is left of @t since @start: a finite timeout spent waiting for the bus is not spent
 * again on the transfer. */
static emb_timeout_t remaining(emb_timeout_t t, emb_instant_t start)
{
    emb_tick_t spent;
    if ((t.ticks == 0u) || (t.ticks == EMB_TICK_MAX)) {
        return t;
    }
    spent = emb_instant_sub(emb_time_now(), start).ticks;
    return (spent >= t.ticks) ? EMB_NO_WAIT : EMB_TIMEOUT_(t.ticks - spent);
}

emb_status_t emb_drv_spi_bus_init(const emb_device_t *dev)
{
    if (!is_spi(dev)) {
        return EMB_EINVAL;
    }
    return embk_bus_init(bus_of(dev), dev->name);
}

void emb_drv_spi_begin(const emb_device_t *dev, const emb_drv_spi_req_t *req)
{
    if (embk_bus_holder(bus_of(dev)) != req->target) {
        (void)emb_drv_gpio_assert(&req->target->cs, true);
    } /* a held bus asserted it at acquire */
}

void emb_drv_spi_end(const emb_device_t *dev, const emb_drv_spi_req_t *req)
{
    if (embk_bus_holder(bus_of(dev)) != req->target) {
        (void)emb_drv_gpio_assert(&req->target->cs, false);
    }
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
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_SUBMIT, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_SUBMIT, (req != NULL) && target_ok(req->target),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    st = embk_bus_admit(bus_of(dev), req->target);
    if (st != EMB_OK) {
        return st; /* the bus is held for another target, its chip select asserted */
    }
    return emb_drv_submit(dev, &req->req);
}

emb_status_t emb_drv_spi_transfer(const emb_device_t *dev, const emb_drv_spi_target_t *target,
                                  const void *tx, void *rx, size_t len, emb_timeout_t timeout,
                                  size_t *done)
{
    emb_drv_spi_req_t req;
    emb_instant_t start;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_SPI_TRANSFER);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_TRANSFER, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_TRANSFER, target_ok(target), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    if (done != NULL) {
        *done = 0u;
    }
    start = emb_time_now();
    st = embk_bus_enter(bus_of(dev), target, timeout); /* another thread's hold ends first */
    if (st != EMB_OK) {
        return st;
    }
    emb_drv_spi_req_transfer(&req, target, tx, rx, len);
    st = emb_drv_wait(dev, &req.req, remaining(timeout, start));
    embk_bus_exit(bus_of(dev));
    if (done != NULL) {
        *done = (size_t)req.req.done;
    }
    return st;
}

emb_status_t emb_drv_spi_acquire(const emb_device_t *dev, const emb_drv_spi_target_t *target,
                                 emb_timeout_t timeout)
{
    bool first = false;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_SPI_ACQUIRE);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_ACQUIRE, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_ACQUIRE, target_ok(target), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    st = embk_bus_acquire(bus_of(dev), target, timeout, &first);
    if ((st == EMB_OK) && first) {
        (void)emb_drv_gpio_assert(&target->cs, true); /* asserted until the last release */
    }
    return st;
}

emb_status_t emb_drv_spi_release(const emb_device_t *dev)
{
    const void *ended = NULL;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_SPI_RELEASE);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_RELEASE, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    st = embk_bus_release(bus_of(dev), &ended);
    if (ended != NULL) {
        /* MISRA Dev CS-12: rule 11.5, the key the class gave the bus, its target */
        (void)emb_drv_gpio_assert(&((const emb_drv_spi_target_t *)ended)->cs, false);
    }
    return st;
}
