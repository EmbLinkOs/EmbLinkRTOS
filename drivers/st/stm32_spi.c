/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The STM32 SPI as a controller of the SPI class (SPEC-017 §5; RM0390 Rev 6 §26): its own
 * chip-select input managed by software (SSM, SSI), so it is never a mode fault; one frame
 * in flight, the receive interrupt storing each frame and sending the next.
 */
#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_SPI
#include <emb/irq.h>

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define SPI_CR1    0x00u
#define SPI_CR2    0x04u
#define SPI_SR     0x08u
#define SPI_DR     0x0Cu
#define CR1_CPHA   (1u << 0)
#define CR1_CPOL   (1u << 1)
#define CR1_MSTR   (1u << 2)
#define CR1_BR     3u /* the baud rate's shift: SPICLK = PCLK / 2^(BR + 1) */
#define CR1_SPE    (1u << 6)
#define CR1_SSI    (1u << 8)
#define CR1_SSM    (1u << 9)
#define CR2_ERRIE  (1u << 5)
#define CR2_RXNEIE (1u << 6)
#define SR_RXNE    (1u << 0)
#define SR_MODF    (1u << 5)
#define BR_MAX     7u
#define ONES       0xFFu

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
    REG32(cfg->base + SPI_CR2) = CR2_RXNEIE | CR2_ERRIE;
    stm32_spi_send(cfg->base, x);
    emb_irq_unlock(key);
    return EMB_EBUSY;
}

/* Sends nothing more: the frame in flight comes back whole, and its interrupt completes the
 * transfer with EMB_ECANCELED, done counting it (SPEC-017 §5). SPE is never cleared mid-frame,
 * as RM0390 §26.3.8 requires. At most one frame time. */
static void stm32_spi_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_stm32_spi_data_t *d = stm32_spi_data(dev);
    emb_irq_key_t key = emb_irq_lock();
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
