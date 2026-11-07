/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* STM32 general-purpose timer as a periodic trigger (RM0390 Rev 6 §18): its update event drives
 * TRGO, which another peripheral (ADC1's regular conversions) takes as its start. */
#include <emb_stm32_drivers.h>

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define TIM_CR1     0x00u
#define TIM_CR2     0x04u
#define TIM_SR      0x10u
#define TIM_EGR     0x14u
#define TIM_PSC     0x28u
#define TIM_ARR     0x2Cu
#define CR1_CEN     (1u << 0)
#define CR1_ARPE    (1u << 7)
#define CR2_MMS_UPD (2u << 4) /* master mode: the update event is TRGO */
#define EGR_UG      (1u << 0)

#define TICK_HZ 1000000u /* the counter's rate: 1 MHz, so the period is in microseconds */

uint32_t emb_stm32_tim_clock_hz(uint32_t pclk_hz, uint32_t apb_div)
{
    /* RM0390 §6.2: a timer on a divided APB bus runs at twice the bus clock */
    return (apb_div == 1u) ? pclk_hz : 2u * pclk_hz;
}

int emb_stm32_tim_trgo_periodic(uintptr_t base, uint32_t tim_clk_hz, uint32_t rate_hz)
{
    uint32_t period_us;
    if (rate_hz == 0u || rate_hz > TICK_HZ || (tim_clk_hz % TICK_HZ) != 0u) {
        return 0;
    }
    period_us = TICK_HZ / rate_hz;
    REG32(base + TIM_CR1) = 0u;
    REG32(base + TIM_PSC) = (tim_clk_hz / TICK_HZ) - 1u;
    REG32(base + TIM_ARR) = period_us - 1u;
    REG32(base + TIM_CR2) = CR2_MMS_UPD;
    REG32(base + TIM_EGR) = EGR_UG; /* load PSC and ARR now */
    REG32(base + TIM_SR) = 0u;
    REG32(base + TIM_CR1) = CR1_ARPE | CR1_CEN;
    return 1;
}

void emb_stm32_tim_stop(uintptr_t base)
{
    REG32(base + TIM_CR1) = 0u;
}
