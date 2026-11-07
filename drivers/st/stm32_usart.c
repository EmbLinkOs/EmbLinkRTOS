/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* STM32 USART (RM0390 Rev 6 §25.6): the console's polled transmit, and an interrupt-driven
 * receive for commands. */
#include <emb_stm32_drivers.h>

#define REG32(a)  (*(volatile uint32_t *)(uintptr_t)(a))
#define USART_SR  0x00u
#define USART_DR  0x04u
#define USART_BRR 0x08u
#define USART_CR1 0x0Cu
#define SR_ORE    (1u << 3)
#define SR_RXNE   (1u << 5)
#define SR_TC     (1u << 6)
#define SR_TXE    (1u << 7)
#define CR1_RE    (1u << 2)
#define CR1_TE    (1u << 3)
#define CR1_RXNEI (1u << 5)
#define CR1_UE    (1u << 13)

/* One character at 115200 baud from a 180 MHz core is ~15 600 cycles, a few thousand polls. */
#define TX_BOUND 100000u

void emb_stm32_usart_init(uintptr_t base, uint32_t pclk_hz, uint32_t baud)
{
    REG32(base + USART_CR1) = 0u;
    REG32(base + USART_BRR) = (pclk_hz + baud / 2u) / baud; /* 16x oversampling */
    REG32(base + USART_CR1) = CR1_UE | CR1_TE;              /* 1 stop bit in CR2's reset value */
}

void emb_stm32_usart_putc(uintptr_t base, char c)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: one character time */
        if ((REG32(base + USART_SR) & SR_TXE) != 0u) {
            break;
        }
    }
    REG32(base + USART_DR) = (uint32_t)(unsigned char)c;
}

void emb_stm32_usart_flush(uintptr_t base)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: the last character's frame */
        if ((REG32(base + USART_SR) & SR_TC) != 0u) {
            break;
        }
    }
}

void emb_stm32_usart_rx_irq_enable(uintptr_t base)
{
    REG32(base + USART_CR1) |= CR1_RE | CR1_RXNEI;
}

int emb_stm32_usart_getc_isr(uintptr_t base)
{
    uint32_t sr = REG32(base + USART_SR);
    if ((sr & (SR_RXNE | SR_ORE)) == 0u) {
        return -1;
    }
    /* reading SR then DR clears RXNE and an overrun (ORE): the overrun's lost byte is gone */
    return (int)(REG32(base + USART_DR) & 0xFFu);
}
