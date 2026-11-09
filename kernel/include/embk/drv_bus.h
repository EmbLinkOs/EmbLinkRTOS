/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The held bus's operations, for the class cores (kernel/device/spi.c, i2c.c). A holder is
 * named by a key, the class's target. */
#ifndef EMBK_DRV_BUS_H
#define EMBK_DRV_BUS_H

#include <emb/drv_bus.h>
#include <emb/status.h>
#include <emb/time.h>

#include <stdbool.h>

/* A fresh bus: its mutex, named @name, recursive, with priority inheritance. */
emb_status_t embk_bus_init(emb_drv_bus_t *bus, const char *name);

/* Holds the bus for @key, waiting within @timeout for another thread's hold to end. *first is
 * true when the hold begins (the class then asserts the target's chip select). EMB_EBUSY when
 * the caller holds it for another key. */
emb_status_t embk_bus_acquire(emb_drv_bus_t *bus, const void *key, emb_timeout_t timeout,
                              bool *first);

/* Releases one acquire; *ended is the key whose hold ended with it, or NULL. EMB_EPERM when
 * the caller does not hold the bus. */
emb_status_t embk_bus_release(emb_drv_bus_t *bus, const void **ended);

/* Enters the bus for one transfer to @key: as acquire, without holding it after. */
emb_status_t embk_bus_enter(emb_drv_bus_t *bus, const void *key, emb_timeout_t timeout);
void embk_bus_exit(emb_drv_bus_t *bus);

/* For a request submitted from anywhere: EMB_EBUSY while the bus is held for another key. */
emb_status_t embk_bus_admit(const emb_drv_bus_t *bus, const void *key);

/* The key holding the bus, or NULL: one load, which only the holder's thread changes. */
const void *embk_bus_holder(const emb_drv_bus_t *bus);

#endif /* EMBK_DRV_BUS_H */
