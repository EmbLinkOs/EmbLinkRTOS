/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    crash_print.h
 * @brief   The crash record as one console line, "EMB_CRASH <hex>", which tools/crashdec reads from
 *          a run's log (SPEC-015 §4).
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#ifndef CRASH_PRINT_H
#define CRASH_PRINT_H

#include <emb/crash.h>

#include <emb_board.h>

static inline void crash_print(const emb_crash_record_t *r)
{
    static const char digits[] = "0123456789abcdef";
    char pair[3];
    uint16_t i;
    pair[2] = '\0';
    emb_board_puts("EMB_CRASH ");
    for (i = 0u; i < r->length; i++) { /* bounded by the record length */
        pair[0] = digits[(r->raw[i] >> 4) & 0xFu];
        pair[1] = digits[r->raw[i] & 0xFu];
        emb_board_puts(pair);
    }
    emb_board_puts("\n");
}

#endif /* CRASH_PRINT_H */
