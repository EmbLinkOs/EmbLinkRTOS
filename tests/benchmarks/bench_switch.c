/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Context-switch cost (R-003 T5 and T6, SPEC-012 §13): the average cost of a
 * notification hand-over between two threads (set, wake, switch, wait, return) and of a
 * yield pair, in cycle-counter units, over many iterations. Prints the raw figures with
 * the metadata of 05 §4.4 on the console; a number is evidence only from hardware
 * (SIM-003), never from an emulator or the native port.
 */
#include <emb/arch.h> /* the cycle counter is a port-contract function */
#include <emb/emb.h>

#include <emb_board.h>

#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#endif

#define ITERATIONS 10000u
#define PING       EMB_NOTIFY_BIT(0)

static emb_thread_t ta;
static emb_thread_t tb;
static uint32_t rounds;
static emb_cycle_t t_start;
static emb_cycle_t t_end;

static void ping_a(void *arg)
{
    (void)arg;
    for (;;) {
        (void)emb_notify_wait(PING, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, NULL);
        (void)emb_notify_set(tb, PING);
    }
}

static void report(const char *what, uint32_t units, uint32_t count)
{
    emb_board_puts("bench ");
    emb_board_puts(what);
    emb_board_puts(": total ");
    emb_board_put_u32(units);
    emb_board_puts(" cycle-units over ");
    emb_board_put_u32(count);
    emb_board_puts(" ops; per op x100 = ");
    emb_board_put_u32((uint32_t)(((uint64_t)units * 100u) / count));
    emb_board_puts("\n");
}

static void ping_b(void *arg)
{
    uint32_t i;
    (void)arg;
    t_start = emb_arch_cycles();
    for (i = 0u; i < ITERATIONS; i++) {
        (void)emb_notify_set(ta, PING);
        (void)emb_notify_wait(PING, EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, NULL);
        rounds++;
    }
    t_end = emb_arch_cycles();
    report("notify round trip (2 switches)", t_end - t_start, ITERATIONS * 2u);
    emb_board_puts("meta: board " EMB_HW_BOARD_NAME ", compiler " EMB_COMPILER_NAME
                   ", profile " CONFIG_EMB_PROFILE ", checked ");
    emb_board_put_u32((uint32_t)CONFIG_EMB_CHECKED);
    emb_board_puts(", cycle unit hz ");
    emb_board_put_u32(emb_arch_cycles_hz());
    emb_board_puts("\n");
#if CONFIG_EMB_ARCH_NATIVE
    emb_board_puts("native: not timing evidence (SIM-003)\n");
    emb_native_exit(0);
#else
    emb_board_puts("BENCH_END\n");
    for (;;) {
        (void)emb_thread_sleep(EMB_SEC(1));
    }
#endif
}

EMB_THREAD_DEFINE(thread_a, ping_a, NULL, 2u, 256u, 0u);
EMB_THREAD_DEFINE(thread_b, ping_b, NULL, 3u, 256u, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    ta = thread_a;
    tb = thread_b;
    emb_kernel_start();
}
