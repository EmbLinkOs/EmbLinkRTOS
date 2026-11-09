/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The NUCLEO-F446RE as a 3.3 V USB-UART for another board's console (docs/engineering/hil.md):
 * USART1 receives on PA10 (CN10 pin 33) and every byte goes out of USART2 (PA2) to the ST-LINK's
 * virtual COM port, both at 115200 8N1. The header pins named PA2 and PA3 (CN10 pins 35 and 37,
 * Arduino D1 and D0) are not connected to the STM32 on a stock board (SB62 and SB63 open), which
 * is why the relay exists. PA10 idles high through a pull-up when nothing drives it; a break
 * (the line held low) is not relayed. The green LED (PA5) toggles on each byte.
 * build.sh links it with the board's own clock, pin and USART drivers and its regions.ld.
 */
#include <stdint.h>

#include <emb_stm32_drivers.h>
#include <hw_config.h>

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define USART_SR    0x00u
#define USART_DR    0x04u
#define USART_CR1   0x0Cu
#define SR_RXNE     (1u << 5)
#define SR_ORE      (1u << 3)
#define SR_FE       (1u << 1)
#define GPIOA_PUPDR (EMB_HW_GPIOA_BASE + 0x0Cu)
#define CR1_RE      (1u << 2)

extern uint32_t __data_load[], __data_start[], __data_end[], __bss_start[], __bss_end[];
void embn_cm_reset(void);
int main(void);

static uint8_t msp_stack[1024] __attribute__((aligned(8), section(".emb_isr_stack")));

static void hang(void)
{
    for (;;) {
    }
}

typedef void (*vector_t)(void);
__attribute__((section(".vectors"), used)) static const vector_t vectors[16] = {
    [0] = (vector_t)(uintptr_t)&msp_stack[sizeof(msp_stack)],
    [1] = embn_cm_reset,
    [2] = hang,
    [3] = hang,
    [4] = hang,
    [5] = hang,
    [6] = hang,
};

static const emb_stm32_pin_t pins[] = {
    {EMB_STM32_PORT(A), 2u, EMB_STM32_PIN_AF, 7u, 0u, 0u},  /* USART2_TX to the ST-LINK */
    {EMB_STM32_PORT(A), 3u, EMB_STM32_PIN_AF, 7u, 0u, 0u},  /* USART2_RX from the ST-LINK */
    {EMB_STM32_PORT(A), 10u, EMB_STM32_PIN_AF, 7u, 0u, 0u}, /* USART1_RX from the other board */
    {EMB_STM32_PORT(A), 5u, EMB_STM32_PIN_OUTPUT, 0u, 0u, 0u},
};

void embn_cm_reset(void)
{
    uint32_t *src = __data_load;
    for (uint32_t *dst = __data_start; dst < __data_end; dst++) {
        *dst = *src++;
    }
    for (uint32_t *dst = __bss_start; dst < __bss_end; dst++) {
        *dst = 0u;
    }
    (void)main();
    hang();
}

static void puts_vcp(const char *s)
{
    while (*s != '\0') {
        emb_stm32_usart_putc(EMB_HW_USART2_BASE, *s++);
    }
}

int main(void)
{
    if (emb_stm32f4_clock_init() == 0) {
        hang();
    }
    emb_stm32_pins_apply(pins, sizeof(pins) / sizeof(pins[0]));
    emb_stm32_clock_enable(EMB_HW_USART2_CLKEN_OFFSET, EMB_HW_USART2_CLKEN_BIT);
    emb_stm32_clock_enable(EMB_HW_USART1_CLKEN_OFFSET, EMB_HW_USART1_CLKEN_BIT);
    emb_stm32_usart_init(EMB_HW_USART2_BASE, EMB_HW_APB1_HZ, 115200u);
    emb_stm32_usart_init(EMB_HW_USART1_BASE, EMB_HW_APB2_HZ, 115200u);
    REG32(GPIOA_PUPDR) = (REG32(GPIOA_PUPDR) & ~(3u << 20)) |
                         (1u << 20); /* PA10 idles high unwired */
    REG32(EMB_HW_USART1_BASE + USART_CR1) |= CR1_RE;
    puts_vcp("\r\n[nucleo uart relay: PA10 -> VCP, 115200]\r\n");
    for (;;) {
        uint32_t sr = REG32(EMB_HW_USART1_BASE + USART_SR);
        if ((sr & (SR_RXNE | SR_ORE)) != 0u) {
            char c = (char)REG32(EMB_HW_USART1_BASE + USART_DR); /* also clears ORE, FE */
            if ((sr & SR_FE) != 0u) {
                continue; /* a break: the line held low, nothing sent */
            }
            emb_stm32_usart_putc(EMB_HW_USART2_BASE, c);
            emb_stm32_gpio_toggle(EMB_HW_GPIOA_BASE, 5u);
        }
    }
}
