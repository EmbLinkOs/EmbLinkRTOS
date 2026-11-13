/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    stm32_dma.c
 * @brief   The STM32F4 DMA controller (RM0390 Rev 6 §9) as a DMA class device (SPEC-017 §7, A119).
 *
 * @details
 * A job per stream, its request picked by the channel select the generator gave it, its end by the
 * stream's interrupt. A peripheral's job runs in direct mode; memory to memory, which only DMA2
 * does, through the stream's FIFO.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_DMA
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define DMA_LISR   0x00u /* streams 0 to 3; HISR (0x04) 4 to 7 */
#define DMA_LIFCR  0x08u /* streams 0 to 3; HIFCR (0x0C) 4 to 7 */
#define DMA_SXCR   0x10u /* stream x's at 0x10 + 0x18 x */
#define DMA_SXSTEP 0x18u
#define SXCR       0x00u
#define SXNDTR     0x04u
#define SXPAR      0x08u
#define SXM0AR     0x0Cu
#define SXFCR      0x14u

#define CR_EN        (1u << 0)
#define CR_DMEIE     (1u << 1)
#define CR_HTIE      ((uint32_t)1u << 3)
#define CR_TEIE      (1u << 2)
#define CR_TCIE      (1u << 4)
#define CR_DIR_M2P   (1u << 6)
#define CR_DIR_M2M   (2u << 6)
#define CR_CIRC      ((uint32_t)1u << 8)
#define CR_PINC      (1u << 9)
#define CR_MINC      (1u << 10)
#define CR_PSIZE     11u
#define CR_MSIZE     13u
#define CR_PL_HIGH   (2u << 16)
#define CR_CHSEL     25u
#define FCR_DMDIS    (1u << 2) /* the FIFO, which memory to memory needs */
#define FCR_FTH_FULL 3u

/* A stream's flags, in its xISR and xIFCR: at bits 0, 6, 16 and 22 within its half. */
#define FLAG_FE   0x01u
#define FLAG_DME  0x04u
#define FLAG_TE   0x08u
#define FLAG_HT   0x10u
#define FLAG_TC   0x20u
#define FLAG_ALL  0x3Du
#define STOP_POLL 100000u /* bounded: EN clears once the current beat retires */
#define MAX_COUNT 65535u  /* NDTR */

static const emb_stm32_dma_config_t *stm32_dma_config(const emb_device_t *dev)
{
    return (const emb_stm32_dma_config_t *)dev->config;
}

static emb_stm32_dma_data_t *stm32_dma_data(const emb_device_t *dev)
{
    return (emb_stm32_dma_data_t *)dev->data;
}

static uintptr_t stm32_dma_stream(uintptr_t base, uint32_t s)
{
    return base + DMA_SXCR + (DMA_SXSTEP * s);
}

/* Where stream s's flags are: its half's status register's offset, and the shift in it. */
static uint32_t stm32_dma_shift(uint32_t s)
{
    static const uint8_t shifts[4] = {0u, 6u, 16u, 22u};
    return shifts[s & 3u];
}

static uint32_t stm32_dma_flags(uintptr_t base, uint32_t s)
{
    return (REG32(base + DMA_LISR + ((s >> 2) * 4u)) >> stm32_dma_shift(s)) & FLAG_ALL;
}

static void stm32_dma_clear(uintptr_t base, uint32_t s)
{
    REG32(base + DMA_LIFCR + ((s >> 2) * 4u)) = FLAG_ALL << stm32_dma_shift(s);
}

/* Off, and waits until the stream is: bounded by STOP_POLL. */
static void stm32_dma_disable(uintptr_t stream)
{
    uint32_t n;
    REG32(stream + SXCR) &= ~(CR_EN | CR_TCIE | CR_TEIE | CR_DMEIE);
    for (n = 0u; n < STOP_POLL; n++) {
        if ((REG32(stream + SXCR) & CR_EN) == 0u) {
            break;
        }
    }
}

