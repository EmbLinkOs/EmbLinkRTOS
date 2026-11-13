/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    bus.c
 * @brief   A bus held across requests (SPEC-016 §6), shared by the SPI and I2C classes.
 *
 * @details
 * A recursive mutex with priority inheritance, the key of the target holding it, and its depth.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <embk/drv_bus.h>

#include <stddef.h>

emb_status_t embk_bus_init(emb_drv_bus_t *bus, const char *name)
{
    emb_mutex_attr_t attr;
    bus->held = NULL;
    bus->depth = 0u;
    emb_mutex_attr_default(&attr); /* priority inheritance */
    attr.name = name;
    attr.flags |= EMB_MUTEX_RECURSIVE; /* the holder's own transfers take it again */
    return emb_mutex_init(&bus->storage, &attr, &bus->lock);
}

emb_status_t embk_bus_enter(emb_drv_bus_t *bus, const void *key, emb_timeout_t timeout)
{
    emb_status_t st = emb_mutex_lock(bus->lock, timeout); /* another thread's hold ends first */
    if (st != EMB_OK) {
        return st;
    }
    if ((bus->held != NULL) && (bus->held != key)) {
        (void)emb_mutex_unlock(bus->lock);
        return EMB_EBUSY; /* the caller holds it for another target */
    }
    return EMB_OK;
}

void embk_bus_exit(emb_drv_bus_t *bus)
{
    (void)emb_mutex_unlock(bus->lock);
}

emb_status_t embk_bus_acquire(emb_drv_bus_t *bus, const void *key, emb_timeout_t timeout,
                              bool *first)
{
    emb_status_t st = embk_bus_enter(bus, key, timeout);
    *first = false;
    if (st != EMB_OK) {
        return st;
    }
    if (bus->depth == 0u) {
        bus->held = key;
        *first = true;
    }
    bus->depth++;
    return EMB_OK; /* the mutex stays locked, once per acquire */
}

emb_status_t embk_bus_release(emb_drv_bus_t *bus, const void **ended)
{
    *ended = NULL;
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
        *ended = bus->held;
        bus->held = NULL;
    }
    (void)emb_mutex_unlock(bus->lock);  /* the check's */
    return emb_mutex_unlock(bus->lock); /* the acquire's */
}

emb_status_t embk_bus_admit(const emb_drv_bus_t *bus, const void *key)
{
    const void *held = bus->held; /* one load: only the holder's thread changes it */
    return ((held != NULL) && (held != key)) ? EMB_EBUSY : EMB_OK;
}

const void *embk_bus_holder(const emb_drv_bus_t *bus)
{
    return bus->held;
}

emb_timeout_t embk_bus_time_left(emb_timeout_t t, emb_instant_t start)
{
    emb_tick_t spent;
    if ((t.ticks == 0u) || (t.ticks == EMB_TICK_MAX)) {
        return t;
    }
    spent = emb_instant_sub(emb_time_now(), start).ticks;
    return (spent >= t.ticks) ? EMB_NO_WAIT : EMB_TIMEOUT_(t.ticks - spent);
}
