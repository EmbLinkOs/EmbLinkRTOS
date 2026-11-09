/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The STM32F4 I2C (RM0390 Rev 6 §24) as an I2C class device (SPEC-017 §6), by interrupt.
 *
 * As a controller each segment is a start (or a repeated start), the address with its
 * direction, then its bytes, and the last ends with a stop. A read's acknowledge is managed
 * as §24.3.3 asks: one byte, ACK off before ADDR is cleared; two, POS with ACK off and both
 * taken at BTF; more, the last three taken through BTF with ACK off before the last two.
 * As a target the controller acknowledges its address (OAR1) only while a listen waits: ADDR
 * starts its transaction, RXNE and TXE move its bytes, and the controller's stop (STOPF, the
 * target receiving) or its not-acknowledge of the last byte read (AF, the target sending)
 * ends it. */
#include <emb/irq.h>

#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_I2C
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define I2C_CR1   0x00u
#define I2C_CR2   0x04u
#define I2C_OAR1  0x08u
#define I2C_DR    0x10u
#define I2C_SR1   0x14u
#define I2C_SR2   0x18u
#define I2C_CCR   0x1Cu
#define I2C_TRISE 0x20u

#define CR1_PE    (1u << 0)
#define CR1_START (1u << 8)
#define CR1_STOP  (1u << 9)
#define CR1_ACK   (1u << 10)
#define CR1_POS   (1u << 11)
#define CR1_SWRST (1u << 15)

#define CR2_ITERREN (1u << 8)
#define CR2_ITEVTEN (1u << 9)
#define CR2_ITBUFEN (1u << 10)
#define CR2_FREQ    0x3Fu

#define OAR1_ALWAYS (1u << 14) /* "should always be kept at 1 by software" */

#define SR1_SB    (1u << 0)
#define SR1_ADDR  (1u << 1)
#define SR1_BTF   (1u << 2)
#define SR1_STOPF (1u << 4)
#define SR1_RXNE  (1u << 6)
#define SR1_TXE   (1u << 7)
#define SR1_BERR  (1u << 8)
#define SR1_ARLO  (1u << 9)
#define SR1_AF    (1u << 10)
#define SR1_OVR   (1u << 11)
#define SR1_ERRS  (SR1_BERR | SR1_ARLO | SR1_AF | SR1_OVR)

#define SR2_MSL  (1u << 0)
#define SR2_BUSY (1u << 1)
#define SR2_TRA  (1u << 2)

#define CCR_FS       (1u << 15)
#define CCR_MASK     0xFFFu
#define CCR_MIN_STD  4u
#define CCR_MIN_FAST 1u
#define MHZ          1000000u
#define FAST_MAX     400000u
#define STOP_POLL    100000u /* bounded: a stop condition takes a few bit times */
#define ONES         0xFFu

static const emb_stm32_i2c_config_t *stm32_i2c_config(const emb_device_t *dev)
{
    return (const emb_stm32_i2c_config_t *)dev->config;
}

static emb_stm32_i2c_data_t *stm32_i2c_data(const emb_device_t *dev)
{
    return (emb_stm32_i2c_data_t *)dev->data;
}

/* The clock registers for at most @hz (§24.6.8, §24.6.9), the peripheral off meanwhile. */
static bool stm32_i2c_timing(uintptr_t base, uint32_t clock_hz, uint32_t hz)
{
    uint32_t freq = clock_hz / MHZ;
    uint32_t ccr;
    uint32_t trise;
    if ((hz == 0u) || (hz > FAST_MAX)) {
        return false; /* fast mode plus is FMPI2C1's, another peripheral */
    }
    if (hz <= EMB_DRV_I2C_STANDARD) {
        uint32_t div = 2u * hz; /* SCL high and low each CCR periods */
        ccr = (clock_hz / div) + (((clock_hz % div) != 0u) ? 1u : 0u);
        ccr = (ccr < CCR_MIN_STD) ? CCR_MIN_STD : ccr;
        trise = freq + 1u; /* 1000 ns */
    } else {
        uint32_t div = 3u * hz; /* DUTY 0: low twice high */
        ccr = (clock_hz / div) + (((clock_hz % div) != 0u) ? 1u : 0u);
        ccr = ((ccr < CCR_MIN_FAST) ? CCR_MIN_FAST : ccr) | CCR_FS;
        trise = ((freq * 300u) / 1000u) + 1u; /* 300 ns */
    }
    if ((ccr & CCR_MASK) > CCR_MASK) {
        return false;
    }
    REG32(base + I2C_CR1) &= ~CR1_PE;
    REG32(base + I2C_CR2) = (REG32(base + I2C_CR2) & ~CR2_FREQ) | (freq & CR2_FREQ);
    REG32(base + I2C_CCR) = ccr;
    REG32(base + I2C_TRISE) = trise;
    REG32(base + I2C_CR1) |= CR1_PE;
    return true;
}

