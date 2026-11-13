/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    device.h
 * @brief   Devices and driver requests (SPEC-016).
 *
 * @details
 * The device instance, its lifecycle, and the request/completion core every driver class is built
 * on.
 *
 * @see     kernel/device/device.c
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_DEVICE_H
#define EMB_DEVICE_H

#include <emb/notify.h>
#include <emb/sem.h>
#include <emb/status.h>
#include <emb/thread.h>
#include <emb/time.h>
#include <emb/types.h>
#if CONFIG_EMB_WORKQ
#include <emb/work.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the device (SPEC-016 §2, §3) ------------------------------------------------ */

/* Lifecycle states (04 §2.2). */
#define EMB_DEVICE_UNINIT       0u
#define EMB_DEVICE_INITIALIZING 1u
#define EMB_DEVICE_READY        2u
#define EMB_DEVICE_SUSPENDED    3u
#define EMB_DEVICE_FAILED       4u

/* Initialization levels (SPEC-016 §4). */
#define EMB_DEVICE_EARLY  0u /* polled, before the kernel */
#define EMB_DEVICE_KERNEL 1u /* after the kernel's objects exist */

/* Driver classes (SPEC-017), in emb_device_t.class_id; 0 is a device of no class. */
#define EMB_DRV_CLASS_NONE     0u
#define EMB_DRV_CLASS_UART     1u
#define EMB_DRV_CLASS_SPI      2u
#define EMB_DRV_CLASS_I2C      3u
#define EMB_DRV_CLASS_DMA      4u
#define EMB_DRV_CLASS_WATCHDOG 5u
#define EMB_DRV_CLASS_FLASH    6u
#define EMB_DRV_CLASS_RTC      7u
#define EMB_DRV_CLASS_ENTROPY  8u
#define EMB_DRV_CLASS_CLOCK    9u
#define EMB_DRV_CLASS_GPIO     10u

struct emb_device;
struct emb_drv_request;

/* What every class's operations table begins with. The core calls these; a class adds its
 * own operations after them. */
typedef struct emb_device_api {
    /* Bring the hardware up; EMB_OK makes the device READY, anything else FAILED. */
    emb_status_t (*init)(const struct emb_device *dev);
    /* Begin the request now at the head of its lane's queue. Returns EMB_EBUSY when the
     * request runs on, and the driver later calls emb_drv_complete() for it, from an
     * interrupt or a thread; or the request's final status, with req->done set, when it
     * finished here. start() never calls emb_drv_complete() for the request it was given,
     * so completion never recurses into it (A115). */
    emb_status_t (*start)(const struct emb_device *dev, struct emb_drv_request *req);
    /* Stop the head request in progress, then complete it (emb_drv_complete) with what it
     * transferred. May be NULL for a device whose requests complete inside start(). */
    void (*stop)(const struct emb_device *dev, struct emb_drv_request *req);
    /* Power transitions; NULL means the device has nothing to do. */
    emb_status_t (*suspend)(const struct emb_device *dev);
    emb_status_t (*resume)(const struct emb_device *dev);
    /* The class's capability bits this driver has (EMB_DRV_<CLASS>_CAP_*, SPEC-016 §11). */
    uint32_t caps;
} emb_device_api_t;

/* A device's request lanes (AMENDMENTS-M4 A114): each a queue of its own, its head started
 * whatever the other lane does, so a full-duplex device (a UART) writes while it reads. A
 * class that needs one lane uses lane 0. */
#define EMB_DRV_LANES 2u

/* RAM: the device's lifecycle and its request queues. Lock domain: the device's critical
 * section (SPEC-016 §6). */
typedef struct emb_device_ram {
    uint8_t lifecycle; /* EMB_DEVICE_* */
    uint8_t power;     /* the device's power state (SPEC-018) */
    uint8_t busy;      /* bit n: lane n's head request has been started */
    uint8_t reserved_;
    int16_t last_status;                         /* what made it FAILED, or EMB_OK */
    struct emb_drv_request *head[EMB_DRV_LANES]; /* each lane's queue, first in first out */
    struct emb_drv_request *tail[EMB_DRV_LANES];
} emb_device_state_t;

/* A device: emitted by the generator (SPEC-016 §8), const, reached by symbol. */
typedef struct emb_device {
    const char *name;                     /* for diagnostics only */
    const emb_device_api_t *api;          /* the class's table begins with this */
    const void *config;                   /* generated, const */
    emb_device_state_t *state;            /* RAM */
    void *data;                           /* the driver's private RAM */
    const struct emb_device *const *deps; /* initialized before this one */
    uint16_t class_id;                    /* EMB_DRV_CLASS_* (SPEC-017) */
    uint8_t level;                        /* EMB_DEVICE_EARLY or EMB_DEVICE_KERNEL */
    uint8_t dep_count;
} emb_device_t;

/* The board's device by its description's name: the generated emb_dev_<name>
 * (soc_devices.h). A device the board does not have is a link error, not a run-time one.
 * MISRA Dev CS-12: rule 20.10, the one token paste that names a generated device. */
