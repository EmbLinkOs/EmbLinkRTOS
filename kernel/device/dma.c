/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    dma.c
 * @brief   The DMA class (SPEC-017 §7, A119): its checks over a driver's channel operations.
 *
 * @details
 * Channels run at once, each its own, so a job does not go through the device's request queue.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_dma.h>

#include <embk/kernel.h>

static const emb_drv_dma_api_t *dma_api(const emb_device_t *dev)
{
    if ((dev == NULL) || (dev->api == NULL) || (dev->class_id != EMB_DRV_CLASS_DMA)) {
        return NULL;
    }
    /* MISRA Dev CS-12: rule 11.3, the class's operations begin with the device's */
    return (const emb_drv_dma_api_t *)(const void *)dev->api;
}

/* [addr, addr + bytes) inside one of the board's DMA regions. */
static bool dma_reaches(uintptr_t addr, uint32_t bytes)
{
    unsigned i;
    for (i = 0u; i < emb_board_dma_region_count; i++) { /* bounded by the board's regions */
        const emb_drv_dma_region_t *g = &emb_board_dma_regions[i];
        if ((addr >= g->base) && (bytes <= g->size) && ((addr - g->base) <= (g->size - bytes))) {
            return true;
        }
    }
    return false;
}

static bool job_ok(const emb_drv_dma_job_t *job)
{
    return (job != NULL) && (job->dir <= EMB_DRV_DMA_MEM_TO_MEM) &&
           ((job->width == 1u) || (job->width == 2u) || (job->width == 4u)) && (job->count != 0u);
}

/* The memory a job reads or writes, each side that is memory, inside the DMA regions. */
static bool memory_ok(const emb_drv_dma_job_t *job)
{
    uint32_t span = job->width;
    if ((job->flags & EMB_DRV_DMA_FIXED_MEM) == 0u) {
        if (job->count > (UINT32_MAX / job->width)) {
            return false;
        }
        span = job->count * job->width;
    }
    if ((job->dir != EMB_DRV_DMA_TO_PERIPH) && !dma_reaches(job->dst, span)) {
        return false; /* where it writes */
    }
    if ((job->dir != EMB_DRV_DMA_FROM_PERIPH) && !dma_reaches(job->src, span)) {
        return false; /* where it reads */
    }
    return true;
}

emb_status_t emb_drv_dma_start(const emb_drv_dma_chan_t *ch, const emb_drv_dma_job_t *job,
                               emb_drv_dma_done_t done, void *arg)
{
    const emb_drv_dma_api_t *api = (ch != NULL) ? dma_api(ch->dma) : NULL;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_DMA_START, (api != NULL) && (api->start != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_DMA_START, job_ok(job) && (done != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    if (!emb_device_ready(ch->dma)) {
        return EMB_ESTATE;
    }
    if ((job->dir == EMB_DRV_DMA_MEM_TO_MEM) &&
        ((emb_device_caps(ch->dma) & EMB_DRV_DMA_CAP_MEM_TO_MEM) == 0u)) {
        return EMB_ENOTSUP;
    }
    if (!memory_ok(job)) {
        return EMB_EINVAL; /* the hardware cannot reach it, or must not (SPEC-017 §7) */
    }
    return api->start(ch->dma, ch, job, done, arg);
}

emb_status_t emb_drv_dma_stop(const emb_drv_dma_chan_t *ch, uint32_t *moved)
{
    const emb_drv_dma_api_t *api = (ch != NULL) ? dma_api(ch->dma) : NULL;
    uint32_t n = 0u;
    emb_status_t st;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_DMA_STOP, (api != NULL) && (api->stop != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    st = api->stop(ch->dma, ch, &n);
    if (moved != NULL) {
        *moved = n;
    }
    return st;
}