static void stm32_i2c_wait_stop(uintptr_t base)
{
    uint32_t n;
    for (n = 0u; n < STOP_POLL; n++) { /* bounded: until the last stop has gone out */
        if ((REG32(base + I2C_CR1) & CR1_STOP) == 0u) {
            break;
        }
    }
}

/* Acknowledges its address while a listen waits, and not otherwise. */
static void stm32_i2c_target_ack(uintptr_t base, const emb_stm32_i2c_data_t *d)
{
    if (d->listen != NULL) {
        REG32(base + I2C_OAR1) = OAR1_ALWAYS | ((uint32_t)d->listen->own << 1);
        REG32(base + I2C_CR1) |= CR1_ACK;
    } else {
        REG32(base + I2C_CR1) &= ~CR1_ACK;
    }
}

static const emb_drv_i2c_seg_t *stm32_i2c_seg(const emb_stm32_i2c_data_t *d)
{
    return &d->xfer->segs[d->seg];
}

static bool stm32_i2c_last_seg(const emb_stm32_i2c_data_t *d)
{
    return (d->seg + 1u) == d->xfer->count;
}

static void stm32_i2c_end_xfer(uintptr_t base, emb_stm32_i2c_data_t *d, emb_status_t st)
{
    emb_drv_i2c_req_t *x = d->xfer;
    d->xfer = NULL;
    REG32(base + I2C_CR1) &= ~CR1_POS;
    stm32_i2c_target_ack(base, d); /* back to waiting as a target, or to refusing its address */
    REG32(base + I2C_CR2) = (REG32(base + I2C_CR2) & ~CR2_ITBUFEN) |
                            ((d->listen != NULL) ? CR2_ITBUFEN : 0u);
    emb_drv_complete(d->dev, &x->req, st, d->moved);
}

/* A segment ended: the next one's repeated start, or the transaction's stop. */
static void stm32_i2c_next_seg(uintptr_t base, emb_stm32_i2c_data_t *d, bool stop_set)
{
    if (stm32_i2c_last_seg(d) || d->stopping) {
        if (!stop_set) {
            REG32(base + I2C_CR1) |= CR1_STOP;
        }
        stm32_i2c_end_xfer(base, d, d->stopping ? EMB_ECANCELED : EMB_OK);
        return;
    }
    d->seg++;
    d->pos = 0u;
    if (!stop_set) {
        REG32(base + I2C_CR1) |= CR1_START;
    }
    REG32(base + I2C_CR2) |= CR2_ITBUFEN;
}

static void stm32_i2c_take(uintptr_t base, emb_stm32_i2c_data_t *d)
{
    stm32_i2c_seg(d)->rd[d->pos] = (uint8_t)REG32(base + I2C_DR);
    d->pos++;
    d->moved++;
}

/* ADDR of a read segment: the acknowledge set up for its length before it is cleared. */
static void stm32_i2c_read_addr(uintptr_t base, emb_stm32_i2c_data_t *d)
{
    size_t len = stm32_i2c_seg(d)->len;
    if (len == 1u) {
        REG32(base + I2C_CR1) &= ~CR1_ACK;
        (void)REG32(base + I2C_SR2);
        /* the condition after the byte: the bus's stop, or the next segment's start */
        REG32(base + I2C_CR1) |= (stm32_i2c_last_seg(d) || d->stopping) ? CR1_STOP : CR1_START;
    } else if (len == 2u) {
        REG32(base + I2C_CR1) = (REG32(base + I2C_CR1) & ~CR1_ACK) | CR1_POS;
        (void)REG32(base + I2C_SR2);
        REG32(base + I2C_CR2) &= ~CR2_ITBUFEN; /* both bytes at BTF */
    } else {
        REG32(base + I2C_CR1) |= CR1_ACK;
        (void)REG32(base + I2C_SR2);
        if (len == 3u) {
            REG32(base + I2C_CR2) &= ~CR2_ITBUFEN; /* the last three through BTF, from the start */
        }
    }
}

