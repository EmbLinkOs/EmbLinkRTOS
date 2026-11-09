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

#if CONFIG_EMB_DEVICE
static emb_status_t uart_init(const emb_device_t *dev)
{
    const emb_pl011_config_t *cfg = (const emb_pl011_config_t *)dev->config;
    uint32_t div;
    if (cfg->baud == 0u) {
        return EMB_EINVAL;
    }
    div = divisor(cfg->clock_hz, cfg->baud);
    if (((div >> 7) == 0u) || ((div >> 7) > IBRD_MAX)) {
        return EMB_EINVAL; /* the divisor cannot reach the rate */
    }
    if (((REG32(cfg->base + UART_CR) & CR_UARTEN) != 0u) &&
        (REG32(cfg->base + UART_IBRD) == (div >> 7)) &&
        (REG32(cfg->base + UART_FBRD) == ((div & 0x7Fu) >> 1))) {
        return EMB_OK; /* the board's console, already at this rate: left as the board set it */
    }
    emb_pl011_flush(cfg->base); /* it may be sending: let its character finish */
    emb_pl011_setup(cfg->base, cfg->clock_hz, cfg->baud);
    return EMB_OK;
}

const emb_device_api_t emb_pl011_api = {uart_init, NULL, NULL, NULL, NULL, 0u};
#endif