static void stm32_dma_isr(void *arg)
{
    emb_stm32_dma_stream_t *st = (emb_stm32_dma_stream_t *)arg;
    uintptr_t base = stm32_dma_config(st->dma->dev)->base;
    uintptr_t stream = stm32_dma_stream(base, st->index);
    uint32_t flags = stm32_dma_flags(base, st->index);
    emb_status_t status;
    uint32_t moved;
    stm32_dma_clear(base, st->index);
    if ((st->running == 0u) || ((flags & (FLAG_TC | FLAG_HT | FLAG_TE | FLAG_DME)) == 0u)) {
        return; /* a flag of a job already stopped, or a FIFO note in direct mode */
    }
    if ((st->circular == 0u) && ((flags & (FLAG_TC | FLAG_TE | FLAG_DME)) == 0u)) {
        return; /* a half, which only a circular job reports */
    }
    if ((st->circular != 0u) && ((flags & (FLAG_TE | FLAG_DME)) == 0u)) {
        /* a circular job goes on: each half's end, in order, the first before the second */
        if ((flags & FLAG_HT) != 0u) {
            st->done(st->arg, EMB_OK, st->count / 2u);
        }
        if ((flags & FLAG_TC) != 0u) {
            st->done(st->arg, EMB_OK, st->count);
        }
        return;
    }
    status = ((flags & (FLAG_TE | FLAG_DME)) != 0u) ? EMB_EXFER : EMB_OK;
    moved = st->count - REG32(stream + SXNDTR);
    stm32_dma_disable(stream);
    st->running = 0u;
    st->done(st->arg, status, moved);
}

static emb_status_t stm32_dma_init(const emb_device_t *dev)
{
    const emb_stm32_dma_config_t *cfg = stm32_dma_config(dev);
    emb_stm32_dma_data_t *d = stm32_dma_data(dev);
    const uint32_t irqs[EMB_STM32_DMA_STREAMS] = {cfg->irq0, cfg->irq1, cfg->irq2, cfg->irq3,
                                                  cfg->irq4, cfg->irq5, cfg->irq6, cfg->irq7};
    uint32_t s;
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit);
    d->dev = dev;
    for (s = 0u; s < EMB_STM32_DMA_STREAMS; s++) { /* bounded: eight streams */
        emb_stm32_dma_stream_t *st = &d->streams[s];
        st->dma = d;
        st->index = (uint8_t)s;
        st->running = 0u;
        stm32_dma_disable(stm32_dma_stream(cfg->base, s));
        stm32_dma_clear(cfg->base, s);
        if ((emb_irq_connect((emb_irq_t)irqs[s], stm32_dma_isr, st) != EMB_OK) ||
            (emb_irq_enable((emb_irq_t)irqs[s]) != EMB_OK)) {
            return EMB_EINVAL;
        }
    }
    return EMB_OK;
}

static uint32_t stm32_dma_size(uint32_t width)
{
    return (width == 4u) ? 2u : ((width == 2u) ? 1u : 0u);
}

