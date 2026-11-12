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
 * Each GPIO line the board uses, its IO_BANK0 function select, and whether its pad's input is
 * enabled.
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
    uint8_t funcsel;  /* IO_BANK0 GPIOn_CTRL.FUNCSEL: 2 UART, 5 SIO, 11 UART_AUX, ... */
    uint8_t input_en; /* PADS_BANK0 GPIOn.IE */
} emb_rp2350_pin_t;

extern const emb_rp2350_pin_t emb_board_pins[];
extern const unsigned emb_board_pin_count;

#ifdef __cplusplus
}
#endif

#endif /* EMB_RP2350_PINS_H */
