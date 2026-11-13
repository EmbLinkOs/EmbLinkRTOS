/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_board.h
 * @brief   The host as a board: console on stdout, a simulated LED (SPEC-013).
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#ifndef EMB_BOARD_H
#define EMB_BOARD_H

#include <emb/compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_HW_BOARD_NAME "native"

void emb_board_init(void);
void emb_board_led_toggle(void);
void emb_board_puts(const char *s);
void emb_board_put_u32(uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* EMB_BOARD_H */
