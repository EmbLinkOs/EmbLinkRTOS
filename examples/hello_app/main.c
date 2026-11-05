/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * An application outside the RTOS tree (docs/user/application.md): a producer thread sends
 * readings to a consumer through a message queue every 10 ms, and a timer counts the
 * seconds. It prints each step and ends after five readings. Build it for any board:
 *
 *   cmake -S examples/hello_app -B build/hello -DEMB_BOARD=mps2_an386
 *   cmake --build build/hello
 *   cmake --build build/hello --target run_hello   # under the board's QEMU machine
 */
#include <emb/emb.h>

#include <emb_board.h>
#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#endif

#define READINGS 5u

typedef struct reading {
    uint32_t seq;
    uint32_t value;
} reading_t;

EMB_MSGQ_DEFINE(readings, sizeof(reading_t), 4u, 0u);

static void producer(void *arg)
{
    emb_instant_t next = emb_time_now();
    uint32_t seq;
    (void)arg;
    for (seq = 1u; seq <= READINGS; seq++) {
        reading_t r = {seq, seq * 7u};
        next = emb_instant_add(next, EMB_MS(10));
        (void)emb_thread_sleep_until(next); /* every 10 ms, without drift */
        EMB_CHECK(emb_msgq_send(readings, &r, EMB_WAIT_FOREVER));
    }
}

static void consumer(void *arg)
{
    reading_t r;
    (void)arg;
    for (;;) {
        EMB_CHECK(emb_msgq_receive(readings, &r, EMB_WAIT_FOREVER));
        emb_board_puts("hello_app: reading ");
        emb_board_put_u32(r.seq);
        emb_board_puts(" = ");
        emb_board_put_u32(r.value);
        emb_board_puts("\n");
        if (r.seq == READINGS) {
            emb_board_puts("hello_app: done\n");
#if CONFIG_EMB_ARCH_NATIVE
            emb_native_exit(0);
#endif
        }
    }
}

EMB_THREAD_DEFINE(producer_thread, producer, NULL, 2u, 512u, 0u);
EMB_THREAD_DEFINE(consumer_thread, consumer, NULL, 3u, 512u, 0u);

int main(void)
{
    emb_board_init();
    emb_board_puts("hello_app: start\n");
    emb_kernel_init();
    emb_kernel_start();
}
