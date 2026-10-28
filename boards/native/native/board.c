/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The host as a board. */
#include <stdio.h>

#include <emb_board.h>

void emb_board_init(void)
{
}

void emb_board_led_toggle(void)
{
    /* the host has no LED */
}

void emb_board_puts(const char *s)
{
    (void)fputs(s, stdout);
}

void emb_board_put_u32(uint32_t v)
{
    (void)printf("%lu", (unsigned long)v);
}
