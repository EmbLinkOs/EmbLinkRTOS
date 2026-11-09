/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Arm PrimeCell SSP PL022 (Arm DDI 0194H) as an SPI controller (SPEC-017 §5). One routine
 * moves a transfer on: it drains the receive FIFO and keeps the transmit FIFO full, at most
 * eight frames in flight so the receive FIFO never overruns. start() runs it once, and a
 * transfer it finishes there (QEMU's model moves frames at once) ends with start()'s status
 * (A115); otherwise the receive FIFO's level and timeout interrupts run it until the last
 * frame is back.
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
#define CR0_SPO   (1u << 6) /* CPOL */
#define CR0_SPH   (1u << 7) /* CPHA */
#define CR0_SCR   8u        /* the serial clock rate's shift */
#define CR1_LBM   (1u << 0)
#define CR1_SSE   (1u << 1)
#define SR_TNF    (1u << 1)
#define SR_RNE    (1u << 2)
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
static void pl022_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_pl022_data_t *d = pl022_data(dev);
    emb_drv_spi_req_t *x = EMB_DRV_SPI_REQ(req);
    bool now = false;
    emb_irq_key_t key = emb_irq_lock();
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
