/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Firat Hajiyev */
/*
 * Toggle the board LED every 500 ms. The absolute deadline keeps the
 * transitions on a fixed schedule instead of accumulating loop overhead.
 */
#include <emb/emb.h>

#include <emb_board.h>
#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#endif

static void blink(void *arg)
{
    emb_instant_t next = emb_time_now();
    bool led_on = false;
#if CONFIG_EMB_ARCH_NATIVE
    unsigned int toggles = 0u;
#endif
    (void)arg;
    for (;;) {
        next = emb_instant_add(next, EMB_MS(500));
        (void)emb_thread_sleep_until(next);
        emb_board_led_toggle();
        led_on = !led_on;
        emb_board_puts(led_on ? "blinky: on\n" : "blinky: off\n");
#if CONFIG_EMB_ARCH_NATIVE
        toggles++;
        if (toggles == 10u) {
            emb_native_exit(0);
        }
#endif
    }
}

EMB_THREAD_DEFINE(blink_thread, blink, NULL, 2u, 512u, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
