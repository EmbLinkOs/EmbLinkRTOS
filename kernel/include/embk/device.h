/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    device.h
 * @brief   The kernel's side of the device model (SPEC-016 §4).
 *
 * @see     kernel/device/device.c
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMBK_DEVICE_H
#define EMBK_DEVICE_H

#include <emb/device.h>

/* Initializes the board's devices of one level (EMB_DEVICE_EARLY or EMB_DEVICE_KERNEL) that
 * are still UNINIT, in the table's order. */
void embk_device_init_level(uint8_t level);

#endif /* EMBK_DEVICE_H */
