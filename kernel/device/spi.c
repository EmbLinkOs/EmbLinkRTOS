/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The SPI controller class (SPEC-017 §5): its requests and blocking wrapper over the device
 * core, each target's chip select around its transfers, and the bus held across several
 * (SPEC-016 §6). A controller runs one transfer at a time, on lane 0. */
#include <emb/drv_spi.h>

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
static emb_drv_spi_bus_t *bus_of(const emb_device_t *dev)
{
    return (emb_drv_spi_bus_t *)dev->data;
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
    emb_drv_spi_bus_t *bus;
    emb_mutex_attr_t attr;
    if (!is_spi(dev)) {
        return EMB_EINVAL;
    }
    bus = bus_of(dev);
    bus->held = NULL;
    bus->depth = 0u;
    emb_mutex_attr_default(&attr); /* priority inheritance (SPEC-016 §6) */
    attr.name = dev->name;
    attr.flags |= EMB_MUTEX_RECURSIVE; /* the holder's own transfers take it again */
    return emb_mutex_init(&bus->storage, &attr, &bus->lock);
}

void emb_drv_spi_begin(const emb_device_t *dev, const emb_drv_spi_req_t *req)
{
    if (bus_of(dev)->held != req->target) {
        (void)emb_drv_gpio_assert(&req->target->cs, true);
    } /* a held bus asserted it at acquire */
}

void emb_drv_spi_end(const emb_device_t *dev, const emb_drv_spi_req_t *req)
{
    if (bus_of(dev)->held != req->target) {
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
    const emb_drv_spi_target_t *held;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_SUBMIT, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_SUBMIT, (req != NULL) && target_ok(req->target),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    held = bus_of(dev)->held; /* one load: only the holder's thread changes it */
    if ((held != NULL) && (held != req->target)) {
        return EMB_EBUSY; /* the bus is held for another target, its chip select asserted */
    }
    return emb_drv_submit(dev, &req->req);
}

emb_status_t emb_drv_spi_transfer(const emb_device_t *dev, const emb_drv_spi_target_t *target,
                                  const void *tx, void *rx, size_t len, emb_timeout_t timeout,
                                  size_t *done)
{
    emb_drv_spi_req_t req;
    emb_drv_spi_bus_t *bus;
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
    bus = bus_of(dev);
    start = emb_time_now();
    st = emb_mutex_lock(bus->lock, timeout); /* another thread's hold ends first */
    if (st != EMB_OK) {
        return st;
    }
    if ((bus->held != NULL) && (bus->held != target)) {
        (void)emb_mutex_unlock(bus->lock);
        return EMB_EBUSY; /* the caller holds it for another target */
    }
    emb_drv_spi_req_transfer(&req, target, tx, rx, len);
    st = emb_drv_wait(dev, &req.req, remaining(timeout, start));
    (void)emb_mutex_unlock(bus->lock);
    if (done != NULL) {
        *done = (size_t)req.req.done;
    }
    return st;
}

emb_status_t emb_drv_spi_acquire(const emb_device_t *dev, const emb_drv_spi_target_t *target,
                                 emb_timeout_t timeout)
{
    emb_drv_spi_bus_t *bus;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_SPI_ACQUIRE);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_ACQUIRE, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_ACQUIRE, target_ok(target), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 2u);
    bus = bus_of(dev);
    st = emb_mutex_lock(bus->lock, timeout);
    if (st != EMB_OK) {
        return st;
    }
    if ((bus->held != NULL) && (bus->held != target)) {
        (void)emb_mutex_unlock(bus->lock);
        return EMB_EBUSY;
    }
    if (bus->depth == 0u) {
        (void)emb_drv_gpio_assert(&target->cs, true);
        bus->held = target;
    }
    bus->depth++;
    return EMB_OK; /* the mutex stays locked, once per acquire */
}

emb_status_t emb_drv_spi_release(const emb_device_t *dev)
{
    emb_drv_spi_bus_t *bus;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_SPI_RELEASE);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SPI_RELEASE, is_spi(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    bus = bus_of(dev);
    /* the caller owns the lock only if it can take it again at once: the mutex is recursive
     * for its owner, and taken by someone else otherwise */
    if (emb_mutex_lock(bus->lock, EMB_NO_WAIT) != EMB_OK) {
        return EMB_EPERM;
    }
    if (bus->depth == 0u) {
        (void)emb_mutex_unlock(bus->lock);
        return EMB_EPERM; /* not acquired: the caller only took it here */
    }
    bus->depth--;
    if (bus->depth == 0u) {
        (void)emb_drv_gpio_assert(&bus->held->cs, false);
        bus->held = NULL;
    }
    (void)emb_mutex_unlock(bus->lock);  /* the check's */
    return emb_mutex_unlock(bus->lock); /* the acquire's */
}
