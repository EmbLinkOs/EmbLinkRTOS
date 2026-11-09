/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The I2C class (SPEC-017 §6): as a controller, transactions of write and read segments to a
 * target's address, joined by repeated starts and ended by a stop; as a target, the
 * transactions another controller makes to this one's own address. */
#ifndef EMB_DRV_I2C_H
#define EMB_DRV_I2C_H

#include <emb/device.h>
#include <emb/drv_bus.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The lanes (A114): transactions this controller makes, and those it answers. */
#define EMB_DRV_I2C_LANE_CONTROLLER 0u
#define EMB_DRV_I2C_LANE_TARGET     1u

/* The bus speeds a target names (its clock, at most). */
#define EMB_DRV_I2C_STANDARD  100000u  /* standard mode */
#define EMB_DRV_I2C_FAST      400000u  /* fast mode (CAP_FAST_MODE) */
#define EMB_DRV_I2C_FAST_PLUS 1000000u /* fast mode plus (CAP_FAST_MODE_PLUS) */

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_I2C_CAP_TARGET         0x01u
#define EMB_DRV_I2C_CAP_FAST_MODE      0x02u
#define EMB_DRV_I2C_CAP_FAST_MODE_PLUS 0x04u
#define EMB_DRV_I2C_CAP_TARGET_NACK    0x08u /* a target refuses bytes beyond its buffer */

/* What a target-mode request saw (emb_drv_i2c_req_t.op after it completes). */
#define EMB_DRV_I2C_OP_TRANSFER 0u /* a controller's transaction */
#define EMB_DRV_I2C_OP_LISTEN   1u /* a target's: answered one transaction */

/* A device on the bus, as this controller addresses it. */
typedef struct emb_drv_i2c_target {
    uint32_t hz;      /* the clock, at most: EMB_DRV_I2C_STANDARD, _FAST or _FAST_PLUS */
    uint16_t address; /* 7 bits, 0x08 to 0x77: the reserved ones are refused */
    uint8_t reserved_[2];
} emb_drv_i2c_target_t;

/* One segment of a transaction: a write (wr) or a read (rd), never both. */
typedef struct emb_drv_i2c_seg {
    const uint8_t *wr;
    uint8_t *rd;
    size_t len; /* at least 1 */
} emb_drv_i2c_seg_t;

/* An I2C request: the core's request first. The caller owns it, its target, segments and
 * buffers until it completes. For a transaction req.done counts the bytes moved, segment
 * after segment; for a listen, the bytes received, and sent those sent. */
typedef struct emb_drv_i2c_req {
    emb_drv_request_t req;
    /* a transaction */
    const emb_drv_i2c_target_t *target;
    const emb_drv_i2c_seg_t *segs;
    size_t count;
    /* a listen */
    uint8_t *rx; /* what the controller writes, up to rx_len; more is dropped (see listen) */
    size_t rx_len;
    const uint8_t *tx; /* what it reads, up to tx_len; then 0xFF */
    size_t tx_len;
    size_t sent;
    uint16_t own; /* the address answered */
    uint8_t op;   /* EMB_DRV_I2C_OP_* */
    uint8_t reserved_;
} emb_drv_i2c_req_t;

/* The I2C request a core request belongs to, in a driver's start() and stop(). MISRA Dev
 * CS-12: rule 11.3, the request's first member. */
#define EMB_DRV_I2C_REQ(r) ((emb_drv_i2c_req_t *)(void *)(r))

/**
 * emb_drv_i2c_bus_init() - Set up the class's part of a controller's RAM: for drivers.
 * @dev: A device of the I2C class whose data begins with an emb_drv_bus_t (<emb/drv_bus.h>).
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1); O(active mutexes) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     mutex
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * The driver's initialization calls it, at the KERNEL level, before any transaction.
 *
 * Return: EMB_OK; EMB_EINVAL (not an I2C controller with RAM); the statuses of
 * emb_mutex_init().
 */
emb_status_t emb_drv_i2c_bus_init(const emb_device_t *dev);

/**
 * emb_drv_i2c_req_transfer() - Prepare a transaction.
 * @req:    The request.
 * @target: The device addressed, at its speed; it must outlive the request.
 * @segs:   The segments, in order: a write then a read is a register read.
 * @count:  How many; at least 1.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * The request is reset with its completion target polled. A NULL @req is ignored.
 */
void emb_drv_i2c_req_transfer(emb_drv_i2c_req_t *req, const emb_drv_i2c_target_t *target,
                              const emb_drv_i2c_seg_t *segs, size_t count);

/**
 * emb_drv_i2c_req_listen() - Prepare to answer one transaction as a target.
 * @req:    The request.
 * @own:    The address answered, 0x08 to 0x77.
 * @rx:     Where what the controller writes goes, or NULL.
 * @rx_len: Its size; bytes beyond it are not stored.
 * @tx:     What the controller reads, or NULL.
 * @tx_len: Its size; reads beyond it get 0xFF.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * The request runs on the target lane: it waits for a controller to address @own, and
 * completes at that transaction's stop, a write then a read joined by a repeated start
 * included. While no listen is waiting the address is not acknowledged. Bytes written beyond
 * @rx_len are refused (not acknowledged) by a controller with EMB_DRV_I2C_CAP_TARGET_NACK;
 * one without acknowledges and drops them. A NULL @req is ignored.
 */
