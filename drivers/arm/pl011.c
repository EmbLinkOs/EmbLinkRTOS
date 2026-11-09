/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Arm PrimeCell UART PL011 (Arm DDI 0183G): polled transmit, and the device of SPEC-016. */
#include <emb_pl011.h>

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define UART_DR    0x00u
#define UART_FR    0x18u
#define UART_IBRD  0x24u
#define UART_FBRD  0x28u
#define UART_LCR_H 0x2Cu
#define UART_CR    0x30u
#define FR_TXFF    (1u << 5)
#define FR_BUSY    (1u << 3)
#define LCR_8N1    0x60u  /* WLEN 8 bits, one stop bit, no parity */
#define LCR_FEN    0x10u  /* the FIFOs */
#define CR_UARTEN  0x001u /* the UART */
#define CR_ENABLE  0x301u /* UARTEN | TXE | RXE */
#define IBRD_MAX   0xFFFFu

/* One character at 115200 baud from a 150 MHz core is ~13 000 cycles, a few thousand polls. */
#define TX_BOUND 100000u

/* pico-sdk's divisor: 64ths of the 16x oversampled bit time, rounded; IBRD its upper part */
static uint32_t divisor(uint32_t clock_hz, uint32_t baud)
{
    return ((8u * clock_hz) / baud) + 1u;
}

void emb_pl011_setup(uintptr_t base, uint32_t clock_hz, uint32_t baud)
{
    uint32_t div = divisor(clock_hz, baud);
    REG32(base + UART_CR) = 0u;
    REG32(base + UART_IBRD) = div >> 7;
    REG32(base + UART_FBRD) = (div & 0x7Fu) >> 1;
    REG32(base + UART_LCR_H) = LCR_8N1 | LCR_FEN; /* also latches the divisors */
    REG32(base + UART_CR) = CR_ENABLE;
}

void emb_pl011_putc(uintptr_t base, char c)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: one character time */
        if ((REG32(base + UART_FR) & FR_TXFF) == 0u) {
            break;
        }
    }
    REG32(base + UART_DR) = (uint32_t)(unsigned char)c;
}

void emb_pl011_flush(uintptr_t base)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: the last character's frame */
        if ((REG32(base + UART_FR) & FR_BUSY) == 0u) {
            break;
        }
    }
}

#if CONFIG_EMB_DRV_UART
#include <emb/irq.h>

#define UART_IMSC 0x38u
#define UART_MIS  0x40u
#define UART_ICR  0x44u
#define FR_RXFE   (1u << 4)
#define DR_FE     (1u << 8)
#define DR_PE     (1u << 9)
#define DR_BE     (1u << 10) /* a break: the line low past a whole frame */
#define INT_RX    (1u << 4)  /* the receive FIFO at its level */
#define INT_TX    (1u << 5)  /* the transmit FIFO at or below its level */
#define INT_RT    (1u << 6)  /* the receive timeout: bytes waiting, the line idle */
#define INT_ALL   0x7FFu

static const emb_pl011_config_t *config(const emb_device_t *dev)
{
    return (const emb_pl011_config_t *)dev->config;
}

static emb_pl011_data_t *data(const emb_device_t *dev)
{
    return (emb_pl011_data_t *)dev->data;
}

/* Fills the transmit FIFO from the write in progress; true once the write is all queued. */
static bool tx_fill(uintptr_t base, emb_drv_uart_req_t *w)
{
    while ((size_t)w->req.done < w->len && (REG32(base + UART_FR) & FR_TXFF) == 0u) {
        REG32(base + UART_DR) = w->tx[w->req.done]; /* bounded by the FIFO and the write */
        w->req.done++;
    }
    return (size_t)w->req.done >= w->len;
}

static void pl011_isr(void *arg)
{
    emb_pl011_data_t *d = (emb_pl011_data_t *)arg;
    uintptr_t base = config(d->dev)->base;
    uint32_t mis = REG32(base + UART_MIS);
    REG32(base + UART_ICR) = mis;
    if ((mis & (INT_RX | INT_RT)) != 0u) {
        while ((REG32(base + UART_FR) & FR_RXFE) == 0u) { /* bounded by the FIFO's 32 */
            uint32_t dr = REG32(base + UART_DR);
            if ((dr & (DR_FE | DR_BE)) != 0u) {
                emb_drv_uart_rx_error(d->dev, &d->rx, EMB_EFRAMING);
            } else if ((dr & DR_PE) != 0u) {
                emb_drv_uart_rx_error(d->dev, &d->rx, EMB_EPARITY);
            } else {
                emb_drv_uart_rx_byte(d->dev, &d->rx, (uint8_t)dr);
            }
        }
        if ((mis & INT_RT) != 0u) {
            emb_drv_uart_rx_idle(d->dev, &d->rx);
        }
    }
    if ((mis & INT_TX) != 0u) {
        emb_drv_uart_req_t *w = d->tx;
        if (w == NULL || tx_fill(base, w)) {
            REG32(base + UART_IMSC) &= ~INT_TX;
            d->tx = NULL;
            if (w != NULL) {
                emb_drv_complete(d->dev, &w->req, EMB_OK, w->req.done);
            }
        }
    }
}

