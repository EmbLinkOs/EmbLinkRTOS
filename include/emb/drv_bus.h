/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    drv_bus.h
 * @brief   A bus held across requests (SPEC-016 §6).
 *
 * @details
 * What a class keeps, first in each controller's RAM, so that one target has the bus for a sequence
 * of transfers. The SPI and I2C classes build their acquire and release on it; drivers only declare
 * it and initialize it through their class (emb_drv_spi_bus_init(), emb_drv_i2c_bus_init()).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_DRV_BUS_H
#define EMB_DRV_BUS_H

#include <emb/mutex.h>
#include <emb/storage.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A recursive mutex with priority inheritance, and the target holding the bus. */
typedef struct emb_drv_bus {
    emb_mutex_storage_t storage;
    emb_mutex_t lock;
    const void *held; /* the target its class gave the bus to, or NULL */
    uint32_t depth;   /* the holder's acquires not yet released */
} emb_drv_bus_t;

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_BUS_H */
