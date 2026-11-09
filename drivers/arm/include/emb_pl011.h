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

#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The generated configuration of a PL011 device. */
typedef struct emb_pl011_config {
    uintptr_t base;    /* the register block */
    uint32_t clock_hz; /* UARTCLK */
    uint32_t baud;
} emb_pl011_config_t;

/* Sets the divisors for the rate, 8 data bits, no parity, one stop bit, the FIFOs on, and
 * enables the transmitter and receiver. */
void emb_pl011_setup(uintptr_t base, uint32_t clock_hz, uint32_t baud);
/* Waits for room in the transmit FIFO, bounded, then writes one character. */
void emb_pl011_putc(uintptr_t base, char c);
/* Waits, bounded, until the last character has left the line. */
void emb_pl011_flush(uintptr_t base);

#if CONFIG_EMB_DEVICE
/* The device's operations: initialization checks the divisor; a UART already running at the
 * rate, as the board's console is, is left as it is, and another is set up once idle. */
extern const emb_device_api_t emb_pl011_api;
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMB_PL011_H */
