/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_avr_drivers.h
 * @brief   Minimal megaAVR drivers: a polled USART and port pins.
 *
 * @details
 * Register offsets and bits are driver knowledge (ATmega328P datasheet DS40002061B); every base
 * address, pin and frequency comes from the board's generated hw_config.h (SPEC-014, HW-001, A69).
 * Inline, so that the constant bases fold into single IN, OUT, LDS and STS instructions and the
 * divisor into a constant, as on an 8-bit part with 32 KiB of flash they must.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#ifndef EMB_AVR_DRIVERS_H
#define EMB_AVR_DRIVERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_AVR_REG8(a) (*(volatile uint8_t *)(uintptr_t)(a))

/* USART (§20): UCSRnA, UCSRnB, UCSRnC, -, UBRRnL, UBRRnH, UDRn from the block's base */
#define EMB_AVR_UCSRA    0u
#define EMB_AVR_UCSRB    1u
#define EMB_AVR_UCSRC    2u
#define EMB_AVR_UBRRL    4u
#define EMB_AVR_UBRRH    5u
#define EMB_AVR_UDR      6u
#define EMB_AVR_A_U2X    (1u << 1)
#define EMB_AVR_A_UDRE   (1u << 5)
#define EMB_AVR_B_TXEN   (1u << 3)
#define EMB_AVR_C_UCSZ_8 ((1u << 2) | (1u << 1))

/* The baud-rate register for double speed (U2X): f_cpu / (8 baud) - 1. */
#define EMB_AVR_UBRR_2X(f_cpu, baud) ((uint16_t)((((f_cpu) / 8UL) / (baud)) - 1UL))

/* 8 data bits, no parity, 1 stop bit, transmitter on, double speed. */
static inline void emb_avr_usart_init(uintptr_t base, uint16_t ubrr)
{
    EMB_AVR_REG8(base + EMB_AVR_UCSRA) = (uint8_t)EMB_AVR_A_U2X;
    EMB_AVR_REG8(base + EMB_AVR_UBRRH) = (uint8_t)(ubrr >> 8);
    EMB_AVR_REG8(base + EMB_AVR_UBRRL) = (uint8_t)(ubrr & 0xFFu);
    EMB_AVR_REG8(base + EMB_AVR_UCSRC) = (uint8_t)EMB_AVR_C_UCSZ_8;
    EMB_AVR_REG8(base + EMB_AVR_UCSRB) = (uint8_t)EMB_AVR_B_TXEN;
}

/* Waits, bounded by one character time, for the data register, then writes it. */
static inline void emb_avr_usart_putc(uintptr_t base, char c)
{
    while ((EMB_AVR_REG8(base + EMB_AVR_UCSRA) & (uint8_t)EMB_AVR_A_UDRE) == 0u) {
    }
    EMB_AVR_REG8(base + EMB_AVR_UDR) = (uint8_t)c;
}

/* Port pins (§14): PINx, DDRx and PORTx at the port's base, +1 and +2. */
static inline void emb_avr_gpio_output(uintptr_t port, unsigned pin)
{
    EMB_AVR_REG8(port + 1u) |= (uint8_t)(1u << pin);
}

static inline void emb_avr_gpio_toggle(uintptr_t port, unsigned pin)
{
    EMB_AVR_REG8(port + 2u) ^= (uint8_t)(1u << pin);
}

#ifdef __cplusplus
}
#endif

#endif /* EMB_AVR_DRIVERS_H */
