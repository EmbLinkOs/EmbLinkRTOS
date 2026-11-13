/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    main.c
 * @brief   The trace ring: a few threads' events, dumped and turned into CTF (SPEC-015 §3, A128).
 *
 * @details
 * A producer sends readings to a consumer through a message queue every millisecond, and the
 * consumer signals a semaphore for every fourth. The trace ring records the kernel's events for
 * 20 ms; the ring is then dumped on the framed console's channel 1 and the image ends:
 *
 *   cmake -S examples/tracing -B build/tracing -DEMB_BOARD=mps2_an386
 *   python3 tools/trace/trace.py console.bin --ctf build/trace --perfetto build/trace.json
 *   babeltrace2 build/trace
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/emb.h>

#include <emb_board.h>
#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#endif

EMB_MSGQ_DEFINE(readings, sizeof(uint32_t), 4u, 0u);
EMB_SEM_DEFINE(fourth, 0u, 100u);

static void producer(void *arg)
{
    emb_instant_t next = emb_time_now();
    uint32_t seq = 0u;
    (void)arg;
    for (;;) {
        next = emb_instant_add(next, EMB_MS(1));
        (void)emb_thread_sleep_until(next);
        seq++;
        (void)emb_msgq_send(readings, &seq, EMB_NO_WAIT);
    }
}

static void consumer(void *arg)
{
    uint32_t v;
    (void)arg;
    for (;;) {
        if ((emb_msgq_receive(readings, &v, EMB_WAIT_FOREVER) == EMB_OK) && ((v % 4u) == 0u)) {
            (void)emb_sem_give(fourth);
        }
    }
}

static void control(void *arg)
{
    emb_trace_stats_t st;
    (void)arg;
    EMB_CHECK(emb_trace_start());
    (void)emb_thread_sleep(EMB_MS(20));
    emb_trace_stop();
    EMB_CHECK(emb_trace_dump());
    EMB_CHECK(emb_trace_stats_get(&st));
    emb_board_puts("tracing: ");
    emb_board_put_u32(st.recorded);
    emb_board_puts(" events recorded, ");
    emb_board_put_u32(st.sent);
    emb_board_puts(" sent\ntracing: done\n");
#if CONFIG_EMB_ARCH_NATIVE
    emb_native_exit(0);
#else
    emb_board_exit(0);
#endif
}

EMB_THREAD_DEFINE(producer_thread, producer, NULL, 3u, 1024u, 0u);
EMB_THREAD_DEFINE(consumer_thread, consumer, NULL, 2u, 1024u, 0u);
EMB_THREAD_DEFINE(control_thread, control, NULL, 4u, 1024u, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
