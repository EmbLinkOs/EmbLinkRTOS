/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The host as a board: console on stdout, a simulated LED (SPEC-013). */
#ifndef EMB_BOARD_H
#define EMB_BOARD_H

#include <emb/compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

void emb_board_init(void);
void emb_board_led_toggle(void);
void emb_board_puts(const char *s);
void emb_board_put_u32(uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* EMB_BOARD_H */
