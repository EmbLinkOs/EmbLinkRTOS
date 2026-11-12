/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    board.c
 * @brief   The host as a board.
 *
 * @author  Junior Deogracias
 * @date    2026-10-07 (created)
 */
#include <emb/config.h>
#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif

#include <stdio.h>

#include <emb_board.h>
#include <emb_native.h> /* the C library is host code (A93) */

void emb_board_init(void)
{
}

void emb_board_led_toggle(void)
{
    /* the host has no LED */
}

void emb_board_puts(const char *s)
{
    emb_native_host_begin();
    (void)fputs(s, stdout);
    emb_native_host_end();
}

void emb_board_put_u32(uint32_t v)
{
    emb_native_host_begin();
    (void)printf("%lu", (unsigned long)v);
    emb_native_host_end();
}

#if CONFIG_EMB_DEVICE
/* No devices yet: the board has no description to generate them from (SPEC-016 §8). */
const emb_device_table_t emb_board_device_table = {NULL, 0u, 0u};
#endif
