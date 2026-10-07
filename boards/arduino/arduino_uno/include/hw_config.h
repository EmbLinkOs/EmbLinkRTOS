/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Hardware configuration of the arduino_uno board (ATmega328P at 16 MHz). Hand-written
 * for M1 from board.yaml and the ATmega328P datasheet; emb-hwgen generates this file
 * from M3 (SPEC-014, HW-005). Vector numbers are the datasheet's, zero-based.
 */
#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#define EMB_HW_BOARD "arduino_uno"
#define EMB_HW_SOC   "atmega328p"
#define EMB_HW_F_CPU 16000000UL

#define EMB_VECTOR_RESET        0
#define EMB_VECTOR_INT0         1
#define EMB_VECTOR_INT1         2
#define EMB_VECTOR_PCINT0       3
#define EMB_VECTOR_PCINT1       4
#define EMB_VECTOR_PCINT2       5
#define EMB_VECTOR_WDT          6
#define EMB_VECTOR_TIMER2_COMPA 7
#define EMB_VECTOR_TIMER2_COMPB 8
#define EMB_VECTOR_TIMER2_OVF   9
#define EMB_VECTOR_TIMER1_CAPT  10
#define EMB_VECTOR_TIMER1_COMPA 11
#define EMB_VECTOR_TIMER1_COMPB 12
#define EMB_VECTOR_TIMER1_OVF   13
#define EMB_VECTOR_TIMER0_COMPA 14
#define EMB_VECTOR_TIMER0_COMPB 15
#define EMB_VECTOR_TIMER0_OVF   16
#define EMB_VECTOR_SPI_STC      17
#define EMB_VECTOR_USART_RX     18
#define EMB_VECTOR_USART_UDRE   19
#define EMB_VECTOR_USART_TX     20
#define EMB_VECTOR_ADC          21
#define EMB_VECTOR_EE_READY     22
#define EMB_VECTOR_ANALOG_COMP  23
#define EMB_VECTOR_TWI          24
#define EMB_VECTOR_SPM_READY    25

#define EMB_HW_SRAM_BASE      0x0100u
#define EMB_HW_SRAM_SIZE      2048u
#define EMB_HW_FLASH_APP_SIZE 32256u

#define EMB_HW_CONSOLE_BAUD 115200UL
#define EMB_HW_LED_PORT     PORTB
#define EMB_HW_LED_DDR      DDRB
#define EMB_HW_LED_BIT      5

#endif /* HW_CONFIG_H */
