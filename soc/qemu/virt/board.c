/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    board.c
 * @brief   Board support for QEMU's RISC-V virt machine, described in YAML.
 *
 * @details
 * Console on the ns16550a UART, run end through QEMU's sifive_test device (status 0 passes, (code
 * << 16) | 0x3333 fails with code). A reference board for emulation (SIM-002).
 *
 * @author  Junior Deogracias
 * @date    2026-10-09 (created)
 */
#include <emb_board.h>

#define UART_THR     (*(volatile uint8_t *)(uintptr_t)(EMB_HW_CONSOLE_BASE + 0u))
#define UART_LSR     (*(volatile uint8_t *)(uintptr_t)(EMB_HW_CONSOLE_BASE + 5u))
#define LSR_THRE     0x20u
#define TEST_FINISH  (*(volatile uint32_t *)(uintptr_t)EMB_HW_TEST_BASE)
#define FINISHER_OK  0x5555u
#define FINISHER_ERR 0x3333u
#define FINISHER_RST 0x7777u /* the sifive_test device: system reset */

void emb_board_init(void)
{
}

void emb_board_led_toggle(void)
{
    /* no LED on the virt machine */
}

static void put_char(char c)
{
    while ((UART_LSR & LSR_THRE) == 0u) { /* bounded by the UART draining a character */
    }
    UART_THR = (uint8_t)c;
}

/* The console's bytes as they are (A127): emb_board_puts() and the framed console call it. */
void emb_board_write(const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t i;
    for (i = 0u; i < len; i++) { /* bounded by len */
        put_char(p[i]);
    }
}

void emb_board_put_u32(uint32_t v)
{
    char buf[11];
    unsigned i = sizeof(buf) - 1u;
    buf[i] = '\0';
    do { /* at most ten digits */
        i--;
        buf[i] = (char)('0' + (char)(v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0u);
    emb_board_puts(&buf[i]);
}

void emb_board_exit(int code)
{
    TEST_FINISH = (code == 0) ? FINISHER_OK : (((uint32_t)code << 16) | FINISHER_ERR);
    for (;;) {
    }
}

void emb_board_fault_report(const emb_fault_info_t *info)
{
    emb_board_puts("\nemb: kernel fault class ");
    emb_board_put_u32((info != NULL) ? info->fault_class : 0u);
    emb_board_puts(" code ");
    emb_board_put_u32((info != NULL) ? info->code : 0u);
    emb_board_puts(" pc ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->pc : 0u);
    emb_board_puts(" address ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->address : 0u);
    emb_board_puts(" at ");
    emb_board_puts((info != NULL && info->where != NULL) ? info->where : "?");
    emb_board_puts("\n");
    emb_board_exit(2);
}

void emb_board_system_reset(void)
{
    TEST_FINISH = FINISHER_RST;
}
