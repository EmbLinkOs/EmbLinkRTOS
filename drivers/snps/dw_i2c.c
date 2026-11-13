/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    dw_i2c.c
 * @brief   DW_apb_i2c (the RP2350 datasheet §12.2; register offsets and bits as pico-sdk 2.1.1's
 *          hardware/regs/i2c.h names them) as an I2C class device (SPEC-017 §6), by interrupt.
 *
 * @details
 * A transaction is a stream of commands in the transmit FIFO: a byte to write, or a read,
 * with RESTART on a segment's first and STOP on the last. Reads come back in the receive
 * FIFO, at most its depth in flight. The stop condition (STOP_DET) ends a transaction; an
 * abort (TX_ABRT) ends it early with the controller's own stop, its reason in
 * IC_TX_ABRT_SOURCE. As a target the controller answers its address (IC_SAR): bytes written
 * arrive in the receive FIFO, a read request (RD_REQ) asks for the next byte to send.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/irq.h>

#include <emb_dw_i2c.h>

#if CONFIG_EMB_DRV_I2C
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define IC_CON                0x00u
#define IC_TAR                0x04u
#define IC_SAR                0x08u
#define IC_DATA_CMD           0x10u
#define IC_FS_SCL_HCNT        0x1Cu
#define IC_FS_SCL_LCNT        0x20u
#define IC_INTR_STAT          0x2Cu
#define IC_INTR_MASK          0x30u
#define IC_RX_TL              0x38u
#define IC_TX_TL              0x3Cu
#define IC_CLR_INTR           0x40u
#define IC_CLR_RD_REQ         0x50u
#define IC_CLR_TX_ABRT        0x54u
#define IC_CLR_RX_DONE        0x58u
#define IC_CLR_STOP_DET       0x60u
#define IC_ENABLE             0x6Cu
#define IC_STATUS             0x70u
#define IC_TXFLR              0x74u
#define IC_RXFLR              0x78u
#define IC_SDA_HOLD           0x7Cu
#define IC_TX_ABRT_SOURCE     0x80u
#define IC_SLV_DATA_NACK_ONLY 0x84u
#define IC_ENABLE_STATUS      0x9Cu
#define IC_FS_SPKLEN          0xA0u

#define CON_MASTER_MODE      (1u << 0)
#define CON_SPEED_FAST       (2u << 1) /* the FS counts, which every speed here uses */
#define CON_RESTART_EN       (1u << 5)
#define CON_SLAVE_DISABLE    (1u << 6)
#define CON_STOP_DET_IFADDR  (1u << 7)
#define CON_TX_EMPTY_CTRL    (1u << 8)
#define CON_RX_FIFO_FULL_HLD (1u << 9)

#define CMD_READ    (1u << 8)
#define CMD_STOP    (1u << 9)
#define CMD_RESTART (1u << 10)

#define INTR_RX_FULL  (1u << 2)
#define INTR_TX_EMPTY (1u << 4)
#define INTR_RD_REQ   (1u << 5)
#define INTR_TX_ABRT  (1u << 6)
#define INTR_RX_DONE  (1u << 7)
#define INTR_STOP_DET (1u << 9)

#define ENABLE_ENABLE (1u << 0)
#define ENABLE_ABORT  (1u << 1)

#define STATUS_SLV_ACTIVITY (1u << 6)

#define ABRT_7B_ADDR_NOACK (1u << 0)
#define ABRT_TXDATA_NOACK  (1u << 3)
#define ABRT_LOST          (1u << 12)
#define ABRT_USER_ABRT     (1u << 16)
#define ABRT_FLUSH_SHIFT   23u /* TX_FLUSH_CNT: the commands the abort flushed */

