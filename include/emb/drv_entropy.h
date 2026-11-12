/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The entropy class (SPEC-017 §11, SEC-012, A124): bytes from a hardware source of randomness,
 * a block at a time, each block checked by the source's own health tests where it has them. A
 * board without a source has no entropy device, and says so: nothing stands in for one. The
 * bytes are raw entropy, for seeding a cryptographic generator, not a generator themselves. */
#ifndef EMB_DRV_ENTROPY_H
#define EMB_DRV_ENTROPY_H

#include <emb/device.h>
#include <emb/drv_bus.h>
#include <emb/time.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_ENTROPY_CAP_HEALTH_TEST 0x01u /* each block passes the source's health tests */

#define EMB_DRV_ENTROPY_BLOCK_MAX 64u /* the largest block a driver may give */

/* An entropy driver's operations: the device's, then one fresh block of block_size bytes,
 * waited for up to the timeout. A block that fails a health test is discarded and reported as
 * EMB_EHEALTH; no byte of it reaches the caller. A driver's data begins with the class's lock,
 * which its init() sets up with emb_drv_entropy_lock_init(). */
typedef struct emb_drv_entropy_api {
    emb_device_api_t dev;
    emb_status_t (*block)(const emb_device_t *dev, uint8_t *buf, emb_timeout_t timeout);
    uint8_t block_size;
    uint8_t reserved_[3];
} emb_drv_entropy_api_t;

/* The board's entropy device, or NULL where it has no source: every board defines it, the
 * generated soc_devices.c from the description, a hand-written board by hand. */
extern const emb_device_t *const emb_board_entropy;

/**
 * emb_drv_entropy_board() - The board's entropy source, if it has one.
 * @out: Receives the device.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_ENTROPY
 * @req      SEC-012
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ENOTSUP (the board has no source of entropy: none stands in for it).
 */
emb_status_t emb_drv_entropy_board(const emb_device_t **out);

/**
 * emb_drv_entropy_lock_init() - Set up the class's lock, first in an entropy driver's data:
 * for drivers, in their init().
 * @dev: A device of the entropy class.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1); O(active mutexes) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     none
 * @config   CONFIG_EMB_DRV_ENTROPY
 * @req      DRV-001
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL (not an entropy device with data).
 */
emb_status_t emb_drv_entropy_lock_init(const emb_device_t *dev);

/**
 * emb_drv_entropy_read() - Read bytes of entropy.
 * @dev:     A device of the entropy class (emb_drv_entropy_board()).
 * @buf:     Receives @len bytes.
 * @len:     Bytes wanted.
 * @got:     Receives the bytes delivered, also when the read ends early; may be NULL.
 * @timeout: How long the whole read may take, the wait for another thread's read included.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(entropy_bytes) of the source's own time, plus the lock's priority inheritance
 * @owns     none
 * @config   CONFIG_EMB_DRV_ENTROPY
 * @req      SEC-012 DRV-001
 * @since    0.3
 * @stable   no
 *
 * One read at a time per device. A read that times out keeps the bytes it had, in @buf up to
 * @got. A block that fails a health test ends the read with EMB_EHEALTH, and no byte of that
 * block is delivered. What the class held of a block is cleared before it returns.
 *
 * Return: EMB_OK (all @len bytes); EMB_EHEALTH; EMB_ETIMEDOUT; EMB_ESTATE (not READY).
 */
emb_status_t emb_drv_entropy_read(const emb_device_t *dev, void *buf, size_t len, size_t *got,
                                  emb_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_ENTROPY_H */