static void stm32_i2c_controller_event(uintptr_t base, emb_stm32_i2c_data_t *d, uint32_t sr1)
{
    const emb_drv_i2c_seg_t *g = stm32_i2c_seg(d);
    bool rd = g->rd != NULL;
    size_t left = g->len - d->pos;
    /* a receiver's BTF has a byte in DR and one shifting in, so RXNE with it; one left over from
     * the write before a repeated start is not a byte received */
    bool btf_rx = (sr1 & (SR1_BTF | SR1_RXNE)) == (SR1_BTF | SR1_RXNE);
    if ((sr1 & SR1_SB) != 0u) {
        REG32(base + I2C_DR) = ((uint32_t)d->xfer->target->address << 1) | (rd ? 1u : 0u);
        return;
    }
    if ((sr1 & SR1_ADDR) != 0u) {
        if (rd) {
            stm32_i2c_read_addr(base, d);
        } else {
            (void)REG32(base + I2C_SR2);
        }
        return;
    }
    if (!rd) {
        if (d->stopping && (left != 0u)) {
            left = 0u; /* canceled: nothing more goes out, and the stop follows at BTF */
            REG32(base + I2C_CR2) &= ~CR2_ITBUFEN;
        }
        if (((sr1 & SR1_TXE) != 0u) && (left != 0u)) {
            REG32(base + I2C_DR) = g->wr[d->pos];
            d->pos++;
            d->moved++;
            if (d->pos == g->len) {
                REG32(base + I2C_CR2) &= ~CR2_ITBUFEN; /* the end at BTF */
            }
        } else if (((sr1 & SR1_BTF) != 0u) && (left == 0u)) {
            stm32_i2c_next_seg(base, d, false);
        } else {
            /* TXE before the byte left: the next event moves on */
        }
        return;
    }
    if (g->len == 1u) {
        if ((sr1 & SR1_RXNE) != 0u) {
            stm32_i2c_take(base, d);
            stm32_i2c_next_seg(base, d, true); /* its stop or start was set at ADDR */
        }
        return;
    }
    if (g->len == 2u) {
        if (btf_rx) {
            REG32(base + I2C_CR1) |= (stm32_i2c_last_seg(d) || d->stopping) ? CR1_STOP : CR1_START;
            stm32_i2c_take(base, d);
            stm32_i2c_take(base, d);
            REG32(base + I2C_CR1) &= ~CR1_POS;
            stm32_i2c_next_seg(base, d, true);
        }
        return;
    }
    /* three or more: RXNE while more than three remain, then the last three through BTF */
    if (((sr1 & SR1_RXNE) != 0u) && (left > 3u)) {
        stm32_i2c_take(base, d);
        if ((g->len - d->pos) == 3u) {
            REG32(base + I2C_CR2) &= ~CR2_ITBUFEN;
        }
    } else if (btf_rx && (left == 3u)) {
        REG32(base + I2C_CR1) &= ~CR1_ACK; /* N-2 in DR, N-1 shifting: N will be refused */
        stm32_i2c_take(base, d);
    } else if (btf_rx && (left == 2u)) {
        REG32(base + I2C_CR1) |= (stm32_i2c_last_seg(d) || d->stopping) ? CR1_STOP : CR1_START;
        stm32_i2c_take(base, d);
        stm32_i2c_take(base, d);
        stm32_i2c_next_seg(base, d, true);
    } else {
        /* nothing yet */
    }
}

static void stm32_i2c_end_listen(uintptr_t base, emb_stm32_i2c_data_t *d, emb_status_t st)
{
    emb_drv_i2c_req_t *l = d->listen;
    d->listen = NULL;
    stm32_i2c_target_ack(base, d); /* refused until the next listen */
    REG32(base + I2C_CR2) &= ~CR2_ITBUFEN;
    emb_drv_complete(d->dev, &l->req, st, l->req.done);
}