#define SDA_TX_HOLD    0xFFFFu  /* IC_SDA_HOLD's transmit hold, its low half */
#define FIFO_DEPTH     16u      /* IC_TX_BUFFER_DEPTH and IC_RX_BUFFER_DEPTH */
#define DISABLE_POLL   100000u  /* bounded: the controller goes idle within a byte's time */
#define SPEED_MAX      1000000u /* fast mode plus */
#define ONES           0xFFu
#define COUNT_MIN      8u /* SCL high and low counts, at least (pico-sdk's check) */
#define COUNT_MAX      0xFFFFu
#define HOLD_NS_FAST   10000000u /* 300 ns of SDA hold: clock * 3 / 10^7, + 1 */
#define HOLD_NS_FASTPL 25000000u /* 120 ns in fast mode plus */

static const emb_dw_i2c_config_t *dw_config(const emb_device_t *dev)
{
    return (const emb_dw_i2c_config_t *)dev->config;
}

static emb_dw_i2c_data_t *dw_data(const emb_device_t *dev)
{
    return (emb_dw_i2c_data_t *)dev->data;
}

/* Off, so that its mode and counts may change; bounded by DISABLE_POLL. */
static void dw_disable(uintptr_t base)
{
    uint32_t n;
    REG32(base + IC_ENABLE) = 0u;
    for (n = 0u; n < DISABLE_POLL; n++) {
        if ((REG32(base + IC_ENABLE_STATUS) & 1u) == 0u) {
            break;
        }
    }
}

/* SCL's counts for a clock of at most @hz (pico-sdk's i2c_set_baudrate, its period rounded
 * up); false when the controller's clock cannot reach it. */
static bool dw_timing(uintptr_t base, uint32_t clock_hz, uint32_t hz)
{
    uint32_t period;
    uint32_t lcnt;
    uint32_t hcnt;
    uint32_t hold;
    if ((hz == 0u) || (hz > SPEED_MAX)) {
        return false;
    }
    period = (clock_hz / hz) + (((clock_hz % hz) != 0u) ? 1u : 0u);
    lcnt = (period * 3u) / 5u;
    hcnt = period - lcnt;
    if ((hcnt < COUNT_MIN) || (lcnt < COUNT_MIN) || (hcnt > COUNT_MAX) || (lcnt > COUNT_MAX)) {
        return false;
    }
    hold = (clock_hz * 3u / ((hz < SPEED_MAX) ? HOLD_NS_FAST : HOLD_NS_FASTPL)) + 1u;
    REG32(base + IC_FS_SCL_HCNT) = hcnt;
    REG32(base + IC_FS_SCL_LCNT) = lcnt;
    REG32(base + IC_FS_SPKLEN) = (lcnt < 16u) ? 1u : (lcnt / 16u);
    REG32(base + IC_SDA_HOLD) = (REG32(base + IC_SDA_HOLD) & ~SDA_TX_HOLD) | hold;
    return true;
}

/* Target mode for the listen waiting: its address acknowledged from here on. */
static void dw_target_mode(uintptr_t base, const emb_drv_i2c_req_t *l)
{
    dw_disable(base);
    REG32(base + IC_CON) = CON_SPEED_FAST | CON_STOP_DET_IFADDR | CON_RX_FIFO_FULL_HLD;
    REG32(base + IC_SAR) = l->own;
    REG32(base + IC_RX_TL) = 0u; /* each byte: the listen's buffer fills as they come */
    REG32(base + IC_SLV_DATA_NACK_ONLY) = (l->rx_len == 0u) ? 1u : 0u;
    (void)REG32(base + IC_CLR_INTR);
    REG32(base + IC_INTR_MASK) = INTR_RX_FULL | INTR_RD_REQ | INTR_STOP_DET | INTR_TX_ABRT;
    REG32(base + IC_ENABLE) = ENABLE_ENABLE;
}

/* Neither a transaction nor a listen: off, so that no address is acknowledged. */
static void dw_idle(uintptr_t base, const emb_dw_i2c_data_t *d)
{
    if (d->listen != NULL) {
        dw_target_mode(base, d->listen);
        return;
    }
    REG32(base + IC_INTR_MASK) = 0u;
    dw_disable(base);
}

