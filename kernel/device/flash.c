/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    flash.c
 * @brief   The flash class (SPEC-017 §9, A122).
 *
 * @details
 * Its checks over a driver's operations (bounds, the write unit, whole sectors), a program read
 * back, an erase sector by sector, and the device's lock around each operation; and the partitions
 * on top (<emb/flash_area.h>).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_flash.h>
#include <emb/flash_area.h>

#include <embk/drv_bus.h>
#include <embk/kernel.h>

#define VERIFY_CHUNK 32u /* bytes read back at a time */

/* What an operation does. */
#define OP_READ    0u
#define OP_PROGRAM 1u
#define OP_ERASE   2u

static bool is_flash(const emb_device_t *dev)
{
    return (dev != NULL) && (dev->api != NULL) && (dev->class_id == EMB_DRV_CLASS_FLASH) &&
           (dev->data != NULL);
}

/* The class's part of the driver's RAM, its first member. MISRA Dev CS-12: rule 11.5. */
static emb_drv_bus_t *flash_lock(const emb_device_t *dev)
{
    return (emb_drv_bus_t *)dev->data;
}

/* The class's operations: the device's first. MISRA Dev CS-12: rule 11.3. */
static const emb_drv_flash_api_t *flash_api(const emb_device_t *dev)
{
    return (const emb_drv_flash_api_t *)(const void *)dev->api;
}

static bool in_range(uint32_t offset, size_t len, uint32_t size)
{
    uint32_t room = (offset <= size) ? (size - offset) : 0u;
    return (offset <= size) && (len <= (size_t)room);
}

emb_status_t emb_drv_flash_lock_init(const emb_device_t *dev)
{
    if (!is_flash(dev)) {
        return EMB_EINVAL;
    }
    return embk_bus_init(flash_lock(dev), dev->name);
}

emb_status_t emb_drv_flash_geometry_get(const emb_device_t *dev,
                                        const emb_drv_flash_geometry_t **out)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_GEOMETRY_GET, is_flash(dev) && (out != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    *out = flash_api(dev)->geometry(dev);
    return EMB_OK;
}

/* The sector @offset falls in, from the geometry: bounded by its sector count. */
static bool sector_of(const emb_drv_flash_geometry_t *g, uint32_t offset, uint32_t *start,
                      uint32_t *size)
{
    uint32_t at = 0u;
    uint32_t i;
    if (offset >= g->size) {
        return false;
    }
    if (g->sectors == NULL) {
        *start = offset - (offset % g->sector_size);
        *size = g->sector_size;
        return true;
    }
    for (i = 0u; i < g->sector_count; i++) { /* bounded: the sectors */
        if (offset < (at + g->sectors[i])) {
            *start = at;
            *size = g->sectors[i];
            return true;
        }
        at += g->sectors[i];
    }
    return false;
}

emb_status_t emb_drv_flash_sector(const emb_device_t *dev, uint32_t offset, uint32_t *start,
                                  uint32_t *size)
{
    uint32_t s = 0u;
    uint32_t n = 0u;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_SECTOR, is_flash(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    if (!sector_of(flash_api(dev)->geometry(dev), offset, &s, &n)) {
        return EMB_EINVAL;
    }
    if (start != NULL) {
        *start = s;
    }
    if (size != NULL) {
        *size = n;
    }
    return EMB_OK;
}

/* A program read back against what was written. */
static emb_status_t verify(const emb_device_t *dev, uint32_t offset, const uint8_t *buf, size_t len)
{
    const emb_drv_flash_api_t *api = flash_api(dev);
    uint8_t back[VERIFY_CHUNK];
    size_t done = 0u;
    while (done < len) { /* bounded: len */
        size_t n = ((len - done) < VERIFY_CHUNK) ? (len - done) : VERIFY_CHUNK;
        size_t i;
        emb_status_t st = api->read(dev, offset + (uint32_t)done, back, n);
        if (st != EMB_OK) {
            return st;
        }
        for (i = 0u; i < n; i++) { /* bounded: a chunk */
            if (back[i] != buf[done + i]) {
                return EMB_EVERIFY;
            }
        }
        done += n;
    }
    return EMB_OK;
}

/* Whole sectors, one at a time, so the clock catches up between them. */
static emb_status_t erase_sectors(const emb_device_t *dev, uint32_t offset, uint32_t len)
{
    const emb_drv_flash_api_t *api = flash_api(dev);
    const emb_drv_flash_geometry_t *g = api->geometry(dev);
    uint32_t at = offset;
    uint32_t end = offset + len;
    uint32_t start = 0u;
    uint32_t size = 0u;
    if (!sector_of(g, offset, &start, &size) || (start != offset)) {
        return EMB_EINVAL; /* not a sector's start */
    }
    if ((len != 0u) && (!sector_of(g, end - 1u, &start, &size) || ((start + size) != end))) {
        return EMB_EINVAL; /* not a sector's end */
    }
    while (at < end) { /* bounded: the sectors in the range */
        emb_status_t st;
        (void)sector_of(g, at, &start, &size);
        st = api->erase(dev, at, size);
        if (st != EMB_OK) {
            return st;
        }
        at += size;
    }
    return EMB_OK;
}

/* An operation's checks, then the driver's; the caller holds the lock where there is one. */
static emb_status_t run(const emb_device_t *dev, uint8_t op, uint32_t offset, void *rbuf,
                        const void *wbuf, size_t len)
{
    const emb_drv_flash_api_t *api = flash_api(dev);
    const emb_drv_flash_geometry_t *g = api->geometry(dev);
    emb_status_t st;
    if (!emb_device_ready(dev)) {
        return EMB_ESTATE;
    }
    if (!in_range(offset, len, g->size)) {
        return EMB_EINVAL;
    }
    if (len == 0u) {
        return EMB_OK;
    }
    if (op == OP_READ) {
        return api->read(dev, offset, rbuf, len);
    }
    if (op == OP_ERASE) {
        return erase_sectors(dev, offset, (uint32_t)len);
    }
    if (((offset % g->write_unit) != 0u) || ((len % g->write_unit) != 0u)) {
        return EMB_EINVAL;
    }
    st = api->program(dev, offset, wbuf, len);
    if (st != EMB_OK) {
        return st;
    }
    /* MISRA Dev CS-12: rule 11.5, the caller's bytes */
    return verify(dev, offset, (const uint8_t *)wbuf, len);
}

static emb_status_t locked(const emb_device_t *dev, uint8_t op, uint32_t offset, void *rbuf,
                           const void *wbuf, size_t len, emb_timeout_t timeout)
{
    emb_drv_bus_t *lock = flash_lock(dev);
    emb_status_t st = embk_bus_enter(lock, NULL, timeout);
    if (st != EMB_OK) {
        return st;
    }
    st = run(dev, op, offset, rbuf, wbuf, len);
    embk_bus_exit(lock);
    return st;
}

emb_status_t emb_drv_flash_read(const emb_device_t *dev, uint32_t offset, void *buf, size_t len,
                                emb_timeout_t timeout)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_READ, is_flash(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_READ, (buf != NULL) || (len == 0u),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_FLASH_READ);
    return locked(dev, OP_READ, offset, buf, NULL, len, timeout);
}