#define EMB_DEVICE(name) (&emb_dev_##name)

/* The board's devices in initialization order, EARLY first (SPEC-016 §4): the generated
 * soc_devices.c defines it. emb_kernel_init() initializes the EARLY ones before its
 * initialization table runs, and emb_kernel_start() the KERNEL ones before the first thread
 * does; a device the board support has initialized already is left as it is. Every board
 * defines it: emb-hwgen emits it for a described board, empty when the board has no devices,
 * and a hand-written board support defines {NULL, 0, 0}. */
typedef struct emb_device_table {
    const emb_device_t *const *devices;
    uint16_t count;
    uint16_t early; /* the first `early` devices are EMB_DEVICE_EARLY */
} emb_device_table_t;

extern const emb_device_table_t emb_board_device_table;

/**
 * emb_device_caps() - The capabilities a device's driver has.
 * @dev: The device.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-002
 * @since    0.3
 * @stable   no
 *
 * An operation that needs a capability the device lacks returns EMB_ENOTSUP; nothing is
 * emulated in its place (SPEC-016 §11).
 *
 * Return: the class's EMB_DRV_<CLASS>_CAP_* bits; 0 for a NULL @dev.
 */
uint32_t emb_device_caps(const emb_device_t *dev);

/* ---- requests and completion (SPEC-016 §5) ---------------------------------------- */

/* Completion targets (DRV-005). */
#define EMB_DRV_TARGET_POLL     0u
#define EMB_DRV_TARGET_NOTIFY   1u
#define EMB_DRV_TARGET_SEM      2u
#define EMB_DRV_TARGET_WORK     3u
#define EMB_DRV_TARGET_CALLBACK 4u

typedef void (*emb_drv_callback_t)(struct emb_drv_request *req, void *arg);

/* The common head of every class's request. The caller owns its memory until it completes;
 * the core and the driver own its fields in between. */
typedef struct emb_drv_request {
    struct emb_drv_request *next; /* the device's queue */
    emb_status_t status;          /* EMB_EBUSY while queued or in progress, then the result */
    uint32_t done;                /* bytes or items completed, also on error and cancel */
    uint8_t target;               /* EMB_DRV_TARGET_* */
    uint8_t phase;                /* the core's: idle, queued, started, stopping, done */
    uint8_t lane;                 /* the device's lane it queues on: 0, or 1 (A114) */
    emb_notify_bits_t bits;       /* EMB_DRV_TARGET_NOTIFY */
    emb_thread_t thread;          /* EMB_DRV_TARGET_NOTIFY */
    emb_sem_t sem;                /* EMB_DRV_TARGET_SEM */
#if CONFIG_EMB_WORKQ
    emb_workq_t workq; /* EMB_DRV_TARGET_WORK */
    emb_work_t *work;
#endif
    emb_drv_callback_t callback; /* EMB_DRV_TARGET_CALLBACK */
    void *arg;
} emb_drv_request_t;

/**
 * emb_device_init() - Initialize one device, after the devices it depends on.
 * @dev: The device; its dependencies must already have been initialized.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(dependencies) plus the driver's init
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-002 DRV-003
 * @since    0.3
 * @stable   no
 *
 * Moves the device from UNINIT through INITIALIZING to READY, or to FAILED with the cause in
 * its last status. A dependency that is not READY fails the device with EMB_ENOENT without
 * calling its driver, and leaves every other device alone.
 *
 * Return: EMB_OK (READY); the driver's error (FAILED); EMB_ENOENT (a dependency is not
 * READY); EMB_EEXIST (not UNINIT); EMB_EINVAL.
 */
emb_status_t emb_device_init(const emb_device_t *dev);

/**
 * emb_device_init_list() - Initialize devices in the given order.
 * @devs: The devices, each after the ones it depends on (the generator's order).
 * @count: How many.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(count) plus the drivers' init
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-001 DRV-003
 * @since    0.3
 * @stable   no
 *
 * A device that fails does not stop the others; one that depends on it fails too.
 *
 * Return: EMB_OK when every device is READY; EMB_EIO when one or more FAILED; EMB_EINVAL.
 */
emb_status_t emb_device_init_list(const emb_device_t *const *devs, size_t count);

/**
 * emb_device_state() - A device's lifecycle state.
 * @dev: The device.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-002
 * @since    0.3
 * @stable   no
 *
 * Return: one of EMB_DEVICE_UNINIT, _INITIALIZING, _READY, _SUSPENDED, _FAILED.
 */
uint8_t emb_device_state(const emb_device_t *dev);

/**
 * emb_device_ready() - Whether a device accepts requests.
 * @dev: The device.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-002
 * @since    0.3
 * @stable   no
 *
 * Return: true when READY.
 */
bool emb_device_ready(const emb_device_t *dev);

/**
 * emb_device_status() - Why a device FAILED.
 * @dev: The device.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-002 DRV-003
 * @since    0.3
 * @stable   no
 *
 * Return: the status that moved the device to FAILED, or EMB_OK.
 */
emb_status_t emb_device_status(const emb_device_t *dev);

