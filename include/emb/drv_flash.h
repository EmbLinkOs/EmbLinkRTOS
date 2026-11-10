/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The flash class (SPEC-017 §9, A122): a NOR flash read, programmed and erased by offset, its
 * geometry from the description. An operation runs to its end in the caller: the STM32F446's
 * CPU stalls on its flash meanwhile, and the RP2350's runs from RAM with interrupts held off,
 * so a request queue would not let anything else run. Partitions (<emb/flash_area.h>) are
 * how applications use it. */
#ifndef EMB_DRV_FLASH_H
#define EMB_DRV_FLASH_H

#include <emb/device.h>
#include <emb/drv_bus.h>
#include <emb/time.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A flash's geometry, generated from the SoC's flash description and the board's chip size. */
typedef struct emb_drv_flash_geometry {
    uintptr_t base;          /* where reads see it mapped; offsets count from here */
    uint32_t size;           /* bytes */
    const uint32_t *sectors; /* each erase sector's size, in order; NULL when all are sector_size */
    uint32_t sector_size;    /* the sectors' size when sectors is NULL */
    uint16_t sector_count;
    uint16_t write_unit; /* a program's offset and length are multiples of it */
    uint8_t erased;      /* the value an erased byte reads */
    uint8_t reserved_[3];
} emb_drv_flash_geometry_t;

/* A flash driver's operations: the device's, then its geometry, and read, program and erase,
 * each run to its end. The class checks bounds, alignment and whole sectors first, so a driver
 * sees only operations it can do; program() needs the bytes erased. A driver's data begins
 * with the class's lock, which its init() sets up with emb_drv_flash_lock_init(). */
typedef struct emb_drv_flash_api {
    emb_device_api_t dev;
    const emb_drv_flash_geometry_t *(*geometry)(const emb_device_t *dev);
    emb_status_t (*read)(const emb_device_t *dev, uint32_t offset, void *buf, size_t len);
    emb_status_t (*program)(const emb_device_t *dev, uint32_t offset, const void *buf, size_t len);
    emb_status_t (*erase)(const emb_device_t *dev, uint32_t offset, uint32_t len);
} emb_drv_flash_api_t;

/**
 * emb_drv_flash_lock_init() - Set up the class's lock, first in a flash driver's data: for
 * drivers, in their init().
 * @dev: A device of the flash class.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1); O(active mutexes) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL (not a flash device with data).
 */
emb_status_t emb_drv_flash_lock_init(const emb_device_t *dev);

/**
 * emb_drv_flash_geometry() - A flash's geometry.
 * @dev: A device of the flash class.
 * @out: Receives the geometry, which lives as long as the device.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK.
 */
emb_status_t emb_drv_flash_geometry(const emb_device_t *dev, const emb_drv_flash_geometry_t **out);

/**
 * emb_drv_flash_sector() - The erase sector an offset falls in.
 * @dev:    A device of the flash class.
 * @offset: An offset in the flash.
 * @start:  Receives the sector's offset; may be NULL.
 * @size:   Receives the sector's size; may be NULL.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(flash_sectors)
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL (@offset past the flash's end).
 */
emb_status_t emb_drv_flash_sector(const emb_device_t *dev, uint32_t offset, uint32_t *start,
                                  uint32_t *size);

/**
 * emb_drv_flash_read() - Read from a flash.
 * @dev:     A device of the flash class.
 * @offset:  Where, from the flash's start.
 * @buf:     Receives @len bytes.
 * @len:     Bytes to read.
 * @timeout: How long to wait for another thread's operation to end.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(flash_bytes) plus the lock's priority inheritance
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001 DRV-003
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL (outside the flash); EMB_ETIMEDOUT; EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_flash_read(const emb_device_t *dev, uint32_t offset, void *buf, size_t len,
                                emb_timeout_t timeout);

/**
 * emb_drv_flash_program() - Program erased flash, and check it reads back.
 * @dev:     A device of the flash class.
 * @offset:  Where, a multiple of the write unit.
 * @buf:     The bytes.
 * @len:     A multiple of the write unit.
 * @timeout: How long to wait for another thread's operation to end.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(flash_bytes) of the flash's own time, plus the lock's priority inheritance
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001 DRV-003
 * @since    0.3
 * @stable   no
 *
 * NOR flash programs only bits that are erased: bytes not erased come back different, and are
 * reported as EMB_EVERIFY. While it runs, the STM32F446 stalls every fetch from flash and the
 * RP2350 holds interrupts off; the kernel's clock catches up afterwards (A122).
 *
 * Return: EMB_OK; EMB_EINVAL (unaligned, or outside the flash); EMB_EVERIFY (what reads back
 * differs); EMB_EWRPROT (write-protected); EMB_EIO (the controller reported an error);
 * EMB_ETIMEDOUT; EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_flash_program(const emb_device_t *dev, uint32_t offset, const void *buf,
                                   size_t len, emb_timeout_t timeout);

/**
 * emb_drv_flash_erase() - Erase whole sectors.
 * @dev:     A device of the flash class.
 * @offset:  The first sector's start.
 * @len:     Up to the end of the last sector.
 * @timeout: How long to wait for another thread's operation to end.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(flash_sectors) of the flash's own time, plus the lock's priority inheritance
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-001 DRV-003
 * @since    0.3
 * @stable   no
 *
 * An erase runs sector by sector, the clock catching up after each: a 128 KiB sector of the
 * STM32F446 takes one to two seconds, a 4 KiB sector of the RP2350's flash some 45 ms.
 *
 * Return: EMB_OK; EMB_EINVAL (not whole sectors, or outside the flash); EMB_EWRPROT; EMB_EIO;
 * EMB_ETIMEDOUT; EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_flash_erase(const emb_device_t *dev, uint32_t offset, uint32_t len,
                                 emb_timeout_t timeout);

/**
 * emb_drv_flash_poll_read() - Read from a flash without the kernel: early boot, a bootloader.
 * @dev:    A device of the flash class.
 * @offset: Where.
 * @buf:    Receives @len bytes.
 * @len:    Bytes to read.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(flash_bytes)
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-007
 * @since    0.3
 * @stable   no
 *
 * The polled calls take no lock: the caller makes sure no other operation runs.
 *
 * Return: as emb_drv_flash_read(), without EMB_ETIMEDOUT.
 */
emb_status_t emb_drv_flash_poll_read(const emb_device_t *dev, uint32_t offset, void *buf,
                                     size_t len);

/**
 * emb_drv_flash_poll_program() - Program erased flash without the kernel.
 * @dev:    A device of the flash class.
 * @offset: Where, a multiple of the write unit.
 * @buf:    The bytes.
 * @len:    A multiple of the write unit.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(flash_bytes) of the flash's own time
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-007
 * @since    0.3
 * @stable   no
 *
 * Return: as emb_drv_flash_program(), without EMB_ETIMEDOUT.
 */
emb_status_t emb_drv_flash_poll_program(const emb_device_t *dev, uint32_t offset, const void *buf,
                                        size_t len);

/**
 * emb_drv_flash_poll_erase() - Erase whole sectors without the kernel.
 * @dev:    A device of the flash class.
 * @offset: The first sector's start.
 * @len:    Up to the end of the last sector.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(flash_sectors) of the flash's own time
 * @owns     none
 * @config   CONFIG_EMB_DRV_FLASH
 * @req      DRV-007
 * @since    0.3
 * @stable   no
 *
 * Return: as emb_drv_flash_erase(), without EMB_ETIMEDOUT.
 */
emb_status_t emb_drv_flash_poll_erase(const emb_device_t *dev, uint32_t offset, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_FLASH_H */
