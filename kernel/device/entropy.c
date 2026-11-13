/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    entropy.c
 * @brief   The entropy class (SPEC-017 §11, SEC-012, A124).
 *
 * @details
 * A read assembled from a driver's blocks, under the device's lock and one timeout, conditioned by
 * SHA-256 over twice the raw bytes it puts out (NIST SP 800-90B §3.1.5); a block that fails its
 * health test ends the read; and what the class held is cleared before it returns. The raw read
 * skips the conditioning.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_entropy.h>

#include <embk/drv_bus.h>
#include <embk/kernel.h>
#include <embk/sha256.h>

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

/* A read's checks, shared by the conditioned and the raw read. */
static emb_status_t entropy_begin(const emb_device_t *dev, uint16_t api_id, const void *buf,
                                  size_t len, size_t *got, emb_timeout_t timeout)
{
    const emb_drv_entropy_api_t *api;
    EMBK_API_REQUIRE(api_id, is_entropy(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(api_id, (buf != NULL) || (len == 0u), EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE_THREAD(api_id);
    api = entropy_api(dev);
    EMBK_API_REQUIRE(api_id,
                     (api->block_size != 0u) && (api->block_size <= EMB_DRV_ENTROPY_BLOCK_MAX),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (got != NULL) {
        *got = 0u;
    }
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    return embk_bus_enter(entropy_lock(dev), NULL, timeout);
}

#define RAW_IN \
    ((size_t)EMB_DRV_ENTROPY_RAW_RATIO * (size_t)EMBK_SHA256_SIZE) /* raw bytes a digest */

emb_status_t emb_drv_entropy_read(const emb_device_t *dev, void *buf, size_t len, size_t *got,
                                  emb_timeout_t timeout)
{
    /* on the stack, not in RAM that outlives the read: the raw bytes and the digest are
     * entropy in transit, cleared before returning (CS-4.3) */
    uint8_t pool[RAW_IN + EMB_DRV_ENTROPY_BLOCK_MAX];
    uint8_t digest[EMBK_SHA256_SIZE];
    embk_sha256_t sha;
    uint8_t *out = (uint8_t *)buf;
    emb_instant_t start = emb_time_now();
    size_t done = 0u;
    size_t used = 0u;
    emb_status_t st = entropy_begin(dev, EMB_API_ID_DRV_ENTROPY_READ, buf, len, got, timeout);
    const emb_drv_entropy_api_t *api;
    if (st != EMB_OK) {
        return st;
    }
    api = entropy_api(dev);
    while ((st == EMB_OK) && (done < len)) { /* bounded: len, 32 bytes a turn */
        size_t n = len - done;
        size_t i;
        while ((st == EMB_OK) && (used < RAW_IN)) { /* bounded: RAW_IN, a block a turn */
            st = api->block(dev, &pool[used], embk_bus_time_left(timeout, start));
            used += api->block_size;
        }
        if (st != EMB_OK) {
            break; /* a failed block, and the raw bytes gathered with it, are dropped */
        }
        embk_sha256_start(&sha);
        embk_sha256_add(&sha, pool, RAW_IN);
        embk_sha256_end(&sha, digest);
        if (n > EMBK_SHA256_SIZE) {
            n = EMBK_SHA256_SIZE;
        }
        for (i = 0u; i < n; i++) { /* bounded: a digest */
            out[done + i] = digest[i];
        }
        done += n;
        for (i = RAW_IN; i < used; i++) { /* bounded: a block: what is left over goes first */
            pool[i - RAW_IN] = pool[i];
        }
        used -= RAW_IN;
    }
    wipe(pool, sizeof(pool));
    wipe(digest, sizeof(digest));
    embk_bus_exit(entropy_lock(dev));
    if (got != NULL) {
        *got = done;
    }
    return st;
}

emb_status_t emb_drv_entropy_read_raw(const emb_device_t *dev, void *buf, size_t len, size_t *got,
                                      emb_timeout_t timeout)
{
    uint8_t block[EMB_DRV_ENTROPY_BLOCK_MAX]; /* a block in transit, cleared before returning */
    uint8_t *out = (uint8_t *)buf;
    emb_instant_t start = emb_time_now();
    size_t done = 0u;
    emb_status_t st = entropy_begin(dev, EMB_API_ID_DRV_ENTROPY_READ_RAW, buf, len, got, timeout);
    const emb_drv_entropy_api_t *api;
    if (st != EMB_OK) {
        return st;
    }
    api = entropy_api(dev);
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
