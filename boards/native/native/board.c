/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The host as a board. */
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
