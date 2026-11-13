/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    stm32_spi.c
 * @brief   The STM32 SPI as a controller of the SPI class (SPEC-017 §5; RM0390 Rev 6 §26).
 *
 * @details
 * Its own chip-select input managed by software (SSM, SSI), so it is never a mode fault; one frame
 * in flight, the receive interrupt storing each frame and sending the next.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_SPI
#include <emb/irq.h>

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define SPI_CR1     0x00u
#define SPI_CR2     0x04u
#define SPI_SR      0x08u
#define SPI_DR      0x0Cu
#define CR1_CPHA    (1u << 0)
#define CR1_CPOL    (1u << 1)
#define CR1_MSTR    (1u << 2)
#define CR1_BR      3u /* the baud rate's shift: SPICLK = PCLK / 2^(BR + 1) */
#define CR1_SPE     (1u << 6)
#define CR1_SSI     (1u << 8)
#define CR1_SSM     (1u << 9)
#define CR2_RXDMAEN (1u << 0)
#define CR2_TXDMAEN (1u << 1)
#define CR2_ERRIE   (1u << 5)
#define CR2_RXNEIE  (1u << 6)
#define SR_RXNE     (1u << 0)
#define SR_MODF     (1u << 5)
#define SR_BSY      (1u << 7)
#define DMA_MIN     16u     /* frames: shorter transfers stay with the interrupt */
#define BSY_POLL    100000u /* bounded: the frame in flight */
#define BR_MAX      7u
#define ONES        0xFFu

static const emb_stm32_spi_config_t *stm32_spi_config(const emb_device_t *dev)
{
    return (const emb_stm32_spi_config_t *)dev->config;
}

static emb_stm32_spi_data_t *stm32_spi_data(const emb_device_t *dev)
{
    return (emb_stm32_spi_data_t *)dev->data;
}

static void stm32_spi_send(uintptr_t base, const emb_drv_spi_req_t *x)
{
    REG32(base + SPI_DR) = (x->tx != NULL) ? x->tx[x->req.done] : ONES;
}

static void stm32_spi_finish(uintptr_t base, emb_stm32_spi_data_t *d)
{
    REG32(base + SPI_CR2) = 0u;
    REG32(base + SPI_CR1) &= ~CR1_SPE;
    d->xfer = NULL;
}

/* A DMA transfer's end: the receive job took the last frame, or failed. */
static void stm32_spi_dma_end(emb_stm32_spi_data_t *d, emb_status_t st, uint32_t moved)
{
    emb_drv_spi_req_t *x = d->xfer;
    if (x == NULL) {
        return; /* stopped already */
    }
    stm32_spi_finish(stm32_spi_config(d->dev)->base, d);
    emb_drv_spi_end(d->dev, x);
    emb_drv_complete(d->dev, &x->req, st, moved);
}

static void stm32_spi_dma_rx_done(void *arg, emb_status_t st, uint32_t moved)
{
    stm32_spi_dma_end((emb_stm32_spi_data_t *)arg, st, moved);
}

static void stm32_spi_dma_tx_done(void *arg, emb_status_t st, uint32_t moved)
{
    emb_stm32_spi_data_t *d = (emb_stm32_spi_data_t *)arg;
    (void)moved;
    if ((st != EMB_OK) && (d->xfer != NULL)) { /* the frames stopped coming: so does the transfer */
        uint32_t got = 0u;
        (void)emb_drv_dma_stop(&stm32_spi_config(d->dev)->dma_rx, &got);
        stm32_spi_dma_end(d, st, got);
    }
}

/* The transfer by DMA, where the board gave the device channels and the buffers are where DMA
 * reaches; false leaves it to the interrupt. The receive request first, the transmit request
 * last, which starts the frames (RM0390 §26.3.9). */
