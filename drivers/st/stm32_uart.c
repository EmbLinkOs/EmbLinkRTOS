/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The STM32 USART as a device of the UART class (SPEC-017 §4; RM0390 Rev 6 §25): writes sent
 * by the transmit-empty interrupt, bytes received into the class's receive core, the idle
 * line, framing and parity errors. The free functions of emb_stm32_drivers.h stay its
 * polled low layer.
 */
#include <emb_stm32_devices.h>
#include <emb_stm32_drivers.h>

#if CONFIG_EMB_DRV_UART
#include <emb/irq.h>

#define REG32(a)         (*(volatile uint32_t *)(uintptr_t)(a))
#define USART_SR         0x00u
#define USART_DR         0x04u
#define USART_BRR        0x08u
#define USART_CR1        0x0Cu
#define SR_PE            (1u << 0)
#define SR_FE            (1u << 1)
#define SR_NF            (1u << 2)
#define SR_ORE           (1u << 3)
#define SR_IDLE          (1u << 4)
#define SR_RXNE          (1u << 5)
#define SR_TXE           (1u << 7)
#define CR1_SBK          (1u << 0)
#define CR1_RE           (1u << 2)
#define CR1_IDLEIE       (1u << 4)
#define CR1_RXNEIE       (1u << 5)
#define CR1_TXEIE        (1u << 7)
#define CR1_PEIE         (1u << 8)
#define CR1_UE           (1u << 13)
#define USART_OVERSAMPLE 16u /* the smallest divider: BRR's mantissa is at least 1 */

static const emb_stm32_usart_config_t *config(const emb_device_t *dev)
{
    return (const emb_stm32_usart_config_t *)dev->config;
}

static emb_stm32_usart_data_t *data(const emb_device_t *dev)
{
    return (emb_stm32_usart_data_t *)dev->data;
}

static void stm32_usart_isr(void *arg)
{
    emb_stm32_usart_data_t *d = (emb_stm32_usart_data_t *)arg;
    uintptr_t base = config(d->dev)->base;
    uint32_t sr = REG32(base + USART_SR);
    if ((sr & (SR_RXNE | SR_ORE | SR_FE | SR_PE | SR_NF | SR_IDLE)) != 0u) {
        /* reading SR then DR clears RXNE and the error and idle flags (RM0390 §25.6.1) */
        uint8_t byte = (uint8_t)REG32(base + USART_DR);
        if ((sr & SR_FE) != 0u) {
            emb_drv_uart_rx_error(d->dev, &d->rx, EMB_EFRAMING);
        } else if ((sr & SR_PE) != 0u) {
            emb_drv_uart_rx_error(d->dev, &d->rx, EMB_EPARITY);
        } else if ((sr & SR_RXNE) != 0u) {
            emb_drv_uart_rx_byte(d->dev, &d->rx, byte);
        } else {
            /* the idle line or an overrun alone: no byte to keep */
        }
        if ((sr & SR_IDLE) != 0u) {
            emb_drv_uart_rx_idle(d->dev, &d->rx);
        }
    }
    if ((sr & SR_TXE) != 0u && (REG32(base + USART_CR1) & CR1_TXEIE) != 0u) {
        emb_drv_uart_req_t *w = d->tx;
        if (w != NULL && (size_t)w->req.done < w->len) {
            REG32(base + USART_DR) = w->tx[w->req.done];
            w->req.done++;
        } else {
            REG32(base + USART_CR1) &= ~CR1_TXEIE;
            d->tx = NULL;
            if (w != NULL) {
                emb_drv_complete(d->dev, &w->req, EMB_OK, w->req.done);
            }
        }
    }
}

