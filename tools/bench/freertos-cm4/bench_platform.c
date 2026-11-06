/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * What FreeRTOS's build of bench_compare needs on a Cortex-M4 board, and the EmbLinkRTOS build
 * gets from its port and board: the vector table and reset, linked by the board's own
 * regions.ld, and the console. On the NUCLEO-F446RE the clock, the pins and USART2 are set by
 * the board's own drivers (drivers/st/, board_pins.c), so both kernels run at the same 180 MHz
 * with the same console; on QEMU's MPS2 AN386 the console is the CMSDK UART0.
 *
 *   BENCH_NUCLEO   1 for the NUCLEO-F446RE, 0 for the AN386
 *   BENCH_TEST_IRQ the board's test line (EMB_HW_TEST_IRQ), bench_compare's interrupt
 */
#include "FreeRTOS.h"

#include <hw_config.h>
#include <stdint.h>

#if BENCH_NUCLEO
#include <emb_stm32_drivers.h>
extern const emb_stm32_pin_t emb_board_pins[];
extern const unsigned emb_board_pin_count;
#endif

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

#define SCB_CPACR    0xE000ED88u
#define NVIC_ISER(n) (0xE000E100u + 4u * (n))
#define NVIC_IPR(n)  (0xE000E400u + (n))

void bench_platform_init(void);
void bench_puts(const char *s);
void bench_isr(void);
void embn_cm_reset(void); /* regions.ld's ENTRY */
int main(void);
void xPortPendSVHandler(void);
void vPortSVCHandler(void);
void xPortSysTickHandler(void);

extern uint32_t __data_load[], __data_start[], __data_end[], __bss_start[], __bss_end[];

/* the main stack, where regions.ld places the EmbLinkRTOS interrupt stack */
static uint8_t msp_stack[2048] __attribute__((aligned(8), section(".emb_isr_stack")));

static void unexpected(void)
{
    for (;;) {
    }
}

typedef void (*vector_t)(void);
__attribute__((section(".vectors"), used)) static const vector_t vectors[16 + BENCH_TEST_IRQ + 1] = {
    [0] = (vector_t)(uintptr_t)&msp_stack[sizeof(msp_stack)],
    [1] = embn_cm_reset,
    [2] = unexpected,  /* NMI */
    [3] = unexpected,  /* HardFault */
    [4] = unexpected,  /* MemManage */
    [5] = unexpected,  /* BusFault */
    [6] = unexpected,  /* UsageFault */
    [11] = vPortSVCHandler,
    [12] = unexpected, /* DebugMon */
    [14] = xPortPendSVHandler,
    [15] = xPortSysTickHandler,
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
    REG32(SCB_CPACR) |= 0xFu << 20; /* CP10, CP11: the FPU, which the CM4F port expects */
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    (void)main();
    unexpected();
}

#if BENCH_NUCLEO
void bench_platform_init(void)
{
    if (emb_stm32f4_clock_init() == 0) {
        unexpected(); /* no HSE: the CPU is not at configCPU_CLOCK_HZ */
    }
    emb_stm32_pins_apply(emb_board_pins, emb_board_pin_count);
    emb_stm32_clock_enable(EMB_HW_CONSOLE_CLKEN_OFFSET, EMB_HW_CONSOLE_CLKEN_BIT);
    emb_stm32_usart_init(EMB_HW_CONSOLE_BASE, EMB_HW_CONSOLE_CLOCK_HZ, EMB_HW_CONSOLE_BAUD);
}

void bench_puts(const char *s)
{
    while (*s != '\0') {
        emb_stm32_usart_putc(EMB_HW_CONSOLE_BASE, *s++);
    }
}
#else
#define UART_DATA  REG32(EMB_HW_CONSOLE_BASE + 0x00u)
#define UART_STATE REG32(EMB_HW_CONSOLE_BASE + 0x04u)
#define UART_CTRL  REG32(EMB_HW_CONSOLE_BASE + 0x08u)
#define UART_DIV   REG32(EMB_HW_CONSOLE_BASE + 0x10u)

void bench_platform_init(void)
{
    UART_DIV = EMB_HW_CONSOLE_CLOCK_HZ / EMB_HW_CONSOLE_BAUD;
    UART_CTRL = 1u; /* TX enable */
}

void bench_puts(const char *s)
{
    while (*s != '\0') {
        while ((UART_STATE & 1u) != 0u) { /* TX full */
        }
        UART_DATA = (uint32_t)(unsigned char)*s++;
    }
}
#endif

/* The test line: enabled, one level below configMAX_SYSCALL_INTERRUPT_PRIORITY's, so that its
 * handler may call FromISR functions. */
void bench_test_irq_enable(void);
void bench_test_irq_enable(void)
{
    REG8(NVIC_IPR(BENCH_TEST_IRQ)) = (uint8_t)(3u << (8u - configPRIO_BITS));
    REG32(NVIC_ISER(BENCH_TEST_IRQ / 32u)) = 1u << (BENCH_TEST_IRQ % 32u);
}
