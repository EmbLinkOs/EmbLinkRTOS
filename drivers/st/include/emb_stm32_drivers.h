/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Minimal STM32F4 drivers: clock-tree bring-up, clock gates, pins and a polled USART. Register
 * offsets and bit layouts are driver knowledge (RM0390 Rev 6); every base address, gate, pin and
 * frequency comes from the board's generated hw_config.h (SPEC-014, HW-001, A69).
 */
#ifndef EMB_STM32_DRIVERS_H
#define EMB_STM32_DRIVERS_H

#include <stdint.h>

#include <emb_stm32_pins.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Brings the clock tree to EMB_HW_CPU_HZ as hw_config.h describes it: the regulator scale, the
 * oscillator, the PLL, over-drive, flash wait states, the bus dividers, then the switch. Nonzero
 * once the core runs from the requested source; zero, still on HSI, when a ready flag never
 * came. */
int emb_stm32f4_clock_init(void);

/* Sets a peripheral's clock gate: `bit` in the RCC register at `offset`, read back so the clock
 * reaches the peripheral before its first access. */
void emb_stm32_clock_enable(uint32_t offset, uint32_t bit);

/* Configures each pin of the table: its port's clock, then the alternate function, then the
 * mode. */
void emb_stm32_pins_apply(const emb_stm32_pin_t *pins, unsigned count);

/* Inverts an output pin with one BSRR write, so a concurrent change to another pin of the port
 * is not lost. */
void emb_stm32_gpio_toggle(uintptr_t port, unsigned pin);

/* 8 data bits, no parity, 1 stop bit, transmitter on, 16x oversampling; the gate must be set. */
void emb_stm32_usart_init(uintptr_t base, uint32_t pclk_hz, uint32_t baud);
void emb_stm32_usart_putc(uintptr_t base, char c);
/* Waits, bounded, until the last frame has left the shift register. */
void emb_stm32_usart_flush(uintptr_t base);

#ifdef __cplusplus
}
#endif

#endif /* EMB_STM32_DRIVERS_H */
