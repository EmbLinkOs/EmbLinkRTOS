/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    bench_platform.c
 * @brief   What FreeRTOS's build of bench_compare needs on the Pico 2 W, and the EmbLinkRTOS build
 *          gets from its port and board.
 *
 * @details
 * The vector table and reset, linked by the board's own regions.ld with its IMAGE_DEF block, and
 * the board's own support (soc/rpi/rp2350/board.c) for the 150 MHz clock, the pins and the UART0
 * console, so both kernels run on the same clocks. bench_end() reboots into BOOTSEL, as a test
 * image does, for picotool's next load.
 *
 *   BENCH_TEST_IRQ the board's test line (EMB_HW_TEST_IRQ), bench_compare's interrupt
 *
 * @author  Junior Deogracias
 * @date    2026-11-06 (created)
 */
#include "FreeRTOS.h"

#include <stdint.h>

#include <emb_board.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

#define SCB_CPACR    0xE000ED88u
#define NVIC_ISER(n) (0xE000E100u + 4u * (n))
#define NVIC_IPR(n)  (0xE000E400u + (n))
#define NVIC_ISPR(n) (0xE000E200u + 4u * (n))

void bench_platform_init(void);
void bench_puts(const char *s);
void bench_isr(void);
void bench_end(void);
void embn_cm_reset(void); /* regions.ld's ENTRY */
void emb_cm_halt(void);   /* the board code's halt, the port's elsewhere */
int main(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

extern uint32_t __data_load[], __data_start[], __data_end[], __bss_start[], __bss_end[];

/* the main stack, where regions.ld places the EmbLinkRTOS interrupt stack */
static uint8_t msp_stack[2048] __attribute__((aligned(8), section(".emb_isr_stack")));

static void unexpected(void)
{
    for (;;) {
    }
}

void emb_cm_halt(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

typedef void (*vector_t)(void);
__attribute__((section(".vectors"),
               used)) static const vector_t vectors[16 + BENCH_TEST_IRQ + 1] = {
    [0] = (vector_t)(uintptr_t)&msp_stack[sizeof(msp_stack)],
    [1] = embn_cm_reset,
    [2] = unexpected, /* NMI */
    [3] = unexpected, /* HardFault */
    [4] = unexpected, /* MemManage */
    [5] = unexpected, /* BusFault */
    [6] = unexpected, /* UsageFault */
    [7] = unexpected, /* SecureFault */
    [11] = SVC_Handler,
    [12] = unexpected, /* DebugMon */
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 + BENCH_TEST_IRQ] = bench_isr,
};

void embn_cm_reset(void)
{
    uint32_t *src = __data_load;
    uint32_t *dst;
    for (dst = __data_start; dst < __data_end; dst++) {
        *dst = *src++;
    }
    for (dst = __bss_start; dst < __bss_end; dst++) {
        *dst = 0u;
    }
    REG32(SCB_CPACR) |= 0xFu << 20; /* CP10, CP11: the FPU, which the port saves */
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    (void)main();
    unexpected();
}

void bench_platform_init(void)
{
    emb_board_init(); /* PLL_SYS at 150 MHz, the pins, UART0 */
}

void bench_puts(const char *s)
{
    emb_board_puts(s);
}

/* the image's end: back to the boot ROM's loader (EMB_RP2350_EXIT_TO_BOOTSEL), as a test image */
void bench_end(void)
{
    emb_board_exit(0);
}

/* The test line: enabled, one level below configMAX_SYSCALL_INTERRUPT_PRIORITY's, so that its
 * handler may call FromISR functions. */
void bench_test_irq_enable(void);
void bench_test_irq_enable(void)
{
    REG8(NVIC_IPR(BENCH_TEST_IRQ)) = (uint8_t)(3u << (8u - configPRIO_BITS));
    REG32(NVIC_ISER(BENCH_TEST_IRQ / 32u)) = 1u << (BENCH_TEST_IRQ % 32u);
}

/* bench_compare's interrupt: the test line pended in the NVIC, as emb_irq_pend() does */
void bench_trigger(void);
void bench_trigger(void)
{
    REG32(NVIC_ISPR(BENCH_TEST_IRQ / 32u)) = 1u << (BENCH_TEST_IRQ % 32u);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}