/**
 * emb_device_suspend() - Suspend a READY device.
 * @dev: The device.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1) plus the driver's suspend
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-002 PWR-002
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK (SUSPENDED); EMB_EBUSY (a request is queued or in progress); EMB_ESTATE
 * (not READY); the driver's error, which leaves the device READY.
 */
emb_status_t emb_device_suspend(const emb_device_t *dev);

/**
 * emb_device_resume() - Resume a SUSPENDED device.
 * @dev: The device.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1) plus the driver's resume
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-002 PWR-002
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK (READY); EMB_ESTATE (not SUSPENDED); the driver's error (FAILED).
 */
emb_status_t emb_device_resume(const emb_device_t *dev);

/**
 * emb_drv_request_init() - Prepare a request with no completion target (polled).
 * @req: The request's common head.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 */
void emb_drv_request_init(emb_drv_request_t *req);

/**
 * emb_drv_on_notify() - Complete a request by setting notification bits on a thread.
 * @req: The request, prepared by emb_drv_request_init().
 * @thread: The thread to notify.
 * @bits: The bits to set.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 */
void emb_drv_on_notify(emb_drv_request_t *req, emb_thread_t thread, emb_notify_bits_t bits);

/**
 * emb_drv_on_sem() - Complete a request by giving a semaphore.
 * @req: The request, prepared by emb_drv_request_init().
 * @sem: The semaphore to give.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 */
void emb_drv_on_sem(emb_drv_request_t *req, emb_sem_t sem);

#if CONFIG_EMB_WORKQ
/**
 * emb_drv_on_work() - Complete a request by submitting a work item.
 * @req: The request, prepared by emb_drv_request_init().
 * @workq: The queue to submit to.
 * @work: The item to submit.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE CONFIG_EMB_WORKQ
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 */
void emb_drv_on_work(emb_drv_request_t *req, emb_workq_t workq, emb_work_t *work);
#endif

/**
 * emb_drv_on_callback() - Complete a request by calling a function in the completing context.
 * @req: The request, prepared by emb_drv_request_init().
 * @fn: Called once, possibly from an interrupt; it must be bounded and must not block.
 * @arg: Passed to @fn.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-005
 * @since    0.3
 * @stable   no
 */
void emb_drv_on_callback(emb_drv_request_t *req, emb_drv_callback_t fn, void *arg);

/**
 * emb_drv_submit() - Queue a request on a device; it starts when the requests before it end.
 * @dev: The device.
 * @req: The request, its target set; owned by the driver until it completes.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus the driver's start when the queue was empty
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-003 DRV-004
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK (queued; the request reports its result when it completes); EMB_ESTATE (the
 * device is not READY); EMB_EIO (FAILED); EMB_EBUSY (the request is already queued);
 * EMB_EINVAL.
 */
emb_status_t emb_drv_submit(const emb_device_t *dev, emb_drv_request_t *req);

/**
 * emb_drv_complete() - End the device's head request (called by its driver).
 * @dev: The device.
 * @req: The request at the head of its queue.
 * @status: The result.
 * @done: Bytes or items transferred.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1) plus the next request's start
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-004 DRV-005 DRV-006
 * @since    0.3
 * @stable   no
 *
 * Delivers the request's completion target and starts the next queued request. A request
 * completes once: completing one that already completed does nothing.
 */
void emb_drv_complete(const emb_device_t *dev, emb_drv_request_t *req, emb_status_t status,
                      uint32_t done);

/**
 * emb_drv_cancel() - Cancel a queued or running request.
 * @dev: The device.
 * @req: The request.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(queued requests) plus the driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-006
 * @since    0.3
 * @stable   no
 *
 * A queued request completes at once with EMB_ECANCELED. A running one is stopped by its
 * driver, which completes it with EMB_ECANCELED and what it transferred.
 *
 * Return: EMB_OK; EMB_ESTATE (the request is not queued or running); EMB_EINVAL.
 */
emb_status_t emb_drv_cancel(const emb_device_t *dev, emb_drv_request_t *req);

/**
 * emb_drv_wait() - Submit a request and wait for it: the blocking classes' wrapper.
 * @dev: The device.
 * @req: The request; its completion target is replaced.
 * @timeout: EMB_NO_WAIT, EMB_WAIT_FOREVER or EMB_TIMEOUT(duration).
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) plus the request's own time; bounded by the timeout and the driver's stop
 * @owns     none
 * @config   CONFIG_EMB_DEVICE
 * @req      DRV-004 DRV-006
 * @since    0.3
 * @stable   no
 *
 * Waits on a binary semaphore in the caller's stack frame. On timeout, cancels the request
 * and waits for it to end, which the driver's stop bounds; the request's done field then tells
 * what was transferred.
 *
 * Return: the request's status; EMB_ETIMEDOUT when the timeout ended it; the statuses of
 * emb_drv_submit(); EMB_EPERM (not a thread).
 */
emb_status_t emb_drv_wait(const emb_device_t *dev, emb_drv_request_t *req, emb_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DEVICE_H */
