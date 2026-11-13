/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_stm32_pins.h
 * @brief   The pin table emb-hwgen writes into a board's generated/src/board_pins.c (SPEC-014 §6).
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#ifndef EMB_STM32_PINS_H
#define EMB_STM32_PINS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_STM32_PIN_OUTPUT 1u /* GPIOx_MODER: general-purpose output */
#define EMB_STM32_PIN_AF     2u /* GPIOx_MODER: alternate function, AFRL/AFRH selects which */

/* A pin's flags: an I2C line is open drain, pulled up where the board has no resistors. */
#define EMB_STM32_PIN_OPEN_DRAIN 0x01u /* GPIOx_OTYPER */
#define EMB_STM32_PIN_PULL_UP    0x02u /* GPIOx_PUPDR */
#define EMB_STM32_PIN_I2C        (EMB_STM32_PIN_OPEN_DRAIN | EMB_STM32_PIN_PULL_UP)

/* A port's three table fields from hw_config.h: EMB_STM32_PORT(A) for GPIOA. */
#define EMB_STM32_PORT(p) \
    EMB_HW_GPIO##p##_BASE, EMB_HW_GPIO##p##_CLKEN_OFFSET, EMB_HW_GPIO##p##_CLKEN_BIT

typedef struct {
    uintptr_t port_base;  /* the GPIO port's register block */
    uint8_t clken_offset; /* the RCC clock-enable register holding the port's gate */
    uint8_t clken_bit;    /* the port's bit in it */
    uint8_t pin;          /* 0 to 15 */
    uint8_t mode;         /* EMB_STM32_PIN_OUTPUT or EMB_STM32_PIN_AF */
    uint8_t af;           /* the alternate function, 0 to 15; 0 for an output */
    uint8_t level;        /* an output's level at boot: 1 for a chip select, deasserted */
    uint8_t flags;        /* EMB_STM32_PIN_OPEN_DRAIN, EMB_STM32_PIN_PULL_UP: an I2C line's */
} emb_stm32_pin_t;

extern const emb_stm32_pin_t emb_board_pins[];
extern const unsigned emb_board_pin_count;

#ifdef __cplusplus
}
#endif

#endif /* EMB_STM32_PINS_H */
