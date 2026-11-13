/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_rp2350_pins.h
 * @brief   The RP2350 pin table a board's generated board_pins.c defines (A79).
 *
 * @details
 * Each GPIO line the board uses, its IO_BANK0 function select, whether its pad's input is enabled,
 * and its pull.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#ifndef EMB_RP2350_PINS_H
#define EMB_RP2350_PINS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emb_rp2350_pin {
    uint8_t gpio;     /* GPIO line, 0 to 29 on the RP2350A */
    uint8_t funcsel;  /* IO_BANK0 GPIOn_CTRL.FUNCSEL: 2 UART, 4 PWM, 5 SIO, 11 UART_AUX, 31 none */
    uint8_t input_en; /* PADS_BANK0 GPIOn.IE */
    uint8_t pull;     /* EMB_RP2350_PULL_*: the pad's reset pull-down, or a pull-up */
    uint8_t out;      /* EMB_RP2350_OUT_*: an SIO output and its level at boot */
} emb_rp2350_pin_t;

#define EMB_RP2350_PULL_RESET 0u /* as the pad comes out of reset: pulled down */
#define EMB_RP2350_PULL_UP    1u /* a line that idles high: a UART's receive, I2C */
#define EMB_RP2350_PULL_NONE  2u /* neither: an analog input (A129) */

#define EMB_RP2350_OUT_NONE 0u /* a peripheral's pin: its function drives it */
#define EMB_RP2350_OUT_LOW  1u /* an SIO output starting low (an LED) */
#define EMB_RP2350_OUT_HIGH 2u /* an SIO output starting high (a chip select, deasserted) */

extern const emb_rp2350_pin_t emb_board_pins[];
extern const unsigned emb_board_pin_count;

#ifdef __cplusplus
}
#endif

#endif /* EMB_RP2350_PINS_H */