static emb_status_t uart_init(const emb_device_t *dev)
{
    const emb_pl011_config_t *cfg = config(dev);
    emb_pl011_data_t *d = data(dev);
    uint32_t div;
    if (cfg->baud == 0u) {
        return EMB_EINVAL;
    }
    div = divisor(cfg->clock_hz, cfg->baud);
    if (((div >> 7) == 0u) || ((div >> 7) > IBRD_MAX)) {
        return EMB_EINVAL; /* the divisor cannot reach the rate */
    }
    if (((REG32(cfg->base + UART_CR) & CR_UARTEN) == 0u) ||
        (REG32(cfg->base + UART_IBRD) != (div >> 7)) ||
        (REG32(cfg->base + UART_FBRD) != ((div & 0x7Fu) >> 1))) {
        emb_pl011_flush(cfg->base); /* it may be sending: let its character finish */
        emb_pl011_setup(cfg->base, cfg->clock_hz, cfg->baud);
    } /* else the board's console, already at this rate: left as the board set it */
    emb_drv_uart_rx_init(&d->rx, cfg->rx, cfg->rx_size);
    d->tx = NULL;
    d->dev = dev;
    d->irq_on = 0u;
    return (emb_irq_connect((emb_irq_t)cfg->irq, pl011_isr, d) == EMB_OK) ? EMB_OK : EMB_EINVAL;
}

/* The first request turns the interrupt on; until then the board may drive the UART. */
static void irq_on(const emb_device_t *dev)
{
    const emb_pl011_config_t *cfg = config(dev);
    emb_pl011_data_t *d = data(dev);
    if (d->irq_on != 0u) {
        return;
    }
    d->irq_on = 1u;
    REG32(cfg->base + UART_ICR) = INT_ALL;
    REG32(cfg->base + UART_IMSC) |= INT_RX | INT_RT;
    (void)emb_irq_enable((emb_irq_t)cfg->irq);
}

static emb_status_t uart_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_pl011_data_t *d = data(dev);
    emb_drv_uart_req_t *r = EMB_DRV_UART_REQ(req);
    uintptr_t base = config(dev)->base;
    emb_irq_key_t key;
    irq_on(dev);
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        return emb_drv_uart_rx_start(dev, &d->rx, r);
    }
    if (r->op == EMB_DRV_UART_OP_BREAK) {
        /* the PL011 has no timed break: BRK holds the line low until it is cleared, which
         * needs a wait start() cannot make from an interrupt. Not claimed (SPEC-016 §11). */
        return EMB_ENOTSUP;
    }
    key = emb_irq_lock();
    if (tx_fill(base, r)) {
        /* all of it in the FIFO: done now. The transmit interrupt comes only when the FIFO
         * drains past its level, which a write this short may never cross. */
        emb_irq_unlock(key);
        return EMB_OK;
    }
    d->tx = r;
    REG32(base + UART_IMSC) |= INT_TX; /* the FIFO draining past its level refills it */
    emb_irq_unlock(key);
    return EMB_EBUSY;
}

static void uart_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_pl011_data_t *d = data(dev);
    emb_irq_key_t key;
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        emb_drv_uart_rx_stop(dev, &d->rx, EMB_DRV_UART_REQ(req));
        return;
    }
    key = emb_irq_lock();
    if (d->tx == EMB_DRV_UART_REQ(req)) {
        d->tx = NULL; /* what is in the FIFO still goes out; no more is added */
        REG32(config(dev)->base + UART_IMSC) &= ~INT_TX;
    }
    emb_irq_unlock(key);
    emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
}

static emb_status_t uart_poll_write(const emb_device_t *dev, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0u; i < len; i++) { /* bounded by len */
        emb_pl011_putc(config(dev)->base, (char)buf[i]);
    }
    return EMB_OK;
}

static size_t uart_poll_read(const emb_device_t *dev, uint8_t *buf, size_t len)
{
    uintptr_t base = config(dev)->base;
    size_t n = 0u;
    while (n < len && (REG32(base + UART_FR) & FR_RXFE) == 0u) { /* bounded by len */
        buf[n] = (uint8_t)REG32(base + UART_DR);
        n++;
    }
    return n;
}

const emb_drv_uart_api_t emb_pl011_api = {
    {uart_init, uart_start, uart_stop, NULL, NULL, EMB_DRV_UART_CAP_IDLE_DETECT},
    uart_poll_write,
    uart_poll_read,
};
#endif