/* The read segment the next byte received belongs to. */
static void dw_next_read(emb_dw_i2c_data_t *d, const emb_drv_i2c_req_t *x)
{
    while ((d->rseg < x->count) &&
           ((x->segs[d->rseg].rd == NULL) || (d->rpos >= x->segs[d->rseg].len))) {
        d->rseg++; /* bounded by the segments */
        d->rpos = 0u;
    }
}

/* Takes what has come back. */
static void dw_drain(uintptr_t base, emb_dw_i2c_data_t *d, const emb_drv_i2c_req_t *x)
{
    while ((REG32(base + IC_RXFLR) != 0u) && (d->reads_out != 0u)) { /* bounded: FIFO_DEPTH */
        uint8_t b = (uint8_t)REG32(base + IC_DATA_CMD);
        dw_next_read(d, x);
        if (d->rseg < x->count) {
            x->segs[d->rseg].rd[d->rpos] = b;
            d->rpos++;
        }
        d->reads_out--;
        d->received++;
    }
}

/* Queues commands while the FIFOs have room: at most FIFO_DEPTH reads unanswered. */
static void dw_feed(uintptr_t base, emb_dw_i2c_data_t *d, const emb_drv_i2c_req_t *x)
{
    while (!d->issued && !d->stopping && (REG32(base + IC_TXFLR) < FIFO_DEPTH)) {
        const emb_drv_i2c_seg_t *g = &x->segs[d->seg];
        uint32_t cmd;
        if ((g->rd != NULL) && ((d->reads_out + REG32(base + IC_RXFLR)) >= FIFO_DEPTH)) {
            break; /* the receive FIFO's room: the rest after the next drain */
        }
        cmd = (g->rd != NULL) ? CMD_READ : (uint32_t)g->wr[d->pos];
        if ((d->pos == 0u) && (d->seg != 0u)) {
            cmd |= CMD_RESTART; /* each segment after the first */
        }
        if (((d->seg + 1u) == x->count) && ((d->pos + 1u) == g->len)) {
            cmd |= CMD_STOP;
            d->issued = true;
        }
        REG32(base + IC_DATA_CMD) = cmd;
        if (g->rd != NULL) {
            d->reads_out++;
        } else {
            d->written++;
        }
        d->pos++;
        if (d->pos == g->len) {
            d->seg++;
            d->pos = 0u;
        }
    }
    REG32(base + IC_INTR_MASK) = INTR_TX_ABRT | INTR_STOP_DET | INTR_RX_FULL |
                                 (d->issued ? 0u : INTR_TX_EMPTY);
}

static emb_status_t dw_abort_status(uint32_t source, bool stopping)
{
    if (stopping || ((source & ABRT_USER_ABRT) != 0u)) {
        return EMB_ECANCELED;
    }
    if ((source & (ABRT_7B_ADDR_NOACK | ABRT_TXDATA_NOACK)) != 0u) {
        return EMB_ENACK;
    }
    return ((source & ABRT_LOST) != 0u) ? EMB_EARBLOST : EMB_EBUSERR;
}

/* The transaction ended, with @st; the bytes moved are the reads answered and the writes the
 * target acknowledged. */
static void dw_end_xfer(uintptr_t base, emb_dw_i2c_data_t *d, emb_drv_i2c_req_t *x, emb_status_t st,
                        uint32_t unacked)
{
    uint32_t wrote = (d->written > unacked) ? (d->written - unacked) : 0u;
    d->xfer = NULL;
    dw_idle(base, d);
    emb_drv_complete(d->dev, &x->req, st, wrote + d->received);
}

