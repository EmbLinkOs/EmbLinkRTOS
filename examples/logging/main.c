/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    main.c
 * @brief   Logging, with either backend (SPEC-015 §2, A127).
 *
 * @details
 * One thread logs a fixed set of records, a hexdump and a burst larger than the ring, then sends
 * what is left with emb_log_flush() and ends. Built as it is, the console is plain text and the
 * log thread formats each record on the target. Built with the framed console, the records are
 * sent as they are and tools/logdec formats them on the host from the ELF:
 *
 *   cmake -S examples/logging -B build/logging -DEMB_BOARD=mps2_an386
 *   cmake -S examples/logging -B build/logging-framed -DEMB_BOARD=mps2_an386 \
 *         -DEMB_SET=EMB_CONSOLE_FRAMED=y
 *   python3 tools/logdec/logdec.py --elf build/logging-framed/logging.elf console.bin
 *
 * Both print the same lines (the CI's application job checks it).
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/emb.h>
#include <emb/log.h>

#include <emb_board.h>
#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#endif

#define BURST 200u /* more records than the ring holds */

EMB_LOG_MODULE_DEFINE(app, EMB_LOG_LEVEL_DBG);

static void logger(void *arg)
{
    static const unsigned char rx[] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    unsigned i;
    (void)arg;
    EMB_LOG_INF(app, "logging on %s", EMB_HW_BOARD_NAME);
    EMB_LOG_WRN(app, "a %d b %u c %s", -5, 7u, "xyz");
    EMB_LOG_ERR(app, "[%5d|%-5d|%05d|%+d]", 42, 42, 42, 42);
    EMB_LOG_INF(app, "%x %X %#x %o %c%%", 255u, 255u, 255u, 8u, 'z');
    EMB_LOG_INF(app, "%lld %llu %ld %lu", -1LL, 18446744073709551615ULL, -70000L, 70000UL);
    EMB_LOG_INF(app, "%.2f %.0f %f", 3.14159, 2.5, -0.5);
    EMB_LOG_INF(app, "<%.3s|%4s>", "abcdef", "z");
    EMB_LOG_HEXDUMP_INF(app, rx, sizeof(rx), "rx");
    EMB_LOG_DBG(app, "not compiled: above CONFIG_EMB_LOG_LEVEL_MAX");
    /* more than the ring holds while the log thread cannot run: the rest are dropped, and the
     * next record that fits says how many */
    for (i = 0u; i < BURST; i++) {
        EMB_LOG_INF(app, "burst %u", i);
    }
    emb_log_flush();
    EMB_LOG_INF(app, "after the burst");
    emb_log_flush();
    emb_board_puts("logging: done\n");
#if CONFIG_EMB_ARCH_NATIVE
    emb_native_exit(0);
#else
    emb_board_exit(0);
#endif
}

EMB_THREAD_DEFINE(logger_thread, logger, NULL, 3u, 1024u, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
