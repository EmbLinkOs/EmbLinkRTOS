/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    stm32f4_rcc.c
 * @brief   STM32F4 clock-tree bring-up (RM0390 Rev 6 §5.1.4, §6.3).
 *
 * @details
 * The sequence is fixed; the values (oscillator, PLL factors, regulator scale, over-drive, wait
 * states, dividers) come from the generated hw_config.h, where emb-hwgen's solver checked them
 * against the SoC's limits.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#include <emb_stm32_drivers.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define RCC_CR      REG32(EMB_HW_RCC_BASE + 0x00u)
#define RCC_PLLCFGR REG32(EMB_HW_RCC_BASE + 0x04u)
#define RCC_CFGR    REG32(EMB_HW_RCC_BASE + 0x08u)
#define CR_HSEON    (1u << 16)
#define CR_HSERDY   (1u << 17)
#define CR_HSEBYP   (1u << 18)
#define CR_PLLON    (1u << 24)
#define CR_PLLRDY   (1u << 25)

#define CFGR_SW_MASK     0x3u
#define CFGR_SWS_MASK    (0x3u << 2)
#define CFGR_SWS_SHIFT   2u
#define CFGR_HPRE_SHIFT  4u
#define CFGR_PPRE1_SHIFT 10u
#define CFGR_PPRE2_SHIFT 13u
#define CFGR_DIV_MASK    ((0x3Fu << CFGR_PPRE1_SHIFT) | (0xFu << CFGR_HPRE_SHIFT))

/* the CFGR divider encodings (RM0390 §6.3.3) */
#define APB_BITS(d) ((d) == 1u ? 0u : (d) == 2u ? 4u : (d) == 4u ? 5u : (d) == 8u ? 6u : 7u)
#define AHB_BITS(d)      \
    ((d) == 1u     ? 0u  \
     : (d) == 2u   ? 8u  \
     : (d) == 4u   ? 9u  \
     : (d) == 8u   ? 10u \
     : (d) == 16u  ? 11u \
     : (d) == 64u  ? 12u \
     : (d) == 128u ? 13u \
     : (d) == 256u ? 14u \
                   : 15u)
#define CFGR_DIVIDERS                                  \
    ((AHB_BITS(EMB_HW_AHB_DIV) << CFGR_HPRE_SHIFT) |   \
     (APB_BITS(EMB_HW_APB1_DIV) << CFGR_PPRE1_SHIFT) | \
     (APB_BITS(EMB_HW_APB2_DIV) << CFGR_PPRE2_SHIFT))

#if EMB_HW_SYSCLK_SOURCE == EMB_HW_SYSCLK_PLL
#define PLLCFGR_SRC_HSE (1u << 22)
#ifdef EMB_HW_PLL_R
#define PLLCFGR_R (EMB_HW_PLL_R << 28)
#else
#define PLLCFGR_R 0u
#endif
#define PLLCFGR_VALUE                                                          \
    (EMB_HW_PLL_M | (EMB_HW_PLL_N << 6) | (((EMB_HW_PLL_P / 2u) - 1u) << 16) | \
     (EMB_HW_PLL_SOURCE_HSE ? PLLCFGR_SRC_HSE : 0u) | (EMB_HW_PLL_Q << 24) | PLLCFGR_R)
#endif

#define PWR_CR       REG32(EMB_HW_PWR_BASE + 0x00u)
#define PWR_CSR      REG32(EMB_HW_PWR_BASE + 0x04u)
#define PWR_VOS_MASK (0x3u << 14)
#define PWR_ODEN     (1u << 16)
#define PWR_ODSWEN   (1u << 17)
#define CSR_ODRDY    (1u << 16)
#define CSR_ODSWRDY  (1u << 17)

#define FLASH_ACR    REG32(EMB_HW_FLASH_IF_BASE + 0x00u)
#define ACR_LAT_MASK 0xFu
#define ACR_PRFTEN   (1u << 8)
#define ACR_ICEN     (1u << 9)
#define ACR_DCEN     (1u << 10)

