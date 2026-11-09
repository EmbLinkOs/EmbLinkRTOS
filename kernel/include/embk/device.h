/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The kernel's side of the device model (SPEC-016 §4). */
#ifndef EMBK_DEVICE_H
#define EMBK_DEVICE_H

#include <emb/device.h>

/* Initializes the board's devices of one level (EMB_DEVICE_EARLY or EMB_DEVICE_KERNEL) that
 * are still UNINIT, in the table's order. */
void embk_device_init_level(uint8_t level);

#endif /* EMBK_DEVICE_H */
