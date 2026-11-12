/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Arm CMSDK APB UART (Arm DDI 0479C §4.6): polled transmit, and the device of SPEC-016. */
#include <emb_cmsdk_uart.h>

#define REG32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define UART_DATA     0x00u
#define UART_STATE    0x04u
#define UART_CTRL     0x08u
#define UART_BAUDDIV  0x10u
#define STATE_TX_FULL 1u
#define CTRL_TX_EN    1u
#define BAUDDIV_MIN   16u

/* One character at 115200 baud from a 25 MHz PCLK is ~2 200 cycles, a few hundred polls. */
#define TX_BOUND 100000u

void emb_cmsdk_uart_setup(uintptr_t base, uint32_t clock_hz, uint32_t baud)
{
    REG32(base + UART_BAUDDIV) = clock_hz / baud;
    REG32(base + UART_CTRL) = CTRL_TX_EN;
}

void emb_cmsdk_uart_putc(uintptr_t base, char c)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: one character time */
        if ((REG32(base + UART_STATE) & STATE_TX_FULL) == 0u) {
            break;
        }
    }
    REG32(base + UART_DATA) = (uint32_t)(unsigned char)c;
}

#if CONFIG_EMB_DRV_UART
#include <emb/irq.h>

#define STATE_RX_FULL  2u
#define CTRL_RX_EN     2u
#define CTRL_TX_INT_EN 4u
#define CTRL_RX_INT_EN 8u
#define UART_INTSTATUS 0x0Cu /* write ones to clear */
#define INT_TX         1u
#define INT_RX         2u

static const emb_cmsdk_uart_config_t *config(const emb_device_t *dev)
{
    return (const emb_cmsdk_uart_config_t *)dev->config;
}

static emb_cmsdk_uart_data_t *data(const emb_device_t *dev)
{
    return (emb_cmsdk_uart_data_t *)dev->data;
}

/* Both of the UART's lines run this one handler, which serves whatever INTSTATUS says: QEMU
 * versions differ on which of the two lines carries receive and which transmit, and a
 * handler that served only its own would leave the other's status set and the line firing. */
static void cmsdk_isr(void *arg)
{
    emb_cmsdk_uart_data_t *d = (emb_cmsdk_uart_data_t *)arg;
    uintptr_t base = config(d->dev)->base;
    uint32_t status = REG32(base + UART_INTSTATUS);
    REG32(base + UART_INTSTATUS) = status & (INT_RX | INT_TX);
    while ((REG32(base + UART_STATE) & STATE_RX_FULL) != 0u) { /* bounded: one byte buffer */
        emb_drv_uart_rx_byte(d->dev, &d->rx, (uint8_t)REG32(base + UART_DATA));
    }
    if ((status & INT_TX) != 0u) {
        emb_drv_uart_req_t *w = d->tx; /* a byte has left: the next one, or the write's end */
        if (w == NULL) {
            REG32(base + UART_CTRL) &= ~CTRL_TX_INT_EN;
        } else if ((size_t)w->req.done < w->len) {
            REG32(base + UART_DATA) = w->tx[w->req.done];
            w->req.done++;
        } else {
            d->tx = NULL;
            REG32(base + UART_CTRL) &= ~CTRL_TX_INT_EN;
            emb_drv_complete(d->dev, &w->req, EMB_OK, w->req.done);
        }
    }
}

static emb_status_t uart_init(const emb_device_t *dev)
{
    const emb_cmsdk_uart_config_t *cfg = config(dev);
    emb_cmsdk_uart_data_t *d = data(dev);
    if ((cfg->baud == 0u) || ((cfg->clock_hz / cfg->baud) < BAUDDIV_MIN)) {
        return EMB_EINVAL; /* the divider cannot reach the rate */
    }
    emb_cmsdk_uart_setup(cfg->base, cfg->clock_hz, cfg->baud);
    REG32(cfg->base + UART_CTRL) |= CTRL_RX_EN;
    emb_drv_uart_rx_init(&d->rx, cfg->rx, cfg->rx_size);
    d->tx = NULL;
    d->dev = dev;
    d->irq_on = 0u;
    if ((emb_irq_connect((emb_irq_t)cfg->irq_rx, cmsdk_isr, d) != EMB_OK) ||
        (emb_irq_connect((emb_irq_t)cfg->irq_tx, cmsdk_isr, d) != EMB_OK)) {
        return EMB_EINVAL;
    }
    return EMB_OK;
}

/* The first request turns the interrupts on; until then the board may drive the UART. */
static void irq_on(const emb_device_t *dev)
{
    const emb_cmsdk_uart_config_t *cfg = config(dev);
    emb_cmsdk_uart_data_t *d = data(dev);
    if (d->irq_on != 0u) {
        return;
    }
    d->irq_on = 1u;
    REG32(cfg->base + UART_CTRL) |= CTRL_RX_INT_EN;
    (void)emb_irq_enable((emb_irq_t)cfg->irq_rx);
    (void)emb_irq_enable((emb_irq_t)cfg->irq_tx);
}

static emb_status_t uart_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_cmsdk_uart_data_t *d = data(dev);
    emb_drv_uart_req_t *r = EMB_DRV_UART_REQ(req);
    uintptr_t base = config(dev)->base;
    emb_irq_key_t key;
    irq_on(dev);
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        return emb_drv_uart_rx_start(dev, &d->rx, r);
    }
    if (r->op == EMB_DRV_UART_OP_BREAK) {
        return EMB_ENOTSUP; /* the CMSDK UART has no break */
    }
    if (r->len == 0u) {
        return EMB_OK; /* nothing to send */
    }
    /* the state is complete before the first byte can end and its interrupt run */
    key = emb_irq_lock();
    d->tx = r;
    req->done = 1u;
    REG32(base + UART_CTRL) |= CTRL_TX_INT_EN; /* each byte's end sends the next */
    emb_cmsdk_uart_putc(base, (char)r->tx[0]);
    emb_irq_unlock(key);
    return EMB_EBUSY; /* the transmit interrupt sends the rest and ends it */
}

static void uart_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_cmsdk_uart_data_t *d = data(dev);
    emb_irq_key_t key;
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        emb_drv_uart_rx_stop(dev, &d->rx, EMB_DRV_UART_REQ(req));
        return;
    }
    key = emb_irq_lock();
    if (d->tx == EMB_DRV_UART_REQ(req)) {
        d->tx = NULL; /* the byte on the line finishes; no other follows */
        REG32(config(dev)->base + UART_CTRL) &= ~CTRL_TX_INT_EN;
    }
    emb_irq_unlock(key);
    emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
}

static emb_status_t uart_poll_write(const emb_device_t *dev, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0u; i < len; i++) { /* bounded by len */
        emb_cmsdk_uart_putc(config(dev)->base, (char)buf[i]);
    }
    return EMB_OK;
}

static size_t uart_poll_read(const emb_device_t *dev, uint8_t *buf, size_t len)
{
    uintptr_t base = config(dev)->base;
    size_t n = 0u;
    while (n < len && (REG32(base + UART_STATE) & STATE_RX_FULL) != 0u) { /* bounded by len */
        buf[n] = (uint8_t)REG32(base + UART_DATA);
        n++;
    }
    return n;
}

const emb_drv_uart_api_t emb_cmsdk_uart_api = {
    {uart_init, uart_start, uart_stop, NULL, NULL, 0u},
    uart_poll_write,
    uart_poll_read,
};
#endif
