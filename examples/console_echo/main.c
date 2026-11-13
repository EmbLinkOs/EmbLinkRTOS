/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    main.c
 * @brief   The console both ways: what the host sends comes back (M4.3's two-way relay).
 *
 * @details
 * The board reads its console's UART through the UART class (SPEC-017 §4), a line at a time or
 * whatever arrived before the line went quiet, and sends it back in capitals with its length.
 * On the Pico 2 W behind the NUCLEO-F446RE's relay this checks the host's way to the board, PA9
 * to GPIO1, which the shell and update channels use. Any board whose console is a UART device
 * (EMB_SOC_CONSOLE_DEVICE) runs it.
 *
 *   emb build -b pico2_w -S examples/console_echo
 *   emb flash -B build/console_echo-pico2_w
 *   printf 'hello\n' > /dev/cu.usbmodem31103   # and watch: "echo 5: HELLO"
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/emb.h>

#include <emb_board.h>
#include <soc_devices.h>

#ifndef EMB_SOC_CONSOLE_DEVICE
#error "console_echo needs a board whose console is a UART device (EMB_SOC_CONSOLE_DEVICE)"
#endif

static void echo(void *arg)
{
    static char line[64];
    (void)arg;
    emb_board_puts("echo: ready\n");
    for (;;) {
        size_t got = 0u;
        size_t i;
        emb_status_t st = emb_drv_uart_read(EMB_SOC_CONSOLE_DEVICE, line, sizeof(line) - 1u,
                                            EMB_DRV_UART_IDLE, EMB_WAIT_FOREVER, &got);
        if (got == 0u) {
            continue;
        }
        for (i = 0u; i < got; i++) { /* bounded by the line */
            if ((line[i] >= 'a') && (line[i] <= 'z')) {
                line[i] = (char)(line[i] - 'a' + 'A');
            }
            if ((line[i] == '\r') || (line[i] == '\n')) {
                line[i] = ' ';
            }
        }
        line[got] = '\0';
        emb_board_puts("echo ");
        emb_board_put_u32((uint32_t)got);
        emb_board_puts((st == EMB_OK) ? ": " : " (damaged): ");
        emb_board_puts(line);
        emb_board_puts("\n");
    }
}

EMB_THREAD_DEFINE(echo_thread, echo, NULL, 3u, 1024u, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
