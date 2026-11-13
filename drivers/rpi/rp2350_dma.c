/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    rp2350_dma.c
 * @brief   The RP2350's DMA controller (datasheet §12.6; register offsets and bits as pico-sdk
 *          2.1.1's hardware/regs/dma.h names them) as a DMA class device (SPEC-017 §7, A119).
 *
 * @details
 * A job per channel, paced by the DREQ the generator gave it, or always ready for memory to memory;
 * its end on DMA_IRQ_0.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_rp2350_devices.h>

#if CONFIG_EMB_DRV_DMA
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define CH_STEP     0x40u /* channel n's registers at 0x40 n */
#define READ_ADDR   0x00u
#define WRITE_ADDR  0x04u
#define TRANS_COUNT 0x08u
#define CTRL_TRIG   0x0Cu
#define INTE0       0x404u
#define INTS0       0x40Cu
#define CHAN_ABORT  0x464u

#define CTRL_EN           (1u << 0)
#define CTRL_DATA_SIZE    2u
#define CTRL_INCR_READ    (1u << 4)
#define CTRL_INCR_WRITE   (1u << 6)
#define CTRL_RING_SIZE    8u         /* the ring: 2^n bytes, its address's low n bits */
#define CTRL_RING_WRITE   (1u << 12) /* RING_SEL: the ring is the write address's */
#define CTRL_CHAIN_TO     13u
#define CTRL_TREQ_SEL     17u
#define CTRL_BUSY         (1u << 26)
#define CTRL_WRITE_ERROR  (1u << 29)
#define CTRL_READ_ERROR   (1u << 30)
#define CTRL_AHB_ERROR    (1u << 31)
#define COUNT_MASK        ((1u << 28) - 1u)    /* TRANS_COUNT's count, its low 28 bits */
#define MODE_TRIGGER_SELF ((uint32_t)1u << 28) /* TRANS_COUNT's mode: it triggers itself again */
#define RING_MAX          15u                  /* 32 KiB */
#define TREQ_FORCE        63u                  /* always ready: memory to memory */
#define ABORT_POLL        100000u              /* bounded: the transfers in flight retire */

static const emb_rp2350_dma_config_t *rp2350_dma_config(const emb_device_t *dev)
{
    return (const emb_rp2350_dma_config_t *)dev->config;
}

static emb_rp2350_dma_data_t *rp2350_dma_data(const emb_device_t *dev)
{
    return (emb_rp2350_dma_data_t *)dev->data;
}

#define SIDE_NONE  0u
#define SIDE_READ  1u
#define SIDE_WRITE 2u

/* The items a channel moved: by how far its advancing address got, which an abort leaves as
 * it was, within its ring for a circular job; by its count when neither side advances. */
static uint32_t rp2350_dma_moved(uintptr_t r, const emb_rp2350_dma_channel_t *ch)
{
    if (ch->side == SIDE_WRITE) {
        return ((REG32(r + WRITE_ADDR) - ch->start) / ch->width) % ch->count;
    }
    if (ch->side == SIDE_READ) {
        return ((REG32(r + READ_ADDR) - ch->start) / ch->width) % ch->count;
    }
    return ch->count - (REG32(r + TRANS_COUNT) & COUNT_MASK);
}