static void dw_isr_controller(uintptr_t base, emb_dw_i2c_data_t *d, uint32_t stat)
{
    emb_drv_i2c_req_t *x = d->xfer;
    dw_drain(base, d, x);
    if ((stat & INTR_TX_ABRT) != 0u) {
        uint32_t source = REG32(base + IC_TX_ABRT_SOURCE);
        uint32_t flushed = source >> ABRT_FLUSH_SHIFT;
        (void)REG32(base + IC_CLR_TX_ABRT);
        /* the byte not acknowledged and those flushed after it were not moved */
        dw_end_xfer(base, d, x, dw_abort_status(source, d->stopping),
                    flushed + (((source & ABRT_TXDATA_NOACK) != 0u) ? 1u : 0u));
        return;
    }
    if (((stat & INTR_STOP_DET) != 0u) && d->issued && (d->reads_out == 0u)) {
        (void)REG32(base + IC_CLR_STOP_DET);
        dw_end_xfer(base, d, x, EMB_OK, 0u);
        return;
    }
    dw_feed(base, d, x);
}

static void dw_isr_target(uintptr_t base, emb_dw_i2c_data_t *d, uint32_t stat)
{
    emb_drv_i2c_req_t *l = d->listen;
    while (REG32(base + IC_RXFLR) != 0u) { /* bounded: FIFO_DEPTH */
        uint8_t b = (uint8_t)REG32(base + IC_DATA_CMD);
        if ((size_t)l->req.done < l->rx_len) {
            l->rx[l->req.done] = b;
            l->req.done++;
        }
        /* beyond the buffer: dropped; IC_SLV_DATA_NACK_ONLY cannot change while active */
    }
    if ((stat & INTR_RD_REQ) != 0u) {
        REG32(base + IC_DATA_CMD) = (l->sent < l->tx_len) ? l->tx[l->sent] : ONES;
        l->sent++;
        (void)REG32(base + IC_CLR_RD_REQ);
    }
    if ((stat & INTR_TX_ABRT) != 0u) {
        (void)REG32(base + IC_CLR_TX_ABRT); /* a byte queued for a read that ended */
    }
    if ((stat & INTR_STOP_DET) != 0u) {
        (void)REG32(base + IC_CLR_STOP_DET);
        (void)REG32(base + IC_CLR_RX_DONE);
        d->listen = NULL;
        dw_idle(base, d);
        emb_drv_complete(d->dev, &l->req, EMB_OK, l->req.done);
    }
}

static void dw_isr(void *arg)
{
    emb_dw_i2c_data_t *d = (emb_dw_i2c_data_t *)arg;
    uintptr_t base = dw_config(d->dev)->base;
    uint32_t stat = REG32(base + IC_INTR_STAT);
    if (d->xfer != NULL) {
        dw_isr_controller(base, d, stat);
    } else if (d->listen != NULL) {
        dw_isr_target(base, d, stat);
    } else {
        REG32(base + IC_INTR_MASK) = 0u;
        (void)REG32(base + IC_CLR_INTR);
    }
}

static emb_status_t dw_init(const emb_device_t *dev)
{
    const emb_dw_i2c_config_t *cfg = dw_config(dev);
    emb_dw_i2c_data_t *d = dw_data(dev);
    d->xfer = NULL;
    d->listen = NULL;
    d->dev = dev;
    if (emb_drv_i2c_bus_init(dev) != EMB_OK) {
        return EMB_EINVAL;
    }
    REG32(cfg->base + IC_INTR_MASK) = 0u;
    dw_disable(cfg->base);
    if (emb_irq_connect((emb_irq_t)cfg->irq, dw_isr, d) != EMB_OK) {
        return EMB_EINVAL;
    }
    return (emb_irq_enable((emb_irq_t)cfg->irq) == EMB_OK) ? EMB_OK : EMB_EINVAL;
}

