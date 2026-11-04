/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The RP2350 pin table a board's generated board_pins.c defines (A79): each GPIO line the
 * board uses, its IO_BANK0 function select, and whether its pad's input is enabled.
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
