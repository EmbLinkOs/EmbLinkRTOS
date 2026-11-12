/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_stm32_drivers.h
 * @brief   Minimal STM32F4 drivers: clock-tree bring-up, clock gates, pins and a polled USART.
 *
 * @details
 * Register offsets and bit layouts are driver knowledge (RM0390 Rev 6); every base address, gate,
 * pin and frequency comes from the board's generated hw_config.h (SPEC-014, HW-001, A69).
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
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
/* Drive an output pin high or low with one BSRR write. */
void emb_stm32_gpio_set(uintptr_t port, unsigned pin);
void emb_stm32_gpio_clear(uintptr_t port, unsigned pin);

/* 8 data bits, no parity, 1 stop bit, transmitter on, 16x oversampling; the gate must be set. */
void emb_stm32_usart_init(uintptr_t base, uint32_t pclk_hz, uint32_t baud);
void emb_stm32_usart_putc(uintptr_t base, char c);
/* Waits, bounded, until the last frame has left the shift register. */
void emb_stm32_usart_flush(uintptr_t base);

/* Receive by interrupt: RE and RXNEIE on. In the USART's handler, emb_stm32_usart_getc_isr()
 * returns the received byte, or -1 when there is none. */
void emb_stm32_usart_rx_irq_enable(uintptr_t base);
int emb_stm32_usart_getc_isr(uintptr_t base);

/* The last reset's cause (RCC_CSR), then cleared: EMB_STM32F4_RESET_* bits. */
#define EMB_STM32F4_RESET_BOR  (1u << 25)
#define EMB_STM32F4_RESET_PIN  (1u << 26)
#define EMB_STM32F4_RESET_POR  (1u << 27)
#define EMB_STM32F4_RESET_SOFT (1u << 28)
#define EMB_STM32F4_RESET_IWDG (1u << 29)
#define EMB_STM32F4_RESET_WWDG (1u << 30)
#define EMB_STM32F4_RESET_LPWR (1u << 31)
#define EMB_STM32F4_RESET_MASK (0x7Fu << 25) /* bits 25 to 31 */
uint32_t emb_stm32f4_reset_flags_take(void);

/* A general-purpose timer's input clock from its APB bus's clock and divider (RM0390 §6.2). */
uint32_t emb_stm32_tim_clock_hz(uint32_t pclk_hz, uint32_t apb_div);
/* Counts at 1 MHz and raises TRGO at @rate_hz from its update event; 0 when @rate_hz or the
 * clock does not fit. The gate must be set. */
int emb_stm32_tim_trgo_periodic(uintptr_t base, uint32_t tim_clk_hz, uint32_t rate_hz);
void emb_stm32_tim_stop(uintptr_t base);
/* Channel 1 toggles its pin at each update event: the trigger made visible, with no software in
 * the path. The pin's alternate function must route TIMx_CH1. */
void emb_stm32_tim_ch1_toggle_on_update(uintptr_t base);

/* ADC1 converts the temperature sensor on each TIM2 TRGO, end-of-conversion and overrun
 * interrupts on. The gate must be set. */
void emb_stm32f4_adc_temp_on_tim2(uintptr_t adc, uintptr_t common);
/* In ADC1's handler: 1 with the reading in *out, 0 with none, -1 for a lost conversion. */
int emb_stm32f4_adc_take(uintptr_t adc, uint16_t *out);
/* A temperature-sensor reading in thousandths of a degree Celsius, from the factory
 * calibration block (VDDA = 3.3 V); INT32_MIN when the block holds no calibration. */
int32_t emb_stm32f4_temp_mc(uint16_t raw, uintptr_t factory_cal);

/* The independent watchdog: start it with a timeout of up to 4095 ms; it cannot be stopped. */
void emb_stm32f4_iwdg_start(uintptr_t base, uint32_t timeout_ms);
void emb_stm32f4_iwdg_kick(uintptr_t base);

#ifdef __cplusplus
}
#endif

#endif /* EMB_STM32_DRIVERS_H */