static void rp2350_dma_isr(void *arg)
{
    emb_rp2350_dma_data_t *d = (emb_rp2350_dma_data_t *)arg;
    uintptr_t base = rp2350_dma_config(d->dev)->base;
    uint32_t pending = REG32(base + INTS0);
    uint32_t c;
    REG32(base + INTS0) = pending;                   /* write one to clear */
    for (c = 0u; c < EMB_RP2350_DMA_CHANNELS; c++) { /* bounded: sixteen channels */
        emb_rp2350_dma_channel_t *ch = &d->channels[c];
        uintptr_t r = base + (CH_STEP * c);
        uint32_t ctrl;
        if ((((pending >> c) & 1u) == 0u) || (ch->running == 0u)) {
            continue;
        }
        ctrl = REG32(r + CTRL_TRIG);
        if ((ch->circular != 0u) && ((ctrl & CTRL_AHB_ERROR) == 0u)) {
            /* a circular job goes on, triggering itself: the half it just finished */
            ch->half ^= 1u;
            ch->done(ch->arg, EMB_OK, (ch->half != 0u) ? (ch->count / 2u) : ch->count);
            continue;
        }
        if ((ctrl & CTRL_AHB_ERROR) != 0u) {
            REG32(r + CTRL_TRIG) = ctrl & ~CTRL_EN; /* stopped, its error bits cleared */
        }
        ch->running = 0u;
        ch->done(ch->arg, ((ctrl & CTRL_AHB_ERROR) != 0u) ? EMB_EXFER : EMB_OK,
                 rp2350_dma_moved(r, ch));
    }
}

static emb_status_t rp2350_dma_init(const emb_device_t *dev)
{
    const emb_rp2350_dma_config_t *cfg = rp2350_dma_config(dev);
    emb_rp2350_dma_data_t *d = rp2350_dma_data(dev);
    uint32_t c;
    d->dev = dev;
    for (c = 0u; c < EMB_RP2350_DMA_CHANNELS; c++) { /* bounded: sixteen channels */
        d->channels[c].running = 0u;
    }
    REG32(cfg->base + INTE0) = 0u;
    REG32(cfg->base + INTS0) = 0xFFFFu;
    if (emb_irq_connect((emb_irq_t)cfg->irq, rp2350_dma_isr, d) != EMB_OK) {
        return EMB_EINVAL;
    }
    return (emb_irq_enable((emb_irq_t)cfg->irq) == EMB_OK) ? EMB_OK : EMB_EINVAL;
}

static uint32_t rp2350_dma_size(uint32_t width)
{
    return (width == 4u) ? 2u : ((width == 2u) ? 1u : 0u);
}