static emb_status_t stm32_dma_start(const emb_device_t *dev, const emb_drv_dma_chan_t *ch,
                                    const emb_drv_dma_job_t *job, emb_drv_dma_done_t done,
                                    void *arg)
{
    const emb_stm32_dma_config_t *cfg = stm32_dma_config(dev);
    emb_stm32_dma_data_t *d = stm32_dma_data(dev);
    emb_stm32_dma_stream_t *st;
    uintptr_t stream;
    uint32_t cr;
    uint32_t size = stm32_dma_size(job->width);
    emb_irq_key_t key;
    if ((ch->channel >= EMB_STM32_DMA_STREAMS) || (job->count > MAX_COUNT)) {
        return EMB_EINVAL;
    }
    if ((job->dir == EMB_DRV_DMA_MEM_TO_MEM) && !cfg->memory) {
        return EMB_ENOTSUP; /* DMA1 does not copy memory to memory */
    }
    st = &d->streams[ch->channel];
    stream = stm32_dma_stream(cfg->base, ch->channel);
    key = emb_irq_lock();
    if (st->running != 0u) {
        emb_irq_unlock(key);
        return EMB_EBUSY;
    }
    cr = ((uint32_t)(ch->request & 7u) << CR_CHSEL) | CR_PL_HIGH | (size << CR_PSIZE) |
         (size << CR_MSIZE) | CR_TCIE | CR_TEIE | CR_DMEIE;
    if (job->dir == EMB_DRV_DMA_MEM_TO_MEM) {
        /* a fill keeps its source still (PAR, PINC); the destination always advances */
        cr |= CR_DIR_M2M | CR_MINC | (((job->flags & EMB_DRV_DMA_FIXED_MEM) == 0u) ? CR_PINC : 0u);
        REG32(stream + SXPAR) = (uint32_t)job->src; /* the source is PAR in this mode */
        REG32(stream + SXM0AR) = (uint32_t)job->dst;
        REG32(stream + SXFCR) = FCR_DMDIS | FCR_FTH_FULL;
    } else {
        bool to_periph = job->dir == EMB_DRV_DMA_TO_PERIPH;
        cr |= (to_periph ? CR_DIR_M2P : 0u) |
              (((job->flags & EMB_DRV_DMA_FIXED_MEM) == 0u) ? CR_MINC : 0u);
        REG32(stream + SXPAR) = (uint32_t)(to_periph ? job->dst : job->src);
        REG32(stream + SXM0AR) = (uint32_t)(to_periph ? job->src : job->dst);
        REG32(stream + SXFCR) = 0u; /* direct mode */
    }
    if ((job->flags & EMB_DRV_DMA_CIRCULAR) != 0u) {
        cr |= CR_CIRC | CR_HTIE; /* NDTR reloads at the end, and each half interrupts */
    }
    REG32(stream + SXNDTR) = job->count;
    stm32_dma_clear(cfg->base, ch->channel);
    st->circular = ((job->flags & EMB_DRV_DMA_CIRCULAR) != 0u) ? 1u : 0u;
    st->done = done;
    st->arg = arg;
    st->count = job->count;
    st->running = 1u;
    REG32(stream + SXCR) = cr;
    REG32(stream + SXCR) = cr | CR_EN;
    emb_irq_unlock(key);
    return EMB_OK;
}

static emb_status_t stm32_dma_stop(const emb_device_t *dev, const emb_drv_dma_chan_t *ch,
                                   uint32_t *moved)
{
    const emb_stm32_dma_config_t *cfg = stm32_dma_config(dev);
    emb_stm32_dma_data_t *d = stm32_dma_data(dev);
    emb_stm32_dma_stream_t *st;
    uintptr_t stream;
    emb_irq_key_t key;
    if (ch->channel >= EMB_STM32_DMA_STREAMS) {
        return EMB_EINVAL;
    }
    st = &d->streams[ch->channel];
    stream = stm32_dma_stream(cfg->base, ch->channel);
    key = emb_irq_lock();
    if (st->running == 0u) {
        emb_irq_unlock(key);
        return EMB_ESTATE;
    }
    stm32_dma_disable(stream); /* NDTR then holds what was left */
    *moved = st->count - REG32(stream + SXNDTR);
    stm32_dma_clear(cfg->base, ch->channel);
    st->running = 0u;
    emb_irq_unlock(key);
    return EMB_OK;
}

const emb_drv_dma_api_t emb_stm32_dma_api = {
    {stm32_dma_init, NULL, NULL, NULL, NULL, EMB_DRV_DMA_CAP_MEM_TO_MEM | EMB_DRV_DMA_CAP_CIRCULAR},
    stm32_dma_start,
    stm32_dma_stop,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_dma_off_t;
#endif
