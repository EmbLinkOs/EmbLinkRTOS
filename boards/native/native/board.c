/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The host as a board. */
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