void emb_drv_i2c_req_listen(emb_drv_i2c_req_t *req, uint16_t own, uint8_t *rx, size_t rx_len,
                            const uint8_t *tx, size_t tx_len);

/**
 * emb_drv_i2c_submit() - Queue a transaction or a listen.
 * @dev: A device of the I2C class.
 * @req: A request prepared by emb_drv_i2c_req_transfer() or emb_drv_i2c_req_listen().
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(segments) plus the driver's start when its lane was idle
 * @owns     none
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004 DRV-005
 * @since    0.3
 * @stable   no
 *
 * One transaction at a time; one listen at a time, beside it. A speed or a target mode the
 * controller lacks completes with EMB_ENOTSUP. While a thread holds the bus for one target, a
 * transaction to another is refused.
 *
 * Return: EMB_OK; the statuses of emb_drv_submit(); EMB_EBUSY (the bus is held for another
 * target); EMB_EINVAL (not an I2C controller, a reserved address, a speed of 0, or a segment
 * with no buffer, both, or no length).
 */
emb_status_t emb_drv_i2c_submit(const emb_device_t *dev, emb_drv_i2c_req_t *req);

/**
 * emb_drv_i2c_transfer() - Run a transaction and wait until it ends.
 * @dev:     A device of the I2C class.
 * @target:  The device addressed.
 * @segs:    The segments.
 * @count:   How many.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 * @done:    Receives the bytes moved, also on error and timeout; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(bytes) on the bus after any wait for it; bounded by the timeout and the driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004 DRV-006
 * @since    0.3
 * @stable   no
 *
 * The blocking wrapper of SPEC-016 §5.2. It waits for a bus another thread holds, within
 * @timeout; the caller's own hold, for the same target, lets it through. A canceled or
 * timed-out transaction ends with a stop condition after the byte in progress.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_ENACK (the address or a byte not acknowledged);
 * EMB_EARBLOST (another controller won the bus); EMB_EBUSERR (a misplaced start or stop);
 * EMB_ENOTSUP; EMB_EBUSY (the caller holds the bus for another target); the statuses of
 * emb_drv_i2c_submit().
 */
emb_status_t emb_drv_i2c_transfer(const emb_device_t *dev, const emb_drv_i2c_target_t *target,
                                  const emb_drv_i2c_seg_t *segs, size_t count,
                                  emb_timeout_t timeout, size_t *done);

/**
 * emb_drv_i2c_write_read() - Write, then read after a repeated start: a register read.
 * @dev:     A device of the I2C class.
 * @target:  The device addressed.
 * @wr:      What to write (a register's address), or NULL to only read.
 * @wr_len:  Its length; 0 with a NULL @wr.
 * @rd:      Where what is read goes, or NULL to only write.
 * @rd_len:  Its length; 0 with a NULL @rd.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(bytes) on the bus after any wait for it; bounded by the timeout and the driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * Return: the statuses of emb_drv_i2c_transfer(); EMB_EINVAL (neither part).
 */
emb_status_t emb_drv_i2c_write_read(const emb_device_t *dev, const emb_drv_i2c_target_t *target,
                                    const void *wr, size_t wr_len, void *rd, size_t rd_len,
                                    emb_timeout_t timeout);

/**
 * emb_drv_i2c_listen() - Answer one transaction as a target, and wait until it ends.
 * @dev:      A device of the I2C class with the target capability.
 * @own:      The address answered.
 * @rx:       Where what the controller writes goes, or NULL.
 * @rx_len:   Its size.
 * @tx:       What the controller reads, or NULL.
 * @tx_len:   Its size.
 * @timeout:  EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 * @received: Receives the bytes written to this target; may be NULL.
 * @sent:     Receives the bytes read from it; may be NULL.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(bytes) of the bus's time; bounded by the timeout and the driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004 DRV-006
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ETIMEDOUT (no controller addressed it in time); EMB_EBUSERR;
 * EMB_ENOTSUP (no target mode); the statuses of emb_drv_i2c_submit().
 */
emb_status_t emb_drv_i2c_listen(const emb_device_t *dev, uint16_t own, void *rx, size_t rx_len,
                                const void *tx, size_t tx_len, emb_timeout_t timeout,
                                size_t *received, size_t *sent);

/**
 * emb_drv_i2c_acquire() - Hold the bus for one target across transactions.
 * @dev:     A device of the I2C class.
 * @target:  The target.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) plus the mutex's priority inheritance
 * @owns     mutex
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * For a target that needs several transactions in a row, nobody else's between: a write then
 * a poll until it is done. Other threads' transactions wait until it is released; the
 * holder's run. The holder may acquire again for the same target; each acquire needs its
 * release. Listens are not affected.
 *
 * Return: EMB_OK; EMB_ETIMEDOUT; EMB_EBUSY (the caller holds the bus for another target);
 * EMB_EINVAL; the statuses of emb_mutex_lock().
 */
emb_status_t emb_drv_i2c_acquire(const emb_device_t *dev, const emb_drv_i2c_target_t *target,
                                 emb_timeout_t timeout);

/**
 * emb_drv_i2c_release() - Release the bus emb_drv_i2c_acquire() held.
 * @dev: A device of the I2C class.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1) plus the mutex's priority inheritance
 * @owns     mutex
 * @config   CONFIG_EMB_DRV_I2C
 * @req      DRV-004
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EPERM (the caller does not hold the bus); EMB_EINVAL.
 */
emb_status_t emb_drv_i2c_release(const emb_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_I2C_H */
