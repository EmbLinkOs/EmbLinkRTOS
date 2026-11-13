/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_cmsdk_uart.h
 * @brief   Arm CMSDK APB UART (Arm DDI 0479C §4.6), as the MPS2 boards and QEMU's models have it.
 *
 * @details
 * A polled transmit for the board's console, and the device the generator binds to "arm,cmsdk-uart"
 * (SPEC-016 §8). The UART class's requests come with SPEC-017 §4.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_CMSDK_UART_H
#define EMB_CMSDK_UART_H

#include <emb/config.h>

#include <stdint.h>

#if CONFIG_EMB_DRV_UART
#include <emb/drv_uart.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The generated configuration of a CMSDK UART device. */
typedef struct emb_cmsdk_uart_config {
    uintptr_t base;    /* the register block */
    uint32_t clock_hz; /* PCLK */
    uint32_t baud;
    uint32_t irq_rx; /* its receive and transmit interrupt lines */
    uint32_t irq_tx;
    uint8_t *rx; /* the receive ring (rx_buffer) */
    uint32_t rx_size;
} emb_cmsdk_uart_config_t;

#if CONFIG_EMB_DRV_UART
/* A CMSDK UART device's RAM. */
typedef struct emb_cmsdk_uart_data {
    emb_drv_uart_rx_t rx;
    emb_drv_uart_req_t *tx;  /* the write in progress */
    const emb_device_t *dev; /* for the interrupt handlers */
    uint8_t irq_on;          /* the interrupts serve requests: set by the first one */
} emb_cmsdk_uart_data_t;
#endif

/* Sets the divider and enables the transmitter. BAUDDIV is at least 16. */
void emb_cmsdk_uart_setup(uintptr_t base, uint32_t clock_hz, uint32_t baud);
/* Waits for room in the transmit buffer, bounded, then writes one character. */
void emb_cmsdk_uart_putc(uintptr_t base, char c);

#if CONFIG_EMB_DRV_UART
/* The UART class's operations (SPEC-017 §4). Initialization checks the configuration, sets
 * the UART up and connects its interrupts; the first request turns them on, so a board
 * whose application drives the UART itself keeps the line as it was. */
extern const emb_drv_uart_api_t emb_cmsdk_uart_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_CMSDK_UART_H */