static emb_status_t usart_init(const emb_device_t *dev)
{
    const emb_stm32_usart_config_t *cfg = config(dev);
    emb_stm32_usart_data_t *d = data(dev);
    if ((cfg->baud == 0u) || ((cfg->clock_hz / cfg->baud) < USART_OVERSAMPLE)) {
        return EMB_EINVAL; /* the divider cannot reach the rate */
    }
    emb_stm32_clock_enable(cfg->clken_offset, cfg->clken_bit);
    if (((REG32(cfg->base + USART_CR1) & CR1_UE) == 0u) ||
        (REG32(cfg->base + USART_BRR) != ((cfg->clock_hz + (cfg->baud / 2u)) / cfg->baud))) {
        emb_stm32_usart_flush(cfg->base); /* it may be sending: let its character finish */
        emb_stm32_usart_init(cfg->base, cfg->clock_hz, cfg->baud);
    } /* else the board's console, already at this rate: left as the board set it */
    REG32(cfg->base + USART_CR1) |= CR1_RE;
    emb_drv_uart_rx_init(&d->rx, cfg->rx, cfg->rx_size);
    d->tx = NULL;
    d->dev = dev;
    d->irq_on = 0u;
    return (emb_irq_connect((emb_irq_t)cfg->irq, stm32_usart_isr, d) == EMB_OK) ? EMB_OK
                                                                                : EMB_EINVAL;
}

/* The first request turns the interrupt on; until then an application may drive the USART. */
static void irq_on(const emb_device_t *dev)
{
    const emb_stm32_usart_config_t *cfg = config(dev);
    emb_stm32_usart_data_t *d = data(dev);
    if (d->irq_on != 0u) {
        return;
    }
    d->irq_on = 1u;
    (void)REG32(cfg->base + USART_SR); /* a stale idle or overrun flag cleared first */
    (void)REG32(cfg->base + USART_DR);
    REG32(cfg->base + USART_CR1) |= CR1_RXNEIE | CR1_IDLEIE | CR1_PEIE;
    (void)emb_irq_enable((emb_irq_t)cfg->irq);
}

static emb_status_t usart_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_stm32_usart_data_t *d = data(dev);
    emb_drv_uart_req_t *r = EMB_DRV_UART_REQ(req);
    emb_irq_key_t key;
    irq_on(dev);
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        return emb_drv_uart_rx_start(dev, &d->rx, r);
    }
    if (r->op == EMB_DRV_UART_OP_BREAK) {
        REG32(config(dev)->base + USART_CR1) |= CR1_SBK; /* one break frame after the data */
        return EMB_OK;
    }
    if (r->len == 0u) {
        return EMB_OK; /* nothing to send */
    }
    key = emb_irq_lock();
    d->tx = r;
    REG32(config(dev)->base + USART_CR1) |= CR1_TXEIE; /* each empty data register: a byte */
    emb_irq_unlock(key);
    return EMB_EBUSY; /* the interrupt sends the bytes and ends it */
}

static void usart_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_stm32_usart_data_t *d = data(dev);
    emb_irq_key_t key;
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        emb_drv_uart_rx_stop(dev, &d->rx, EMB_DRV_UART_REQ(req));
        return;
    }
    key = emb_irq_lock();
    if (d->tx == EMB_DRV_UART_REQ(req)) {
        d->tx = NULL; /* the byte in the shift register finishes; no other follows */
        REG32(config(dev)->base + USART_CR1) &= ~CR1_TXEIE;
    }
    emb_irq_unlock(key);
    emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
}

static emb_status_t usart_poll_write(const emb_device_t *dev, const uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0u; i < len; i++) { /* bounded by len */
        emb_stm32_usart_putc(config(dev)->base, (char)buf[i]);
    }
    return EMB_OK;
}

static size_t usart_poll_read(const emb_device_t *dev, uint8_t *buf, size_t len)
{
    uintptr_t base = config(dev)->base;
    size_t n = 0u;
    while (n < len && (REG32(base + USART_SR) & SR_RXNE) != 0u) { /* bounded by len */
        buf[n] = (uint8_t)REG32(base + USART_DR);
        n++;
    }
    return n;
}

const emb_drv_uart_api_t emb_stm32_usart_api = {
    {usart_init, usart_start, usart_stop, NULL, NULL,
     EMB_DRV_UART_CAP_IDLE_DETECT | EMB_DRV_UART_CAP_BREAK},
    usart_poll_write,
    usart_poll_read,
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_uart_off_t;
#endif