#define USES_HSE                                  \
    (EMB_HW_SYSCLK_SOURCE == EMB_HW_SYSCLK_HSE || \
     (EMB_HW_SYSCLK_SOURCE == EMB_HW_SYSCLK_PLL && EMB_HW_PLL_SOURCE_HSE))

/* The ready flags' bound: HAL_RCC's HSE start-up timeout is 100 ms, about 400 000 polls at
 * HSI's 16 MHz; PLL lock and the over-drive switch take microseconds. */
#define WAIT_BOUND 500000u

/* Polls until (reg & mask) == want, at most WAIT_BOUND times; nonzero once it holds. */
static int wait_for(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    uint32_t n;
    for (n = 0u; n < WAIT_BOUND; n++) { /* bounded by WAIT_BOUND */
        if ((*reg & mask) == want) {
            return 1;
        }
    }
    return 0;
}

void emb_stm32_clock_enable(uint32_t offset, uint32_t bit)
{
    REG32(EMB_HW_RCC_BASE + offset) |= 1u << bit;
    (void)REG32(EMB_HW_RCC_BASE + offset); /* the clock reaches the peripheral first */
}

int emb_stm32f4_clock_init(void)
{
    emb_stm32_clock_enable(EMB_HW_PWR_CLKEN_OFFSET, EMB_HW_PWR_CLKEN_BIT);
    PWR_CR = (PWR_CR & ~PWR_VOS_MASK) | (EMB_HW_PWR_VOS << 14);
#if USES_HSE
#if EMB_HW_HSE_BYPASS
    RCC_CR |= CR_HSEBYP;
#endif
    RCC_CR |= CR_HSEON;
    if (wait_for(&RCC_CR, CR_HSERDY, CR_HSERDY) == 0) {
        return 0; /* no HSE input: the board's oscillator or clock source is missing */
    }
#endif
#if EMB_HW_SYSCLK_SOURCE == EMB_HW_SYSCLK_PLL
    RCC_PLLCFGR = PLLCFGR_VALUE;
    RCC_CR |= CR_PLLON;
    if (wait_for(&RCC_CR, CR_PLLRDY, CR_PLLRDY) == 0) {
        return 0;
    }
#endif
#if EMB_HW_PWR_OVERDRIVE
    PWR_CR |= PWR_ODEN;
    if (wait_for(&PWR_CSR, CSR_ODRDY, CSR_ODRDY) == 0) {
        return 0;
    }
    PWR_CR |= PWR_ODSWEN;
    if (wait_for(&PWR_CSR, CSR_ODSWRDY, CSR_ODSWRDY) == 0) {
        return 0;
    }
#endif
    FLASH_ACR = EMB_HW_FLASH_WAIT_STATES | ACR_PRFTEN | ACR_ICEN | ACR_DCEN;
    if (wait_for(&FLASH_ACR, ACR_LAT_MASK, EMB_HW_FLASH_WAIT_STATES) == 0) {
        return 0;
    }
    RCC_CFGR = (RCC_CFGR & ~(CFGR_SW_MASK | CFGR_DIV_MASK)) | CFGR_DIVIDERS;
    RCC_CFGR = (RCC_CFGR & ~CFGR_SW_MASK) | EMB_HW_SYSCLK_SOURCE;
    return wait_for(&RCC_CFGR, CFGR_SWS_MASK, (uint32_t)EMB_HW_SYSCLK_SOURCE << CFGR_SWS_SHIFT);
}

/* RCC_CSR (RM0390 §6.3.27): the reset flags of the last reset, read once and cleared so that
 * the next reset's are its own; every later call gives the same flags, so the watchdog's
 * driver and an application both see this boot's cause. */
#define RCC_CSR  REG32(EMB_HW_RCC_BASE + 0x74u)
#define CSR_RMVF (1u << 24)

uint32_t emb_stm32f4_reset_flags_take(void)
{
    static uint32_t boot_flags;
    static uint8_t boot_flags_read;
    if (boot_flags_read == 0u) {
        boot_flags = RCC_CSR & EMB_STM32F4_RESET_MASK;
        RCC_CSR |= CSR_RMVF;
        boot_flags_read = 1u;
    }
    return boot_flags;
}
