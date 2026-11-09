/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Arm PrimeCell UART PL011 (Arm DDI 0183G), as the RP2350 has it: a polled transmit for the
 * board's console, and the device the generator binds to "arm,pl011" (SPEC-016 §8). The UART
 * class's requests come with SPEC-017 §4.
 */
#ifndef EMB_PL011_H
#define EMB_PL011_H

#include <emb/config.h>

#include <stdint.h>

#if CONFIG_EMB_DRV_UART
#include <emb/drv_uart.h>
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
} emb_pl011_config_t;

#if CONFIG_EMB_DRV_UART
/* A PL011 device's RAM. */
typedef struct emb_pl011_data {
    emb_drv_uart_rx_t rx;
    emb_drv_uart_req_t *tx;  /* the write in progress */
    const emb_device_t *dev; /* for the interrupt handler */
    uint8_t irq_on;          /* the interrupt serves requests: set by the first one */
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
 * the first request turns it on. The receive timeout is the idle line. */
extern const emb_drv_uart_api_t emb_pl011_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_PL011_H */
