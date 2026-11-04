/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The pin table emb-hwgen writes into a board's generated/board_pins.c (SPEC-014 §6). */
#ifndef EMB_STM32_PINS_H
#define EMB_STM32_PINS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_STM32_PIN_OUTPUT 1u /* GPIOx_MODER: general-purpose output */
#define EMB_STM32_PIN_AF     2u /* GPIOx_MODER: alternate function, AFRL/AFRH selects which */

typedef struct {
    uintptr_t port_base; /* the GPIO port's register block */
    uint8_t clken_bit;   /* the port's bit in RCC_AHB1ENR */
    uint8_t pin;         /* 0 to 15 */
    uint8_t mode;        /* EMB_STM32_PIN_OUTPUT or EMB_STM32_PIN_AF */
    uint8_t af;          /* the alternate function, 0 to 15; 0 for an output */
} emb_stm32_pin_t;

extern const emb_stm32_pin_t emb_board_pins[];
extern const unsigned emb_board_pin_count;

#ifdef __cplusplus
}
#endif

#endif /* EMB_STM32_PINS_H */
