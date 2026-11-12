/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The DMA class (SPEC-017 §7, A119): channels a driver's device was given at generation, a job
 * started on one, its end delivered to the driver in the DMA interrupt. Not for applications:
 * a SPI or UART driver uses it for long transfers, and its own request is what an application
 * waits on. */
#ifndef EMB_DRV_DMA_H
#define EMB_DRV_DMA_H

#include <emb/device.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which way a job moves data. */
#define EMB_DRV_DMA_TO_PERIPH   0u /* memory to a peripheral's data register */
#define EMB_DRV_DMA_FROM_PERIPH 1u /* a peripheral's data register to memory */
#define EMB_DRV_DMA_MEM_TO_MEM  2u /* memory to memory (CAP_MEM_TO_MEM) */

/* A job's flags. */
#define EMB_DRV_DMA_FIXED_MEM \
    0x01u /* the memory side stays: a fill, or a sink; memory to memory, the source */

/* Capabilities (emb_device_caps()). */
#define EMB_DRV_DMA_CAP_MEM_TO_MEM 0x01u
#define EMB_DRV_DMA_CAP_CIRCULAR   0x02u

/* A channel a device was given (the generator's dma.<request> field, A119): its DMA
 * controller, the channel or stream, and the request it serves (a fixed controller's channel
 * select, or a DREQ). A request the board gives no channel has dma NULL. */
typedef struct emb_drv_dma_chan {
    const emb_device_t *dma;
    uint8_t channel;
    uint8_t request;
    uint8_t reserved_[2];
} emb_drv_dma_chan_t;

/* A job: from src to dst, count items of width bytes. A peripheral's side is its data
 * register and stays put; the memory side advances unless EMB_DRV_DMA_FIXED_MEM. */
typedef struct emb_drv_dma_job {
    uintptr_t src;
    uintptr_t dst;
    uint32_t count;
    uint8_t dir;   /* EMB_DRV_DMA_* */
    uint8_t width; /* 1, 2 or 4 */
    uint8_t flags; /* EMB_DRV_DMA_FIXED_MEM */
    uint8_t reserved_;
} emb_drv_dma_job_t;

/* A job's end, in the DMA interrupt: EMB_OK with count moved, or EMB_EXFER (a bus error)
 * with what moved before it. */
typedef void (*emb_drv_dma_done_t)(void *arg, emb_status_t status, uint32_t moved);

/* A DMA driver's operations: the device's, then a channel's job started and stopped. start()
 * refuses a channel that is running with EMB_EBUSY and a count above its controller's with
 * EMB_EINVAL; stop() gives the items moved, EMB_ESTATE when the channel was not running, and
 * no end is delivered after it returns. */
typedef struct emb_drv_dma_api {
    emb_device_api_t dev;
    emb_status_t (*start)(const emb_device_t *dev, const emb_drv_dma_chan_t *ch,
                          const emb_drv_dma_job_t *job, emb_drv_dma_done_t done, void *arg);
    emb_status_t (*stop)(const emb_device_t *dev, const emb_drv_dma_chan_t *ch, uint32_t *moved);
} emb_drv_dma_api_t;

/* The memory DMA reaches, which every board defines: the regions its description marks dma
 * (soc_devices.c), or none. */
typedef struct emb_drv_dma_region {
    uintptr_t base;
    size_t size;
} emb_drv_dma_region_t;

extern const emb_drv_dma_region_t emb_board_dma_regions[];
extern const unsigned emb_board_dma_region_count;

/**
 * emb_drv_dma_start() - Start a job on a channel: for drivers.
 * @ch:   The channel the device was given.
 * @job:  What to move; it may be on the caller's stack, as start() copies what it needs.
 * @done: Called in the DMA interrupt when the job ends.
 * @arg:  Passed to @done.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(regions): the memory checked against the board's DMA regions
 * @owns     none
 * @config   CONFIG_EMB_DRV_DMA
 * @req      DRV-004 DRV-006
 * @since    0.3
 * @stable   no
 *
 * The memory a job reads or writes belongs to the hardware until @done runs or
 * emb_drv_dma_stop() returns: the caller must not touch it meanwhile. No cache maintenance is
 * done (A109).
 *
 * Return: EMB_OK; EMB_EINVAL (memory outside the board's DMA regions, or a count the
 * controller cannot take); EMB_ENOTSUP (memory to memory on a controller without it);
 * EMB_EBUSY (the channel is running); EMB_ESTATE (the controller is not READY).
 */
emb_status_t emb_drv_dma_start(const emb_drv_dma_chan_t *ch, const emb_drv_dma_job_t *job,
                               emb_drv_dma_done_t done, void *arg);

/**
 * emb_drv_dma_stop() - Stop a channel's job: for drivers.
 * @ch:    The channel.
 * @moved: Receives the items moved before it stopped; may be NULL.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1); the controller's own stop, a few bus cycles
 * @owns     none
 * @config   CONFIG_EMB_DRV_DMA
 * @req      DRV-006
 * @since    0.3
 * @stable   no
 *
 * The data moved is in memory up to @moved (DRV-006). No @done is called for the job after
 * this returns.
 *
 * Return: EMB_OK; EMB_ESTATE (the channel was not running: its job had ended).
 */
emb_status_t emb_drv_dma_stop(const emb_drv_dma_chan_t *ch, uint32_t *moved);

#ifdef __cplusplus
}
#endif

#endif /* EMB_DRV_DMA_H */