static emb_status_t rp2350_dma_start(const emb_device_t *dev, const emb_drv_dma_chan_t *ch,
                                     const emb_drv_dma_job_t *job, emb_drv_dma_done_t done,
                                     void *arg)
{
    const emb_rp2350_dma_config_t *cfg = rp2350_dma_config(dev);
    emb_rp2350_dma_data_t *d = rp2350_dma_data(dev);
    uint32_t c = ch->channel;
    uintptr_t r;
    uint32_t ctrl;
    bool fixed = (job->flags & EMB_DRV_DMA_FIXED_MEM) != 0u;
    emb_irq_key_t key;
    uint32_t ring = 0u;
    bool circular = (job->flags & EMB_DRV_DMA_CIRCULAR) != 0u;
    if ((c >= EMB_RP2350_DMA_CHANNELS) || (job->count > COUNT_MASK)) {
        return EMB_EINVAL;
    }
    if (circular) {
        /* its memory a power of two bytes, aligned to its size: the address ring wraps it */
        uint32_t bytes = job->count * job->width;
        uintptr_t mem = (job->dir == EMB_DRV_DMA_FROM_PERIPH) ? job->dst : job->src;
        while ((ring < RING_MAX) && (((uint32_t)1u << ring) < bytes)) { /* bounded: RING_MAX */
            ring++;
        }
        if ((((uint32_t)1u << ring) != bytes) || ((mem & (bytes - 1u)) != 0u)) {
            return EMB_EINVAL;
        }
    }
    r = cfg->base + (CH_STEP * c);
    ctrl = CTRL_EN | (rp2350_dma_size(job->width) << CTRL_DATA_SIZE) |
           (c << CTRL_CHAIN_TO); /* no chain */
    if (job->dir == EMB_DRV_DMA_MEM_TO_MEM) {
        ctrl |= CTRL_INCR_WRITE | (fixed ? 0u : CTRL_INCR_READ) | (TREQ_FORCE << CTRL_TREQ_SEL);
    } else if (job->dir == EMB_DRV_DMA_TO_PERIPH) {
        ctrl |= (fixed ? 0u : CTRL_INCR_READ) | ((uint32_t)ch->request << CTRL_TREQ_SEL);
    } else {
        ctrl |= (fixed ? 0u : CTRL_INCR_WRITE) | ((uint32_t)ch->request << CTRL_TREQ_SEL);
    }
    if (circular) {
        ctrl |= (ring << CTRL_RING_SIZE) |
                ((job->dir == EMB_DRV_DMA_FROM_PERIPH) ? CTRL_RING_WRITE : 0u);
    }
    key = emb_irq_lock();
    if (d->channels[c].running != 0u) {
        emb_irq_unlock(key);
        return EMB_EBUSY;
    }
    d->channels[c].done = done;
    d->channels[c].arg = arg;
    d->channels[c].count = job->count;
    d->channels[c].width = job->width;
    if ((ctrl & CTRL_INCR_WRITE) != 0u) {
        d->channels[c].side = SIDE_WRITE;
        d->channels[c].start = (uint32_t)job->dst;
    } else if ((ctrl & CTRL_INCR_READ) != 0u) {
        d->channels[c].side = SIDE_READ;
        d->channels[c].start = (uint32_t)job->src;
    } else {
        d->channels[c].side = SIDE_NONE;
        d->channels[c].start = 0u;
    }
    d->channels[c].running = 1u;
    d->channels[c].circular = circular ? 1u : 0u;
    d->channels[c].half = 0u;
    REG32(r + READ_ADDR) = (uint32_t)job->src;
    REG32(r + WRITE_ADDR) = (uint32_t)job->dst;
    /* once (mode 0); a circular job a half at a time, triggering itself again at each end */
    REG32(r + TRANS_COUNT) = circular ? (MODE_TRIGGER_SELF | (job->count / 2u)) : job->count;
    REG32(cfg->base + INTS0) = 1u << c;
    REG32(cfg->base + INTE0) |= 1u << c;
    REG32(r + CTRL_TRIG) = ctrl; /* the trigger: it starts */
    emb_irq_unlock(key);
    return EMB_OK;
}

/* Its interrupt off first, so that the abort ends nothing twice; then the abort, which waits
 * for the transfers in flight (the pico-sdk's dma_channel_abort()). */
static emb_status_t rp2350_dma_stop(const emb_device_t *dev, const emb_drv_dma_chan_t *ch,
                                    uint32_t *moved)
{
    const emb_rp2350_dma_config_t *cfg = rp2350_dma_config(dev);
    emb_rp2350_dma_data_t *d = rp2350_dma_data(dev);
    uint32_t c = ch->channel;
    uintptr_t r;
    uint32_t n;
    emb_irq_key_t key;
    if (c >= EMB_RP2350_DMA_CHANNELS) {
        return EMB_EINVAL;
    }
    r = cfg->base + (CH_STEP * c);
    key = emb_irq_lock();
    if (d->channels[c].running == 0u) {
        emb_irq_unlock(key);
        return EMB_ESTATE;
    }
    REG32(cfg->base + INTE0) &= ~(1u << c);
    REG32(cfg->base + CHAN_ABORT) = 1u << c;
    for (n = 0u; n < ABORT_POLL; n++) {
        if ((REG32(r + CTRL_TRIG) & CTRL_BUSY) == 0u) {
            break;
        }
    }
    *moved = rp2350_dma_moved(r, &d->channels[c]);
    REG32(cfg->base + INTS0) = 1u << c;
    d->channels[c].running = 0u;
    emb_irq_unlock(key);
    return EMB_OK;
}

const emb_drv_dma_api_t emb_rp2350_dma_api = {
    {rp2350_dma_init, NULL, NULL, NULL, NULL,
     EMB_DRV_DMA_CAP_MEM_TO_MEM | EMB_DRV_DMA_CAP_CIRCULAR},
    rp2350_dma_start,
    rp2350_dma_stop,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_rp2350_dma_off_t;
#endif
