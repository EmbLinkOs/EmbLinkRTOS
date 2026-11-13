/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    flash_area.h
 * @brief   Flash partitions (SPEC-017 §9, SPEC-014 §4, A122).
 *
 * @details
 * Each of the board's flash_partitions, its flash device, offset and size, generated into
 * soc_devices.c as EMB_FLASH_AREA_<NAME>. Offsets given to these calls count from the partition's
 * start, and nothing outside it can be reached through them. MCUboot's flash map is built on them
 * (ADR-058).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_FLASH_AREA_H
#define EMB_FLASH_AREA_H

#include <emb/drv_flash.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emb_flash_area {
    const char *name;
    const emb_device_t *dev; /* the flash it is on */
    uint32_t offset;         /* its start, from the flash's */
    uint32_t size;
} emb_flash_area_t;

/**
 * emb_flash_area_read() - Read from a partition.
 * @fa:      A partition, EMB_FLASH_AREA_<NAME>.
 * @offset:  Where, from the partition's start.
 * @buf:     Receives @len bytes.
 * @len:     Bytes to read.
 * @timeout: How long to wait for another thread's operation on the flash to end.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(flash_bytes) plus the lock's priority inheritance
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: as emb_drv_flash_read(); EMB_EINVAL also for a range outside the partition.
 */
emb_status_t emb_flash_area_read(const emb_flash_area_t *fa, uint32_t offset, void *buf, size_t len,
                                 emb_timeout_t timeout);

/**
 * emb_flash_area_program() - Program erased bytes of a partition.
 * @fa:      A partition.
 * @offset:  Where, from the partition's start: a multiple of the write unit.
 * @buf:     The bytes.
 * @len:     A multiple of the write unit.
 * @timeout: How long to wait for another thread's operation on the flash to end.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(flash_bytes) of the flash's own time, plus the lock's priority inheritance
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: as emb_drv_flash_program(); EMB_EINVAL also for a range outside the partition.
 */
emb_status_t emb_flash_area_program(const emb_flash_area_t *fa, uint32_t offset, const void *buf,
                                    size_t len, emb_timeout_t timeout);

/**
 * emb_flash_area_erase() - Erase whole sectors of a partition.
 * @fa:      A partition.
 * @offset:  From the partition's start: a sector's start.
 * @len:     Up to a sector's end.
 * @timeout: How long to wait for another thread's operation on the flash to end.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(flash_sectors) of the flash's own time, plus the lock's priority inheritance
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Partitions start and end on sector boundaries (the generator checks), so erasing a whole
 * partition is erasing 0 to its size.
 *
 * Return: as emb_drv_flash_erase(); EMB_EINVAL also for a range outside the partition.
 */
emb_status_t emb_flash_area_erase(const emb_flash_area_t *fa, uint32_t offset, uint32_t len,
                                  emb_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* EMB_FLASH_AREA_H */
