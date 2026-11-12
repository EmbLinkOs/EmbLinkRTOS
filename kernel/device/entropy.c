/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The entropy class (SPEC-017 §11, SEC-012, A124): a read assembled from a driver's blocks,
 * under the device's lock and one timeout; a block that fails its health test ends the read,
 * and what the class held of a block is cleared before it returns. */
#include <emb/drv_entropy.h>

#include <embk/drv_bus.h>
#include <embk/kernel.h>

static bool is_entropy(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_ENTROPY) &&
           (dev->data != NULL);
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_bus_t *entropy_lock(const emb_device_t *dev)
{
    return (emb_drv_bus_t *)dev->data;
}

/* The class's operations: the device's first. MISRA Dev CS-12: rule 11.3. */
static const emb_drv_entropy_api_t *entropy_api(const emb_device_t *dev)
{
    return (const emb_drv_entropy_api_t *)(const void *)dev->api;
}

emb_status_t emb_drv_entropy_board(const emb_device_t **out)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ENTROPY_BOARD, out != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    *out = emb_board_entropy;
    return (emb_board_entropy != NULL) ? EMB_OK : EMB_ENOTSUP;
}

emb_status_t emb_drv_entropy_lock_init(const emb_device_t *dev)
{
    if (!is_entropy(dev)) {
        return EMB_EINVAL;
    }
    return embk_bus_init(entropy_lock(dev), dev->name);
}

/* Cleared through a volatile pointer, so the compiler keeps the stores. */
static void wipe(uint8_t *p, size_t n)
{
    volatile uint8_t *v = p;
    size_t i;
    for (i = 0u; i < n; i++) { /* bounded: a block */
        v[i] = 0u;
    }
}

emb_status_t emb_drv_entropy_read(const emb_device_t *dev, void *buf, size_t len, size_t *got,
                                  emb_timeout_t timeout)
{
    const emb_drv_entropy_api_t *api;
    uint8_t block[EMB_DRV_ENTROPY_BLOCK_MAX];
    uint8_t *out = (uint8_t *)buf;
    emb_instant_t start = emb_time_now();
    emb_status_t st;
    size_t done = 0u;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ENTROPY_READ, is_entropy(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ENTROPY_READ, (buf != NULL) || (len == 0u),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_ENTROPY_READ);
    api = entropy_api(dev);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_ENTROPY_READ,
                     (api->block_size != 0u) && (api->block_size <= EMB_DRV_ENTROPY_BLOCK_MAX),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (got != NULL) {
        *got = 0u;
    }
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    st = embk_bus_enter(entropy_lock(dev), NULL, timeout);
    if (st != EMB_OK) {
        return st; /* another thread's read ran past the timeout */
    }
    while (done < len) { /* bounded: len, a block at a time */
        size_t n = len - done;
        size_t i;
        st = api->block(dev, block, embk_bus_time_left(timeout, start));
        if (st != EMB_OK) {
            break; /* a failed block is not delivered */
        }
        if (n > api->block_size) {
            n = api->block_size;
        }
        for (i = 0u; i < n; i++) { /* bounded: a block */
            out[done + i] = block[i];
        }
        done += n;
    }
    wipe(block, sizeof(block));
    embk_bus_exit(entropy_lock(dev));
    if (got != NULL) {
        *got = done;
    }
    return st;
}