static void stm32_i2c_target_event(uintptr_t base, emb_stm32_i2c_data_t *d, uint32_t sr1)
{
    emb_drv_i2c_req_t *l = d->listen;
    /* a byte received first: DR is also what a transmitter sends, so the byte written before a
     * repeated start must be out of it before the read's ADDR is cleared */
    if ((sr1 & SR1_RXNE) != 0u) {
        uint8_t b = (uint8_t)REG32(base + I2C_DR);
        if ((size_t)l->req.done < l->rx_len) {
            l->rx[l->req.done] = b;
            l->req.done++;
        }
        if ((size_t)l->req.done >= l->rx_len) {
            REG32(base + I2C_CR1) &= ~CR1_ACK; /* full: the next byte is refused */
        }
    }
    if ((sr1 & SR1_ADDR) != 0u) {
        (void)REG32(base + I2C_SR2); /* cleared; TRA now says which way */
        REG32(base + I2C_CR2) |= CR2_ITBUFEN;
        return;
    }
    if (((sr1 & SR1_TXE) != 0u) && ((REG32(base + I2C_SR2) & SR2_TRA) != 0u)) {
        REG32(base + I2C_DR) = (l->sent < l->tx_len) ? l->tx[l->sent] : ONES;
        l->sent++;
        return;
    }
    if ((sr1 & SR1_STOPF) != 0u) {
        REG32(base + I2C_CR1) = REG32(base + I2C_CR1); /* after SR1's read: clears STOPF */
        stm32_i2c_end_listen(base, d, EMB_OK);
    }
}

static void stm32_i2c_ev_isr(void *arg)
{
    emb_stm32_i2c_data_t *d = (emb_stm32_i2c_data_t *)arg;
    uintptr_t base = stm32_i2c_config(d->dev)->base;
    uint32_t sr1 = REG32(base + I2C_SR1);
    if ((d->xfer != NULL) && ((sr1 & SR1_SB) != 0u || (REG32(base + I2C_SR2) & SR2_MSL) != 0u)) {
        stm32_i2c_controller_event(base, d, sr1);
    } else if (d->listen != NULL) {
        stm32_i2c_target_event(base, d, sr1);
    } else if ((sr1 & SR1_STOPF) != 0u) {
        REG32(base + I2C_CR1) = REG32(base + I2C_CR1); /* a stop nobody waits for: cleared */
    } else {
        REG32(base + I2C_CR2) &= ~CR2_ITBUFEN;
    }
}

static void stm32_i2c_er_isr(void *arg)
{
    emb_stm32_i2c_data_t *d = (emb_stm32_i2c_data_t *)arg;
    uintptr_t base = stm32_i2c_config(d->dev)->base;
    uint32_t sr1 = REG32(base + I2C_SR1);
    REG32(base + I2C_SR1) = ~(sr1 & SR1_ERRS) & 0xFFFFu; /* error flags clear by writing 0 */
    if (d->xfer != NULL && (sr1 & (SR1_AF | SR1_ARLO | SR1_BERR)) != 0u) {
        if ((sr1 & SR1_AF) != 0u) {
            /* a byte refused: the one in the shift register, and one waiting in DR if any */
            uint32_t unsent = ((REG32(base + I2C_SR1) & SR1_TXE) == 0u) ? 2u : 1u;
            if ((stm32_i2c_seg(d)->wr != NULL) && (d->pos != 0u)) {
                d->moved = (d->moved > unsent) ? (d->moved - unsent) : 0u;
            }
            REG32(base + I2C_CR1) |= CR1_STOP;
            stm32_i2c_end_xfer(base, d, EMB_ENACK);
        } else if ((sr1 & SR1_ARLO) != 0u) {
            stm32_i2c_end_xfer(base, d, EMB_EARBLOST); /* the bus is the other controller's */
        } else {
            REG32(base + I2C_CR1) |= CR1_STOP;
            stm32_i2c_end_xfer(base, d, EMB_EBUSERR);
        }
        return;
    }
    if ((d->listen != NULL) && ((sr1 & SR1_AF) != 0u)) {
        /* the controller refused the last byte it read: the read is over. The byte loaded
         * in DR after it was not sent. */
        emb_drv_i2c_req_t *l = d->listen;
        l->sent = (l->sent > 0u) ? (l->sent - 1u) : 0u;
        stm32_i2c_end_listen(base, d, EMB_OK);
    }
}

