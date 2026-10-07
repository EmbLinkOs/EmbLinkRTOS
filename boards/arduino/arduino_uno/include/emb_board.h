/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* arduino_uno board interface: console on USART0, user LED on PB5 (SPEC-012 §11). */
#ifndef EMB_BOARD_H
#define EMB_BOARD_H

#include <emb/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_HW_BOARD_NAME "arduino_uno"

void emb_board_init(void);
void emb_board_led_toggle(void);
void emb_board_puts(const char *s);
void emb_board_put_u32(uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* EMB_BOARD_H */
