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

#if CONFIG_EMB_DEVICE
static emb_status_t uart_init(const emb_device_t *dev)
{
    const emb_cmsdk_uart_config_t *cfg = (const emb_cmsdk_uart_config_t *)dev->config;
    if ((cfg->baud == 0u) || ((cfg->clock_hz / cfg->baud) < BAUDDIV_MIN)) {
        return EMB_EINVAL; /* the divider cannot reach the rate */
    }
    emb_cmsdk_uart_setup(cfg->base, cfg->clock_hz, cfg->baud);
    return EMB_OK;
}

const emb_device_api_t emb_cmsdk_uart_api = {uart_init, NULL, NULL, NULL, NULL};
#endif
