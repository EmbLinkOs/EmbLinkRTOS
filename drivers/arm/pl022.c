/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    pl022.c
 * @brief   Arm PrimeCell SSP PL022 (Arm DDI 0194H) as an SPI controller (SPEC-017 §5).
 *
 * @details
 * One routine moves a transfer on: it drains the receive FIFO and keeps the transmit FIFO full, at
 * most eight frames in flight so the receive FIFO never overruns. start() runs it once, and a
 * transfer it finishes there (QEMU's model moves frames at once) ends with start()'s status (A115);
 * otherwise the receive FIFO's level and timeout interrupts run it until the last frame is back.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_pl022.h>

#if CONFIG_EMB_DRV_SPI
#include <emb/irq.h>

#define REG32(a)  (*(volatile uint32_t *)(uintptr_t)(a))
#define SSP_CR0   0x00u
#define SSP_CR1   0x04u
#define SSP_DR    0x08u
#define SSP_SR    0x0Cu
#define SSP_CPSR  0x10u
#define SSP_IMSC  0x14u
#define SSP_ICR   0x20u
#define SSP_DMACR 0x24u
#define CR0_SPO   (1u << 6) /* CPOL */
#define CR0_SPH   (1u << 7) /* CPHA */
#define CR0_SCR   8u        /* the serial clock rate's shift */
#define CR1_LBM   (1u << 0)
#define CR1_SSE   (1u << 1)
#define SR_TNF    (1u << 1)
#define SR_RNE    (1u << 2)
#define SR_BSY    (1u << 4)
#define DMACR_RX  (1u << 0)
#define DMACR_TX  (1u << 1)
#define DMA_MIN   16u     /* frames: shorter transfers stay with the interrupt */
#define BSY_POLL  100000u /* bounded: the frames in flight, a FIFO's worth */
#define IM_RT     (1u << 1)
#define IM_RX     (1u << 2)
#define ICR_ALL   3u
#define IN_FLIGHT 8u /* the FIFOs' depth */
#define CPSR_MAX  254u
#define SCR_MAX   255u
#define ONES      0xFFu

static const emb_pl022_config_t *pl022_config(const emb_device_t *dev)
{
    return (const emb_pl022_config_t *)dev->config;
}

static emb_pl022_data_t *pl022_data(const emb_device_t *dev)
{
    return (emb_pl022_data_t *)dev->data;
}

/* The prescaler and serial clock rate for the fastest clock not above hz: SSPCLK /
 * (CPSDVSR * (1 + SCR)), CPSDVSR even from 2 to 254. False when even the slowest is faster. */
static bool pl022_rate(uint32_t clock_hz, uint32_t hz, uint32_t *cpsr, uint32_t *scr)
{
    uint32_t c;
    for (c = 2u; c <= CPSR_MAX; c += 2u) { /* bounded: 127 prescalers */
        uint32_t q = clock_hz / c;
        uint32_t s = (q / hz) + (((q % hz) != 0u) ? 1u : 0u); /* 1 + SCR, rounded up */
        if ((s >= 1u) && (s <= (SCR_MAX + 1u))) {
            *cpsr = c;
            *scr = s - 1u;
            return true;
        }
    }
    return false;
}

/* Moves the transfer on; true once its last frame is back. */
static bool pl022_service(uintptr_t base, emb_pl022_data_t *d, emb_drv_spi_req_t *x)
{
    bool moved = true;
    /* bounded: each pass takes a frame back or sends one, and the transfer has len of each */
    while (moved) {
        moved = false;
        while (((REG32(base + SSP_SR) & SR_RNE) != 0u) && ((size_t)x->req.done < x->len)) {
            uint8_t frame = (uint8_t)REG32(base + SSP_DR);
            if (x->rx != NULL) {
                x->rx[x->req.done] = frame;
            }
            x->req.done++;
            moved = true;
        }
        while (!d->stopping && (d->sent < x->len) &&
               ((d->sent - (size_t)x->req.done) < IN_FLIGHT) &&
               ((REG32(base + SSP_SR) & SR_TNF) != 0u)) {
            REG32(base + SSP_DR) = (x->tx != NULL) ? x->tx[d->sent] : ONES;
            d->sent++;
            moved = true;
        }
    }
    return (size_t)x->req.done >= (d->stopping ? d->sent : x->len);
}

static void pl022_finish(uintptr_t base, emb_pl022_data_t *d)
{
    REG32(base + SSP_IMSC) = 0u;
    REG32(base + SSP_CR1) = 0u; /* disabled between transfers: the next sets it up anew */
    d->xfer = NULL;
}

/* A DMA transfer's end: the receive job took the last frame, or failed. */
static void pl022_dma_end(emb_pl022_data_t *d, emb_status_t st, uint32_t moved)
{
    const emb_pl022_config_t *cfg = pl022_config(d->dev);
    emb_drv_spi_req_t *x = d->xfer;
    if (x == NULL) {
        return; /* stopped already */
    }
    REG32(cfg->base + SSP_DMACR) = 0u;
    pl022_finish(cfg->base, d);
    emb_drv_spi_end(d->dev, x);
    emb_drv_complete(d->dev, &x->req, st, moved);
}

static void pl022_dma_rx_done(void *arg, emb_status_t st, uint32_t moved)
{
    pl022_dma_end((emb_pl022_data_t *)arg, st, moved);
}

static void pl022_dma_tx_done(void *arg, emb_status_t st, uint32_t moved)
{
    emb_pl022_data_t *d = (emb_pl022_data_t *)arg;
    (void)moved;
    if ((st != EMB_OK) && (d->xfer != NULL)) { /* the frames stopped coming: so does the transfer */
        uint32_t got = 0u;
        (void)emb_drv_dma_stop(&pl022_config(d->dev)->dma_rx, &got);
        pl022_dma_end(d, st, got);
    }
}

/* The transfer by DMA, where the board gave the device channels and the buffers are where DMA
 * reaches; false leaves it to the interrupt. */
static bool pl022_dma_start(const emb_pl022_config_t *cfg, emb_pl022_data_t *d,
                            const emb_drv_spi_req_t *x)
{
    emb_drv_dma_job_t rx;
    emb_drv_dma_job_t tx;
    if ((cfg->dma_tx.dma == NULL) || (cfg->dma_rx.dma == NULL) || (x->len < DMA_MIN) ||
        (x->len > UINT32_MAX)) {
        return false;
    }
    d->fill = ONES;
    rx.src = cfg->base + SSP_DR;
    rx.dst = (x->rx != NULL) ? (uintptr_t)x->rx : (uintptr_t)&d->sink;
    rx.count = (uint32_t)x->len;
    rx.dir = EMB_DRV_DMA_FROM_PERIPH;
    rx.width = 1u;
    rx.flags = (uint8_t)((x->rx != NULL) ? 0u : EMB_DRV_DMA_FIXED_MEM);
    rx.reserved_ = 0u;
    tx.src = (x->tx != NULL) ? (uintptr_t)x->tx : (uintptr_t)&d->fill;
    tx.dst = cfg->base + SSP_DR;
    tx.count = (uint32_t)x->len;
    tx.dir = EMB_DRV_DMA_TO_PERIPH;
    tx.width = 1u;
    tx.flags = (uint8_t)((x->tx != NULL) ? 0u : EMB_DRV_DMA_FIXED_MEM);
    tx.reserved_ = 0u;
    if (emb_drv_dma_start(&cfg->dma_rx, &rx, pl022_dma_rx_done, d) != EMB_OK) {
        return false;
    }
    if (emb_drv_dma_start(&cfg->dma_tx, &tx, pl022_dma_tx_done, d) != EMB_OK) {
        (void)emb_drv_dma_stop(&cfg->dma_rx, NULL);
        return false;
    }
    REG32(cfg->base + SSP_DMACR) = DMACR_RX | DMACR_TX; /* the requests, the jobs ready */
    return true;
}

static void pl022_isr(void *arg)
{
    emb_pl022_data_t *d = (emb_pl022_data_t *)arg;
    uintptr_t base = pl022_config(d->dev)->base;
    emb_drv_spi_req_t *x = d->xfer;
    REG32(base + SSP_ICR) = ICR_ALL; /* the receive timeout and overrun */
    if (x == NULL) {
        REG32(base + SSP_IMSC) = 0u;
        return;
    }
    if (pl022_service(base, d, x)) {
        emb_status_t st = d->stopping ? EMB_ECANCELED : EMB_OK;
        pl022_finish(base, d);
        emb_drv_spi_end(d->dev, x);
        emb_drv_complete(d->dev, &x->req, st, x->req.done);
    }
}

static emb_status_t pl022_init(const emb_device_t *dev)
{
    const emb_pl022_config_t *cfg = pl022_config(dev);
    emb_pl022_data_t *d = pl022_data(dev);
    d->xfer = NULL;
    d->dev = dev;
    d->sent = 0u;
    d->dma = false;
    d->stopping = false;
    REG32(cfg->base + SSP_CR1) = 0u;
    REG32(cfg->base + SSP_IMSC) = 0u;
    if (emb_drv_spi_bus_init(dev) != EMB_OK) {
        return EMB_EINVAL;
    }
    if (emb_irq_connect((emb_irq_t)cfg->irq, pl022_isr, d) != EMB_OK) {
        return EMB_EINVAL;
    }
    return (emb_irq_enable((emb_irq_t)cfg->irq) == EMB_OK) ? EMB_OK : EMB_EINVAL;
}

static emb_status_t pl022_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    const emb_pl022_config_t *cfg = pl022_config(dev);
    emb_pl022_data_t *d = pl022_data(dev);
    emb_drv_spi_req_t *x = EMB_DRV_SPI_REQ(req);
    const emb_drv_spi_target_t *t = x->target;
    uint32_t cpsr = 0u;
    uint32_t scr = 0u;
    emb_irq_key_t key;
    bool done;
    if ((t->bits != 8u) || !pl022_rate(cfg->clock_hz, t->hz, &cpsr, &scr)) {
        return EMB_ENOTSUP; /* 8-bit frames, at a reachable rate */
    }
    if (x->len == 0u) {
        return EMB_OK;
    }
    REG32(cfg->base + SSP_CR1) = 0u;
    REG32(cfg->base + SSP_CR0) = (scr << CR0_SCR) | (t->bits - 1u) |
                                 (((t->mode & EMB_DRV_SPI_CPOL) != 0u) ? CR0_SPO : 0u) |
                                 (((t->mode & EMB_DRV_SPI_CPHA) != 0u) ? CR0_SPH : 0u);
    REG32(cfg->base + SSP_CPSR) = cpsr;
    while ((REG32(cfg->base + SSP_SR) & SR_RNE) != 0u) { /* bounded: the receive FIFO's 8 */
        (void)REG32(cfg->base + SSP_DR);                 /* stale frames */
    }
    REG32(cfg->base + SSP_ICR) = ICR_ALL;
    REG32(cfg->base + SSP_CR1) = CR1_SSE |
                                 (((t->flags & EMB_DRV_SPI_LOOPBACK) != 0u) ? CR1_LBM : 0u);
    emb_drv_spi_begin(dev, x); /* the clock idles at its polarity: the chip select now */
    key = emb_irq_lock();
    d->xfer = x;
    d->sent = 0u;
    d->stopping = false;
    d->dma = pl022_dma_start(cfg, d, x);
    if (d->dma) {
        emb_irq_unlock(key);
        return EMB_EBUSY; /* the receive job's end completes it */
    }
    done = pl022_service(cfg->base, d, x);
    if (done) {
        pl022_finish(cfg->base, d);
        emb_drv_spi_end(dev, x);
    } else {
        REG32(cfg->base + SSP_IMSC) = IM_RX | IM_RT;
    }
    emb_irq_unlock(key);
    return done ? EMB_OK : EMB_EBUSY;
}