emb_status_t emb_drv_flash_program(const emb_device_t *dev, uint32_t offset, const void *buf,
                                   size_t len, emb_timeout_t timeout)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_PROGRAM, is_flash(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_PROGRAM, (buf != NULL) || (len == 0u),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_FLASH_PROGRAM);
    return locked(dev, OP_PROGRAM, offset, NULL, buf, len, timeout);
}

emb_status_t emb_drv_flash_erase(const emb_device_t *dev, uint32_t offset, uint32_t len,
                                 emb_timeout_t timeout)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_ERASE, is_flash(dev), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_FLASH_ERASE);
    return locked(dev, OP_ERASE, offset, NULL, NULL, len, timeout);
}

emb_status_t emb_drv_flash_poll_read(const emb_device_t *dev, uint32_t offset, void *buf,
                                     size_t len)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_POLL_READ, is_flash(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_POLL_READ, (buf != NULL) || (len == 0u),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_DRV_FLASH_POLL_READ);
    return run(dev, OP_READ, offset, buf, NULL, len);
}

emb_status_t emb_drv_flash_poll_program(const emb_device_t *dev, uint32_t offset, const void *buf,
                                        size_t len)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_POLL_PROGRAM, is_flash(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_POLL_PROGRAM, (buf != NULL) || (len == 0u),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 3u);
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_DRV_FLASH_POLL_PROGRAM);
    return run(dev, OP_PROGRAM, offset, NULL, buf, len);
}

emb_status_t emb_drv_flash_poll_erase(const emb_device_t *dev, uint32_t offset, uint32_t len)
{
    EMBK_API_REQUIRE(EMB_API_ID_DRV_FLASH_POLL_ERASE, is_flash(dev), EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_DRV_FLASH_POLL_ERASE);
    return run(dev, OP_ERASE, offset, NULL, NULL, len);
}

/* ---- partitions ------------------------------------------------------------------------ */

static bool area_ok(const emb_flash_area_t *fa)
{
    return (fa != NULL) && is_flash(fa->dev);
}

emb_status_t emb_flash_area_read(const emb_flash_area_t *fa, uint32_t offset, void *buf, size_t len,
                                 emb_timeout_t timeout)
{
    EMBK_API_REQUIRE(EMB_API_ID_FLASH_AREA_READ, area_ok(fa), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    if (!in_range(offset, len, fa->size)) {
        return EMB_EINVAL;
    }
    return emb_drv_flash_read(fa->dev, fa->offset + offset, buf, len, timeout);
}

emb_status_t emb_flash_area_program(const emb_flash_area_t *fa, uint32_t offset, const void *buf,
                                    size_t len, emb_timeout_t timeout)
{
    EMBK_API_REQUIRE(EMB_API_ID_FLASH_AREA_PROGRAM, area_ok(fa), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    if (!in_range(offset, len, fa->size)) {
        return EMB_EINVAL;
    }
    return emb_drv_flash_program(fa->dev, fa->offset + offset, buf, len, timeout);
}

emb_status_t emb_flash_area_erase(const emb_flash_area_t *fa, uint32_t offset, uint32_t len,
                                  emb_timeout_t timeout)
{
    EMBK_API_REQUIRE(EMB_API_ID_FLASH_AREA_ERASE, area_ok(fa), EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    if (!in_range(offset, len, fa->size)) {
        return EMB_EINVAL;
    }
    return emb_drv_flash_erase(fa->dev, fa->offset + offset, len, timeout);
}
