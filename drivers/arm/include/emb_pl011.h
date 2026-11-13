/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_pl011.h
 * @brief   Arm PrimeCell UART PL011 (Arm DDI 0183G), as the RP2350 has it.
 *
 * @details
 * A polled transmit for the board's console, and the device the generator binds to "arm,pl011"
 * (SPEC-016 §8). The UART class's requests come with SPEC-017 §4.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#ifndef EMB_PL011_H
#define EMB_PL011_H

#include <emb/config.h>
#include <emb/drv_dma.h>

#include <stdint.h>

#if CONFIG_EMB_DRV_UART
#include <emb/drv_uart.h>
#endif
#if CONFIG_EMB_DRV_UART && CONFIG_EMB_TIMER
#include <emb/timer.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The generated configuration of a PL011 device. */
typedef struct emb_pl011_config {
    uintptr_t base;    /* the register block */
    uint32_t clock_hz; /* UARTCLK */
    uint32_t baud;
    uint32_t irq;
    uint8_t *rx; /* the receive ring (rx_buffer) */
    uint32_t rx_size;
    emb_drv_dma_chan_t dma_tx; /* writes by DMA, where the board gives it one (dma: [tx]) */
} emb_pl011_config_t;

#if CONFIG_EMB_DRV_UART
/* A PL011 device's RAM. */
typedef struct emb_pl011_data {
    emb_drv_uart_rx_t rx;
    emb_drv_uart_req_t *tx;  /* the write in progress */
    const emb_device_t *dev; /* for the interrupt handler */
    uint8_t irq_on;          /* the interrupt serves requests: set by the first one */
    uint8_t dma;             /* the write in progress runs by DMA */
#if CONFIG_EMB_TIMER
    /* A break is timed by a software timer whose callback runs in its expiry interrupt:
     * the PL011's BRK holds the line low until cleared, and start() cannot wait. */
    uint8_t brk_phase;           /* 0 none, 1 waiting for the line, 2 the line held low */
    emb_drv_uart_req_t *brk_req; /* the break in progress */
    emb_timer_storage_t brk_storage;
    emb_timer_t brk_timer;
#endif
} emb_pl011_data_t;
#endif

/* Sets the divisors for the rate, 8 data bits, no parity, one stop bit, the FIFOs on, and
 * enables the transmitter and receiver. */
void emb_pl011_setup(uintptr_t base, uint32_t clock_hz, uint32_t baud);
/* Waits for room in the transmit FIFO, bounded, then writes one character. */
void emb_pl011_putc(uintptr_t base, char c);
/* Waits, bounded, until the last character has left the line. */
void emb_pl011_flush(uintptr_t base);

#if CONFIG_EMB_DRV_UART
/* The UART class's operations (SPEC-017 §4). Initialization checks the divisor, leaves a UART
 * already running at the rate as it is (the board's console) and connects the interrupt;
 * the first request turns it on. The receive timeout is the idle line. A break, with
 * CONFIG_EMB_TIMER, holds the line low for two character times and at least 1 ms. */
extern const emb_drv_uart_api_t emb_pl011_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_PL011_H */