/* Sends nothing more; the frames in flight come back whole, and the interrupt that takes the
 * last of them completes the transfer with EMB_ECANCELED, done counting them (SPEC-017 §5).
 * At most IN_FLIGHT frame times. */
/* By DMA: the transmit job stops, the frames already sent come back by the receive job, and
 * then it stops too; a receive job that ends meanwhile completes the transfer itself. */
static void pl022_dma_stop(const emb_device_t *dev, emb_pl022_data_t *d, emb_drv_spi_req_t *x)
{
    const emb_pl022_config_t *cfg = pl022_config(dev);
    uint32_t moved = 0u;
    uint32_t n;
    bool now = false;
    emb_irq_key_t key = emb_irq_lock();
    (void)emb_drv_dma_stop(&cfg->dma_tx, NULL);
    emb_irq_unlock(key);
    for (n = 0u; n < BSY_POLL; n++) {
        if ((REG32(cfg->base + SSP_SR) & SR_BSY) == 0u) {
            break;
        }
    }
    key = emb_irq_lock();
    if (d->xfer == x) {
        (void)emb_drv_dma_stop(&cfg->dma_rx, &moved);
        REG32(cfg->base + SSP_DMACR) = 0u;
        pl022_finish(cfg->base, d);
        emb_drv_spi_end(dev, x);
        now = true;
    }
    emb_irq_unlock(key);
    if (now) {
        emb_drv_complete(dev, &x->req, EMB_ECANCELED, moved);
    }
}

static void pl022_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_pl022_data_t *d = pl022_data(dev);
    emb_drv_spi_req_t *x = EMB_DRV_SPI_REQ(req);
    bool now = false;
    emb_irq_key_t key;
    if ((d->xfer == x) && d->dma) {
        pl022_dma_stop(dev, d, x);
        return;
    }
    key = emb_irq_lock();
    if (d->xfer == x) {
        d->stopping = true;
        if (pl022_service(pl022_config(dev)->base, d, x)) { /* nothing left in flight */
            pl022_finish(pl022_config(dev)->base, d);
            emb_drv_spi_end(dev, x);
            now = true;
        }
    }
    emb_irq_unlock(key);
    if (now) {
        emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
    } /* otherwise it ended already, or the interrupt ends it */
}

const emb_device_api_t emb_pl022_api = {pl022_init, pl022_start, pl022_stop,
                                        NULL,       NULL,        EMB_DRV_SPI_CAP_LOOPBACK};
#else
/* ISO C: a translation unit declares something */
typedef int emb_pl022_off_t;
#endif