static emb_status_t dw_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    const emb_dw_i2c_config_t *cfg = dw_config(dev);
    emb_dw_i2c_data_t *d = dw_data(dev);
    emb_drv_i2c_req_t *x = EMB_DRV_I2C_REQ(req);
    emb_irq_key_t key = emb_irq_lock();
    if (req->lane == EMB_DRV_I2C_LANE_TARGET) {
        d->listen = x;
        if (d->xfer == NULL) {
            dw_disable(cfg->base); /* the counts change only while it is off */
            if (!dw_timing(cfg->base, cfg->clock_hz, EMB_DRV_I2C_FAST)) {
                d->listen = NULL;
                emb_irq_unlock(key);
                return EMB_ENOTSUP;
            }
            dw_target_mode(cfg->base, x);
        } /* else after the transaction */
        emb_irq_unlock(key);
        return EMB_EBUSY;
    }
    dw_disable(cfg->base);
    if (!dw_timing(cfg->base, cfg->clock_hz, x->target->hz)) {
        dw_idle(cfg->base, d);
        emb_irq_unlock(key);
        return EMB_ENOTSUP; /* faster than fast mode plus, or than the clock reaches */
    }
    REG32(cfg->base + IC_CON) = CON_MASTER_MODE | CON_SPEED_FAST | CON_RESTART_EN |
                                CON_SLAVE_DISABLE | CON_TX_EMPTY_CTRL;
    REG32(cfg->base + IC_TAR) = x->target->address;
    REG32(cfg->base + IC_RX_TL) = 0u;
    REG32(cfg->base + IC_TX_TL) = FIFO_DEPTH / 2u;
    (void)REG32(cfg->base + IC_CLR_INTR);
    d->xfer = x;
    d->seg = 0u;
    d->pos = 0u;
    d->rseg = 0u;
    d->rpos = 0u;
    d->written = 0u;
    d->reads_out = 0u;
    d->received = 0u;
    d->issued = false;
    d->stopping = false;
    REG32(cfg->base + IC_ENABLE) = ENABLE_ENABLE;
    dw_feed(cfg->base, d, x);
    emb_irq_unlock(key);
    return EMB_EBUSY;
}

/* A transaction: the controller's abort ends it with a stop, and its interrupt completes it.
 * A listen: off at once when no controller is talking to it; otherwise its stop ends it. */
static void dw_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    const emb_dw_i2c_config_t *cfg = dw_config(dev);
    emb_dw_i2c_data_t *d = dw_data(dev);
    emb_drv_i2c_req_t *x = EMB_DRV_I2C_REQ(req);
    bool now = false;
    emb_irq_key_t key = emb_irq_lock();
    if (d->xfer == x) {
        d->stopping = true;
        REG32(cfg->base + IC_ENABLE) = ENABLE_ENABLE | ENABLE_ABORT;
    } else if (d->listen == x) {
        if ((d->xfer != NULL) || ((REG32(cfg->base + IC_STATUS) & STATUS_SLV_ACTIVITY) == 0u)) {
            d->listen = NULL;
            if (d->xfer == NULL) {
                dw_idle(cfg->base, d);
            }
            now = true;
        } /* else its stop condition ends it, a moment later */
    } else {
        now = false; /* it ended already */
    }
    emb_irq_unlock(key);
    if (now) {
        emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
    }
}

/* After the class's recovery gave the lines back: off, which clears the controller's state, and
 * back to waiting as a target, or idle. */
static void dw_reset(const emb_device_t *dev)
{
    const emb_dw_i2c_config_t *cfg = dw_config(dev);
    emb_dw_i2c_data_t *d = dw_data(dev);
    emb_irq_key_t key = emb_irq_lock();
    REG32(cfg->base + IC_INTR_MASK) = 0u;
    dw_disable(cfg->base);
    (void)REG32(cfg->base + IC_CLR_INTR);
    if (d->listen != NULL) {
        (void)dw_timing(cfg->base, cfg->clock_hz, EMB_DRV_I2C_FAST);
    }
    dw_idle(cfg->base, d);
    emb_irq_unlock(key);
}

const emb_drv_i2c_api_t emb_dw_i2c_api = {
    {dw_init, dw_start, dw_stop, NULL, NULL,
     EMB_DRV_I2C_CAP_TARGET | EMB_DRV_I2C_CAP_FAST_MODE | EMB_DRV_I2C_CAP_FAST_MODE_PLUS},
    dw_reset,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_dw_i2c_off_t;
#endif
