/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    emb_board.h
 * @brief   The board interface of every ATmega board described in YAML.
 *
 * @details
 * The console and the first LED are the ones the board's generated hw_config.h names (SPEC-012 §11,
 * SPEC-014).
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#ifndef EMB_BOARD_H
#define EMB_BOARD_H

#include <emb/compiler.h>

#include <stddef.h>
#include <stdint.h>

#include <hw_config.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_HW_BOARD_NAME EMB_HW_BOARD

void emb_board_init(void);
void emb_board_led_toggle(void);
void emb_board_puts(const char *s);
void emb_board_write(const void *buf, size_t len); /* raw bytes (A127) */
void emb_board_put_u32(uint32_t v);
/* The end of an image (a benchmark's, after BENCH_END): no host to report the code to, so the
 * CPU stops with its interrupts masked; the last character still leaves the USART. */
EMB_NORETURN void emb_board_exit(int code);

#ifdef __cplusplus
}
#endif

#endif /* EMB_BOARD_H */
