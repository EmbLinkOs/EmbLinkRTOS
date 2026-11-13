/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    bench_irq.c
 * @brief   Interrupt latency, software-triggered (SPEC-002 §10, R-003 T7).
 *
 * @details
 * The cycles from emb_irq_pend() on the board's test line to the entry of the handler
 * emb_irq_connect() bound, through the port's kernel-aware entry and dispatch, over many runs;
 * minimum, maximum and average. The pend is the start the cycle counter can see; an external
 * trigger measured with a logic analyzer (docs/engineering/hil.md) adds the pin and the NVIC's own
 * latency. A number is evidence only from hardware (SIM-003).
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#include <emb/arch.h> /* the cycle counter is a port-contract function */
#include <emb/emb.h>

#include <emb_board.h>
#include <hw_config.h>

#define RUNS 1000u

static volatile emb_cycle_t t_handler;
static volatile uint32_t fired;

static void handler(void *arg)
{
    (void)arg;
    t_handler = emb_arch_cycles();
    fired++;
}

static void bench(void *arg)
{
    uint32_t i;
    uint32_t lo = 0xFFFFFFFFu;
    uint32_t hi = 0u;
    uint64_t sum = 0u;
    (void)arg;
    (void)emb_irq_connect((emb_irq_t)EMB_HW_TEST_IRQ, handler, NULL);
    (void)emb_irq_enable((emb_irq_t)EMB_HW_TEST_IRQ);
    for (i = 0u; i < RUNS; i++) {
        uint32_t before = fired;
        uint32_t spin = 0u;
        emb_cycle_t t0 = emb_arch_cycles();
        uint32_t d;
        (void)emb_irq_pend((emb_irq_t)EMB_HW_TEST_IRQ);
        while (fired == before && spin < 1000000u) { /* bounded: the line is unmasked */
            spin++;
        }
        d = (uint32_t)(t_handler - t0);
        lo = (d < lo) ? d : lo;
        hi = (d > hi) ? d : hi;
        sum += d;
    }
    emb_board_puts("bench irq pend to handler: min ");
    emb_board_put_u32(lo);
    emb_board_puts(" max ");
    emb_board_put_u32(hi);
    emb_board_puts(" avg x100 ");
    emb_board_put_u32((uint32_t)((sum * 100u) / RUNS));
    emb_board_puts(" cycle-units over ");
    emb_board_put_u32(RUNS);
    emb_board_puts(" runs\n");
    /* the machine-readable form the HIL runner checks and archives (A89) */
    emb_board_puts("BENCH v1 name=irq_pend_to_handler min=");
    emb_board_put_u32(lo);
    emb_board_puts(" max=");
    emb_board_put_u32(hi);
    emb_board_puts(" avg_x100=");
    emb_board_put_u32((uint32_t)((sum * 100u) / RUNS));
    emb_board_puts(" ops=");
    emb_board_put_u32(RUNS);
    emb_board_puts(" unit_hz=");
    emb_board_put_u32(emb_arch_cycles_hz());
    emb_board_puts(" checked=");
    emb_board_put_u32((uint32_t)CONFIG_EMB_CHECKED);
    emb_board_puts("\n");
    emb_board_puts("meta: board " EMB_HW_BOARD_NAME ", compiler " EMB_COMPILER_NAME
                   ", profile " CONFIG_EMB_PROFILE ", checked ");
    emb_board_put_u32((uint32_t)CONFIG_EMB_CHECKED);
    emb_board_puts(", cycle unit hz ");
    emb_board_put_u32(emb_arch_cycles_hz());
    emb_board_puts("\n");
    emb_board_puts("BENCH_END\n");
    /* the board's end: QEMU exits, a Pico returns to BOOTSEL for the next image, others halt */
    emb_board_exit(0);
}

/* 512 bytes, or the port's minimum where that is larger (Xtensa's windowed frames) */
#define STACK_SIZE (EMB_THREAD_STACK_MIN > 512u ? EMB_THREAD_STACK_MIN : 512u)
EMB_THREAD_DEFINE(bench_thread, bench, NULL, 2u, STACK_SIZE, 0u);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    emb_kernel_start();
}
