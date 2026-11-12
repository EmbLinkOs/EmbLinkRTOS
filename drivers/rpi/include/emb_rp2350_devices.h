/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_rp2350_devices.h
 * @brief   The RP2350 drivers as devices (SPEC-016).
 *
 * @details
 * The operations and generated configurations the bindings name (drivers/bindings.yaml). Thin
 * adapters: the functions of emb_rp2350_drivers.h stay the low layer the board support uses before
 * the kernel.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_RP2350_DEVICES_H
#define EMB_RP2350_DEVICES_H

#include <emb/config.h>

#include <stdint.h>

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The clock generators: the clock tree the board support brought up. */
typedef struct emb_rp2350_clocks_config {
    uintptr_t base;
} emb_rp2350_clocks_config_t;

/* GPIO bank 0: its function select (IO_BANK0) and its pads (PADS_BANK0). */
typedef struct emb_rp2350_io_bank_config {
    uintptr_t base;
} emb_rp2350_io_bank_config_t;

#if CONFIG_EMB_DEVICE
/* READY when clk_ref runs from the crystal. */
extern const emb_device_api_t emb_rp2350_clocks_api;
/* Takes the bank and its pads out of reset; READY once they are. */
extern const emb_device_api_t emb_rp2350_io_bank_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_RP2350_DEVICES_H */
