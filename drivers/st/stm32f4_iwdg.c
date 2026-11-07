/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* STM32F4 independent watchdog (RM0390 Rev 6 §19): once started it cannot be stopped, and it
 * resets the part when not reloaded within its timeout. Clocked by the ~32 kHz LSI. */
#include <emb_stm32_drivers.h>

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define IWDG_KR    0x00u
#define IWDG_PR    0x04u
#define IWDG_RLR   0x08u
#define KEY_ACCESS 0x5555u
#define KEY_RELOAD 0xAAAAu
#define KEY_START  0xCCCCu
#define PR_DIV32   3u /* ~1 kHz: the reload value is the timeout in milliseconds */
#define RLR_MAX    0xFFFu

void emb_stm32f4_iwdg_start(uintptr_t base, uint32_t timeout_ms)
{
    uint32_t rl = (timeout_ms > RLR_MAX) ? RLR_MAX : timeout_ms;
    REG32(base + IWDG_KR) = KEY_START; /* starts the LSI and the counter */
    REG32(base + IWDG_KR) = KEY_ACCESS;
    REG32(base + IWDG_PR) = PR_DIV32;
    REG32(base + IWDG_RLR) = rl;
    REG32(base + IWDG_KR) = KEY_RELOAD;
}

void emb_stm32f4_iwdg_kick(uintptr_t base)
{
    REG32(base + IWDG_KR) = KEY_RELOAD;
}
