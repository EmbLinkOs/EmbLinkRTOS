/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* arduino_uno board init: USART0 console at 115200 8N1, PB5 as output (SPEC-012 §11). */
#include <avr/io.h>
#include <emb_board.h>
#include <hw_config.h>

void emb_board_init(void)
{
    uint16_t ubrr =
        (uint16_t)(((EMB_HW_F_CPU / 8UL) / EMB_HW_CONSOLE_BAUD) - 1UL); /* double speed */
    UCSR0A = (uint8_t)(1u << U2X0);
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFFu);
    UCSR0C = (uint8_t)((1u << UCSZ01) | (1u << UCSZ00)); /* 8 data bits, no parity, 1 stop */
    UCSR0B = (uint8_t)(1u << TXEN0);
    EMB_HW_LED_DDR |= (uint8_t)(1u << EMB_HW_LED_BIT);
}

void emb_board_led_toggle(void)
{
    EMB_HW_LED_PORT ^= (uint8_t)(1u << EMB_HW_LED_BIT);
}

static void put_char(char c)
{
    while ((UCSR0A & (uint8_t)(1u << UDRE0)) == 0u) { /* bounded by one character time */
    }
    UDR0 = (uint8_t)c;
}

void emb_board_puts(const char *s)
{
    while (*s != '\0') {
        put_char(*s);
        s++;
    }
}

void emb_board_put_u32(uint32_t v)
{
    char buf[11];
    unsigned i = sizeof(buf) - 1u;
    buf[i] = '\0';
    do {
        i--;
        buf[i] = (char)('0' + (char)(v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0u);
    emb_board_puts(&buf[i]);
}
