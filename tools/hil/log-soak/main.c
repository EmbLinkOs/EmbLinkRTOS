/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    main.c
 * @brief   The framed console's soak: logs and the trace's stream at once, for hours (M4.3).
 *
 * @details
 * A thread logs a numbered record every 10 ms; another logs a burst of 100 every second, more
 * than the ring's 1 KiB keeps while the log thread waits for the console, so records are dropped
 * and counted; a third logs the log ring's and the trace ring's counters every 5 s. The trace ring
 * streams on channel 1 every 500 ms. tools/hil/log_soak.py reads the console and checks that no
 * frame is damaged and that every numbered record missing is one a dropped record counted.
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/emb.h>

#include <emb_board.h>

EMB_LOG_MODULE_DEFINE(soak, EMB_LOG_LEVEL_INF);

static void steady(void *arg)
{
    emb_instant_t next = emb_time_now();
    unsigned seq = 0u;
    (void)arg;
    for (;;) {
        next = emb_instant_add(next, EMB_MS(10));
        (void)emb_thread_sleep_until(next);
        EMB_LOG_INF(soak, "seq %u", seq);
        seq++;
    }
}

static void bursts(void *arg)
{
    emb_instant_t next = emb_time_now();
    unsigned round = 0u;
    (void)arg;
    for (;;) {
        unsigned i;
        next = emb_instant_add(next, EMB_MS(1000));
        (void)emb_thread_sleep_until(next);
        for (i = 0u; i < 100u; i++) {
            EMB_LOG_DBG(soak, "not compiled at the default level %u", i);
            EMB_LOG_WRN(soak, "burst %u %u", round, i);
        }
        round++;
    }
}

static void counters(void *arg)
{
    emb_instant_t next = emb_time_now();
    (void)arg;
    EMB_CHECK(emb_trace_start());
    for (;;) {
        emb_log_stats_t ls;
        emb_trace_stats_t ts;
        next = emb_instant_add(next, EMB_MS(5000));
        (void)emb_thread_sleep_until(next);
        (void)emb_log_stats_get(&ls);
        (void)emb_trace_stats_get(&ts);
        EMB_LOG_INF(soak, "log records %lu dropped %lu sent %lu high %lu",
                    (unsigned long)ls.records, (unsigned long)ls.dropped, (unsigned long)ls.sent,
                    (unsigned long)ls.high_water);
        EMB_LOG_INF(soak, "trace recorded %lu overwritten %lu sent %lu", (unsigned long)ts.recorded,
                    (unsigned long)ts.overwritten, (unsigned long)ts.sent);
    }
}

EMB_THREAD_DEFINE(steady_thread, steady, NULL, 5u, 1024u, 0u);
EMB_THREAD_DEFINE(burst_thread, bursts, NULL, 4u, 1024u, 0u);
EMB_THREAD_DEFINE(counter_thread, counters, NULL, 3u, 1024u, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
