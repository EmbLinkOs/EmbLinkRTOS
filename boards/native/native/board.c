/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    board.c
 * @brief   The host as a board.
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#include <emb/config.h>
#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#include <emb/drv_dma.h>
#include <emb/drv_entropy.h>

#include <stdint.h>
#endif

#include <stdio.h>

#include <emb_board.h>
#include <emb_native.h> /* the C library is host code (A93) */
#include <sys/random.h> /* getentropy() */

void emb_board_init(void)
{
}

void emb_board_led_toggle(void)
{
    /* the host has no LED */
}

void emb_board_puts(const char *s)
{
    emb_native_host_begin();
    (void)fputs(s, stdout);
    emb_native_host_end();
}

void emb_board_put_u32(uint32_t v)
{
    emb_native_host_begin();
    (void)printf("%lu", (unsigned long)v);
    emb_native_host_end();
}

#if CONFIG_EMB_DEVICE && CONFIG_EMB_DRV_ENTROPY
/* The board's one device, by hand, as it has no description to generate it from (SPEC-016
 * §8): its entropy source, the host's own randomness through getentropy(), real entropy from
 * the operating system and not a generator (SEC-012), 32 bytes a block. */
static emb_drv_bus_t native_entropy_lock; /* its data: the class's lock, and nothing more */
static emb_device_state_t native_entropy_state;

static emb_status_t native_entropy_init(const emb_device_t *dev)
{
    return emb_drv_entropy_lock_init(dev);
}

static emb_status_t native_entropy_block(const emb_device_t *dev, uint8_t *buf,
                                         emb_timeout_t timeout)
{
    int rc;
    (void)dev;
    (void)timeout; /* the host answers at once */
    emb_native_host_begin();
    rc = getentropy(buf, 32u);
    emb_native_host_end();
    return (rc == 0) ? EMB_OK : EMB_EIO;
}

static const emb_drv_entropy_api_t native_entropy_api = {
    {native_entropy_init, NULL, NULL, NULL, NULL, 0u}, native_entropy_block, 32u, {0u, 0u, 0u}};

static const emb_device_t native_entropy_dev = {"entropy",
                                                &native_entropy_api.dev,
                                                NULL,
                                                &native_entropy_state,
                                                &native_entropy_lock,
                                                NULL,
                                                EMB_DRV_CLASS_ENTROPY,
                                                EMB_DEVICE_KERNEL,
                                                0u};
static const emb_device_t *const native_devices[] = {&native_entropy_dev};
const emb_device_table_t emb_board_device_table = {native_devices, 1u, 0u};
const emb_device_t *const emb_board_entropy = &native_entropy_dev;
#elif CONFIG_EMB_DEVICE
/* No devices: the board has no description to generate them from (SPEC-016 §8). */
const emb_device_table_t emb_board_device_table = {NULL, 0u, 0u};
#endif

#if CONFIG_EMB_DRV_DMA
/* A host process: its DMA devices are fakes, which reach whatever it can. */
const emb_drv_dma_region_t emb_board_dma_regions[1] = {{0u, SIZE_MAX}};
const unsigned emb_board_dma_region_count = 1u;
#endif
