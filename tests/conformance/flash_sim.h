/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* A NOR flash in RAM, a flash class device (SPEC-017 §9, A122), for the tests on every target:
 * a program clears bits and never sets them, an erase sets a whole sector to 0xFF, and power
 * can be lost at any operation, tearing it, after which every operation fails until power
 * comes back (BOOT-007's injection). */
#ifndef FLASH_SIM_H
#define FLASH_SIM_H

#include <emb/drv_flash.h>

#include <stdbool.h>

typedef struct flash_sim {
    emb_drv_bus_t lock; /* the class's, first */
    uint8_t *mem;
    emb_drv_flash_geometry_t geometry;
    unsigned ops;     /* programs and erases so far */
    unsigned fail_at; /* the operation, counted from 1, that loses power; 0 for none */
    bool lost;        /* power is lost: everything fails with EMB_EIO */
} flash_sim_t;

extern const emb_drv_flash_api_t flash_sim_api;

/* Its storage set to garbage, power on, nothing failing; the device not yet initialized. */
void flash_sim_reset(flash_sim_t *sim, uint8_t *mem);

/* Power lost at the @n-th program or erase from now (1 is the next one); 0: never. */
void flash_sim_fail_at(flash_sim_t *sim, unsigned n);

/* Power back: operations run again, on what the torn one left. */
void flash_sim_power_on(flash_sim_t *sim);

#endif /* FLASH_SIM_H */
