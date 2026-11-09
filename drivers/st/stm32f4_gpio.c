/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* STM32F4 GPIO (RM0390 Rev 6 §7.4): the board's pin table and output setting and toggling. */
#include <emb_stm32_drivers.h>
#include <hw_config.h>

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define GPIO_MODER 0x00u
#define GPIO_ODR   0x14u
#define GPIO_BSRR  0x18u
#define GPIO_AFRL  0x20u /* pins 0 to 7; AFRH (0x24) holds 8 to 15 */

void emb_stm32_pins_apply(const emb_stm32_pin_t *pins, unsigned count)
{
    unsigned i;
    for (i = 0u; i < count; i++) { /* bounded by the board's pin table */
        const emb_stm32_pin_t *p = &pins[i];
        uint32_t pin = p->pin;
        emb_stm32_clock_enable(p->clken_offset, p->clken_bit);
        if (p->mode == EMB_STM32_PIN_OUTPUT && p->level != 0u) { /* the level before the mode */
            REG32(p->port_base + GPIO_BSRR) = 1u << pin;
        }
        if (p->mode == EMB_STM32_PIN_AF) { /* the function first, so the pin never shows another */
            uintptr_t afr = p->port_base + GPIO_AFRL + ((pin >> 3) * 4u);
            uint32_t shift = (pin & 7u) * 4u;
            REG32(afr) = (REG32(afr) & ~(0xFu << shift)) | ((uint32_t)p->af << shift);
        }
        REG32(p->port_base + GPIO_MODER) =
            (REG32(p->port_base + GPIO_MODER) & ~(3u << (pin * 2u))) |
            ((uint32_t)p->mode << (pin * 2u));
    }
}

void emb_stm32_gpio_toggle(uintptr_t port, unsigned pin)
{
    uint32_t bit = 1u << pin;
    REG32(port + GPIO_BSRR) = ((REG32(port + GPIO_ODR) & bit) != 0u) ? (bit << 16) : bit;
}

void emb_stm32_gpio_set(uintptr_t port, unsigned pin)
{
    REG32(port + GPIO_BSRR) = 1u << pin;
}

void emb_stm32_gpio_clear(uintptr_t port, unsigned pin)
{
    REG32(port + GPIO_BSRR) = 1u << (pin + 16u);
}