static bool stm32_spi_dma_start(const emb_stm32_spi_config_t *cfg, emb_stm32_spi_data_t *d,
                                const emb_drv_spi_req_t *x)
{
    emb_drv_dma_job_t rx;
    emb_drv_dma_job_t tx;
    if ((cfg->dma_tx.dma == NULL) || (cfg->dma_rx.dma == NULL) || (x->len < DMA_MIN) ||
        (x->len > UINT32_MAX)) {
        return false;
    }
    d->fill = ONES;
    rx.src = cfg->base + SPI_DR;
    rx.dst = (x->rx != NULL) ? (uintptr_t)x->rx : (uintptr_t)&d->sink;
    rx.count = (uint32_t)x->len;
    rx.dir = EMB_DRV_DMA_FROM_PERIPH;
    rx.width = 1u;
    rx.flags = (uint8_t)((x->rx != NULL) ? 0u : EMB_DRV_DMA_FIXED_MEM);
    rx.reserved_ = 0u;
    tx.src = (x->tx != NULL) ? (uintptr_t)x->tx : (uintptr_t)&d->fill;
    tx.dst = cfg->base + SPI_DR;
    tx.count = (uint32_t)x->len;
    tx.dir = EMB_DRV_DMA_TO_PERIPH;
    tx.width = 1u;
    tx.flags = (uint8_t)((x->tx != NULL) ? 0u : EMB_DRV_DMA_FIXED_MEM);
    tx.reserved_ = 0u;
    REG32(cfg->base + SPI_CR2) = CR2_RXDMAEN | CR2_ERRIE;
    if (emb_drv_dma_start(&cfg->dma_rx, &rx, stm32_spi_dma_rx_done, d) != EMB_OK) {
        REG32(cfg->base + SPI_CR2) = 0u;
        return false;
    }
    if (emb_drv_dma_start(&cfg->dma_tx, &tx, stm32_spi_dma_tx_done, d) != EMB_OK) {
        (void)emb_drv_dma_stop(&cfg->dma_rx, NULL);
        REG32(cfg->base + SPI_CR2) = 0u;
        return false;
    }
    REG32(cfg->base + SPI_CR2) = CR2_RXDMAEN | CR2_TXDMAEN | CR2_ERRIE;
    return true;
}

static void stm32_spi_isr(void *arg)
{
    emb_stm32_spi_data_t *d = (emb_stm32_spi_data_t *)arg;
    uintptr_t base = stm32_spi_config(d->dev)->base;
    emb_drv_spi_req_t *x = d->xfer;
    uint32_t sr = REG32(base + SPI_SR);
    if (x == NULL) {
        REG32(base + SPI_CR2) = 0u;
        return;
    }
    if ((sr & SR_MODF) != 0u) {
        REG32(base + SPI_CR1) = REG32(base + SPI_CR1); /* the write after the read clears it */
        stm32_spi_finish(base, d);
        emb_drv_spi_end(d->dev, x);
        emb_drv_complete(d->dev, &x->req, EMB_EMODEFAULT, x->req.done);
        return;
    }
    if ((sr & SR_RXNE) != 0u) {
        uint8_t frame = (uint8_t)REG32(base + SPI_DR);
        if (x->rx != NULL) {
            x->rx[x->req.done] = frame;
        }
        x->req.done++;
        if (!d->stopping && (size_t)x->req.done < x->len) {
            stm32_spi_send(base, x);
            return;
        }
        {
            emb_status_t st = d->stopping ? EMB_ECANCELED : EMB_OK;
            stm32_spi_finish(base, d);
            emb_drv_spi_end(d->dev, x);
            emb_drv_complete(d->dev, &x->req, st, x->req.done);
        }
    }
}

static emb_status_t stm32_spi_init(const emb_device_t *dev)
{
    const emb_stm32_spi_config_t *cfg = stm32_spi_config(dev);
    emb_stm32_spi_data_t *d = stm32_spi_data(dev);
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit);
    REG32(cfg->base + SPI_CR1) = 0u;
    REG32(cfg->base + SPI_CR2) = 0u;
    d->xfer = NULL;
    d->dev = dev;
    d->dma = false;
    d->stopping = false;
    if (emb_drv_spi_bus_init(dev) != EMB_OK) {
        return EMB_EINVAL;
    }
    if (emb_irq_connect((emb_irq_t)cfg->irq, stm32_spi_isr, d) != EMB_OK) {
        return EMB_EINVAL;
    }
    return (emb_irq_enable((emb_irq_t)cfg->irq) == EMB_OK) ? EMB_OK : EMB_EINVAL;
}