static emb_status_t stm32_i2c_init(const emb_device_t *dev)
{
    const emb_stm32_i2c_config_t *cfg = stm32_i2c_config(dev);
    emb_stm32_i2c_data_t *d = stm32_i2c_data(dev);
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit);
    REG32(cfg->base + I2C_CR1) = CR1_SWRST; /* a bus left busy by a reset mid-transaction */
    REG32(cfg->base + I2C_CR1) = 0u;
    d->xfer = NULL;
    d->listen = NULL;
    d->dev = dev;
    d->hz = 0u;
    if (emb_drv_i2c_bus_init(dev) != EMB_OK) {
        return EMB_EINVAL;
    }
    if (!stm32_i2c_timing(cfg->base, cfg->clock_hz, EMB_DRV_I2C_STANDARD)) {
        return EMB_EINVAL;
    }
    d->hz = EMB_DRV_I2C_STANDARD;
    REG32(cfg->base + I2C_CR2) |= CR2_ITEVTEN | CR2_ITERREN;
    if ((emb_irq_connect((emb_irq_t)cfg->irq_ev, stm32_i2c_ev_isr, d) != EMB_OK) ||
        (emb_irq_connect((emb_irq_t)cfg->irq_er, stm32_i2c_er_isr, d) != EMB_OK)) {
        return EMB_EINVAL;
    }
    if ((emb_irq_enable((emb_irq_t)cfg->irq_ev) != EMB_OK) ||
        (emb_irq_enable((emb_irq_t)cfg->irq_er) != EMB_OK)) {
        return EMB_EINVAL;
    }
    return EMB_OK;
}

static emb_status_t stm32_i2c_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    const emb_stm32_i2c_config_t *cfg = stm32_i2c_config(dev);
    emb_stm32_i2c_data_t *d = stm32_i2c_data(dev);
    emb_drv_i2c_req_t *x = EMB_DRV_I2C_REQ(req);
    emb_irq_key_t key;
    if (req->lane == EMB_DRV_I2C_LANE_TARGET) {
        key = emb_irq_lock();
        d->listen = x;
        if (d->xfer == NULL) {
            stm32_i2c_target_ack(cfg->base, d);
        } /* else when the transaction ends */
        emb_irq_unlock(key);
        return EMB_EBUSY;
    }
    if (x->target->hz > FAST_MAX) {
        return EMB_ENOTSUP;
    }
    stm32_i2c_wait_stop(cfg->base);
    key = emb_irq_lock();
    if ((x->target->hz != d->hz) && ((REG32(cfg->base + I2C_SR2) & SR2_BUSY) == 0u)) {
        if (!stm32_i2c_timing(cfg->base, cfg->clock_hz, x->target->hz)) {
            emb_irq_unlock(key);
            return EMB_ENOTSUP;
        }
        d->hz = x->target->hz; /* while another controller has the bus, its speed stays */
        stm32_i2c_target_ack(cfg->base, d);
    }
    d->xfer = x;
    d->seg = 0u;
    d->pos = 0u;
    d->moved = 0u;
    d->stopping = false;
    REG32(cfg->base + I2C_CR2) |= CR2_ITBUFEN;
    REG32(cfg->base + I2C_CR1) |= CR1_START; /* once the bus is free */
    emb_irq_unlock(key);
    return EMB_EBUSY;
}

/* A transaction: a write stops after the byte in progress, a read at the end of its segment,
 * which its acknowledge sequence fixes once begun; a stop ends it. A listen: at once when no
 * controller is talking to it; otherwise its stop ends it. */
static void stm32_i2c_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    const emb_stm32_i2c_config_t *cfg = stm32_i2c_config(dev);
    emb_stm32_i2c_data_t *d = stm32_i2c_data(dev);
    emb_drv_i2c_req_t *x = EMB_DRV_I2C_REQ(req);
    bool now = false;
    emb_irq_key_t key = emb_irq_lock();
    if (d->xfer == x) {
        if ((REG32(cfg->base + I2C_SR2) & SR2_MSL) == 0u) {
            /* still waiting for the bus: no start went out */
            REG32(cfg->base + I2C_CR1) &= ~CR1_START;
            d->xfer = NULL;
            stm32_i2c_target_ack(cfg->base, d);
            now = true;
        } else {
            d->stopping = true; /* the stop after the byte in progress */
        }
    } else if (d->listen == x) {
        if ((REG32(cfg->base + I2C_SR2) & (SR2_BUSY | SR2_MSL)) != SR2_BUSY) {
            d->listen = NULL;
            stm32_i2c_target_ack(cfg->base, d);
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

const emb_device_api_t emb_stm32_i2c_api = {
    stm32_i2c_init,
    stm32_i2c_start,
    stm32_i2c_stop,
    NULL,
    NULL,
    EMB_DRV_I2C_CAP_TARGET | EMB_DRV_I2C_CAP_FAST_MODE | EMB_DRV_I2C_CAP_TARGET_NACK};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_i2c_off_t;
#endif
