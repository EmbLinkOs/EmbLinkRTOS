/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    board.c
 * @brief   Board support for every ATmega board described in YAML.
 *
 * @details
 * The emb_board.h interface over the megaAVR drivers, with the console and LED from the board's
 * generated hw_config.h (SPEC-012 §11, SPEC-014). The clock is the board's oscillator, selected by
 * the fuses: nothing to set up.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#include <emb/irq.h>

#include <emb_avr_drivers.h>
#include <emb_board.h>
#include <hw_config.h>

void emb_board_init(void)
{
    emb_avr_usart_init(EMB_HW_CONSOLE_BASE,
                       EMB_AVR_UBRR_2X(EMB_HW_CONSOLE_CLOCK_HZ, EMB_HW_CONSOLE_BAUD));
#ifdef EMB_HW_LED_PIN
    emb_avr_gpio_output(EMB_HW_LED_PORT_BASE, EMB_HW_LED_PIN);
#endif
}

void emb_board_led_toggle(void)
{
#ifdef EMB_HW_LED_PIN
    emb_avr_gpio_toggle(EMB_HW_LED_PORT_BASE, EMB_HW_LED_PIN);
#endif
}

void emb_board_puts(const char *s)
{
    while (*s != '\0') { /* bounded by the string */
        emb_avr_usart_putc(EMB_HW_CONSOLE_BASE, *s);
        s++;
    }
}

void emb_board_put_u32(uint32_t v)
{
    char buf[11];
    unsigned i = sizeof(buf) - 1u;
    buf[i] = '\0';
    do { /* at most ten digits */
        i--;
        buf[i] = (char)('0' + (char)(v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0u);
    emb_board_puts(&buf[i]);
}

void emb_board_exit(int code)
{
    (void)code;
    (void)emb_irq_lock(); /* never unlocked: the image is over */
    for (;;) {
    }
}