static emb_status_t stm32_spi_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    const emb_stm32_spi_config_t *cfg = stm32_spi_config(dev);
    emb_stm32_spi_data_t *d = stm32_spi_data(dev);
    emb_drv_spi_req_t *x = EMB_DRV_SPI_REQ(req);
    const emb_drv_spi_target_t *t = x->target;
    uint32_t br = 0u;
    emb_irq_key_t key;
    if ((t->bits != 8u) || ((t->flags & EMB_DRV_SPI_LOOPBACK) != 0u)) {
        return EMB_ENOTSUP; /* 8-bit frames; no internal loopback on this SPI */
    }
    /* bounded: eight rates, the fastest not above the target's */
    while ((br < BR_MAX) && ((cfg->clock_hz >> (br + 1u)) > t->hz)) {
        br++;
    }
    if ((cfg->clock_hz >> (br + 1u)) > t->hz) {
        return EMB_ENOTSUP; /* slower than PCLK / 256 */
    }
    if (x->len == 0u) {
        return EMB_OK;
    }
    REG32(cfg->base + SPI_CR1) = CR1_MSTR | CR1_SSM | CR1_SSI | (br << CR1_BR) |
                                 (((t->mode & EMB_DRV_SPI_CPOL) != 0u) ? CR1_CPOL : 0u) |
                                 (((t->mode & EMB_DRV_SPI_CPHA) != 0u) ? CR1_CPHA : 0u);
    (void)REG32(cfg->base + SPI_DR); /* a stale frame and its overrun flag */
    (void)REG32(cfg->base + SPI_SR);
    key = emb_irq_lock();
    d->xfer = x;
    d->stopping = false;
    REG32(cfg->base + SPI_CR1) |= CR1_SPE; /* the clock idles at its polarity from here */
    emb_drv_spi_begin(dev, x);
    d->dma = stm32_spi_dma_start(cfg, d, x);
    if (!d->dma) {
        REG32(cfg->base + SPI_CR2) = CR2_RXNEIE | CR2_ERRIE;
        stm32_spi_send(cfg->base, x);
    } /* else the receive job's end completes it */
    emb_irq_unlock(key);
    return EMB_EBUSY;
}

/* Sends nothing more: the frame in flight comes back whole, and its interrupt completes the
 * transfer with EMB_ECANCELED, done counting it (SPEC-017 §5). SPE is never cleared mid-frame,
 * as RM0390 §26.3.8 requires. At most one frame time. */
/* By DMA: the transmit job stops, the frame already sent comes back by the receive job, and
 * then it stops too; a receive job that ends meanwhile completes the transfer itself. */
static void stm32_spi_dma_stop(const emb_device_t *dev, emb_stm32_spi_data_t *d,
                               emb_drv_spi_req_t *x)
{
    const emb_stm32_spi_config_t *cfg = stm32_spi_config(dev);
    uint32_t moved = 0u;
    uint32_t n;
    bool now = false;
    emb_irq_key_t key = emb_irq_lock();
    (void)emb_drv_dma_stop(&cfg->dma_tx, NULL);
    emb_irq_unlock(key);
    for (n = 0u; n < BSY_POLL; n++) {
        if ((REG32(cfg->base + SPI_SR) & SR_BSY) == 0u) {
            break;
        }
    }
    key = emb_irq_lock();
    if (d->xfer == x) {
        (void)emb_drv_dma_stop(&cfg->dma_rx, &moved);
        stm32_spi_finish(cfg->base, d);
        emb_drv_spi_end(dev, x);
        now = true;
    }
    emb_irq_unlock(key);
    if (now) {
        emb_drv_complete(dev, &x->req, EMB_ECANCELED, moved);
    }
}

static void stm32_spi_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_stm32_spi_data_t *d = stm32_spi_data(dev);
    emb_irq_key_t key;
    if ((d->xfer == EMB_DRV_SPI_REQ(req)) && d->dma) {
        stm32_spi_dma_stop(dev, d, EMB_DRV_SPI_REQ(req));
        return;
    }
    key = emb_irq_lock();
    if (d->xfer == EMB_DRV_SPI_REQ(req)) {
        d->stopping = true; /* a frame is always in flight while a transfer runs */
    }
    emb_irq_unlock(key);
}

const emb_device_api_t emb_stm32_spi_api = {
    stm32_spi_init, stm32_spi_start, stm32_spi_stop, NULL, NULL, 0u};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_spi_off_t;
#endif
