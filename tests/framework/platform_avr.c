/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Test platform for the AVR port: console output through the board's USART, strings in
 * flash, no fork, and a software interrupt built on the USART data-register-empty
 * interrupt: enabling UDRIE0 raises USART_UDRE at once (level-pending while masked,
 * taken at the unlock), and the handler disables it again. QEMU's arduino-uno machine
 * emulates it, and so does the board.
 */
#include <stdarg.h>
#include <stdio.h>

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <emb_board.h>
#include <emb_test.h>

void emb_test_logf(const char *fmt_flash, ...)
{
    char buf[80];
    va_list ap;
    va_start(ap, fmt_flash);
    (void)vsnprintf_P(buf, sizeof(buf), fmt_flash, ap);
    va_end(ap);
    emb_board_puts(buf);
}

void emb_test_puts_flash(const char *s_flash)
{
    char c = (char)pgm_read_byte(s_flash);
    while (c != '\0') { /* bounded by the string */
        char two[2];
        two[0] = c;
        two[1] = '\0';
        emb_board_puts(two);
        s_flash++;
        c = (char)pgm_read_byte(s_flash);
    }
}

void emb_test_puts(const char *s)
{
    emb_board_puts(s);
}

void emb_test_exit(int code)
{
    (void)code;
    emb_test_puts_flash(PSTR("EMB_TEST_END\r\n"));
    (void)emb_irq_lock(); /* never unlocked: the run is over */
    for (;;) {
    }
}

static void (*bound_dispatch)(void);
static volatile uint8_t fired; /* written by the handler, polled by the raiser */
static uint8_t masked_before;  /* the count at a raise made while masked */
static uint8_t masked_pending; /* such a raise awaits emb_test_irq_settle() */

EMB_ISR(USART_UDRE)
{
    UCSR0B &= (uint8_t) ~(uint8_t)(1u << UDRIE0); /* one shot */
    fired++;
    if (bound_dispatch != NULL) {
        bound_dispatch();
    }
}

void emb_test_platform_init(void)
{
}

void emb_test_platform_irq_bind(void (*dispatch)(void))
{
    bound_dispatch = dispatch;
}

void emb_test_platform_irq_trigger(void)
{
    uint8_t before = fired;
    uint16_t spin = 0u;
    /* the data register is still busy for one character time after the last console
     * byte; wait until it is empty so that enabling the interrupt makes it pending now */
    while ((UCSR0A & (uint8_t)(1u << UDRE0)) == 0u && spin < 20000u) {
        spin++;
    }
    spin = 0u;
    UCSR0B |= (uint8_t)(1u << UDRIE0);
    if (emb_irq_is_locked()) {
        masked_before = before;
        masked_pending = 1u;
        return; /* level-pending: delivered at the unlock */
    }
    /* unmasked: the interrupt is taken within a character time; wait for it so that
     * delivery is as immediate as the native port's (bounded spin) */
    while (fired == before && spin < 20000u) {
        spin++;
    }
}

void emb_test_platform_irq_settle(void)
{
    uint16_t spin = 0u;
    emb_irq_key_t key;
    if (masked_pending == 0u) {
        return;
    }
    masked_pending = 0u;
    /* The hardware took the interrupt one instruction after the unmask. QEMU takes a
     * pending interrupt only when it returns to its main loop, which a `sei` inside a
     * chain of translated blocks does not force; re-asserting the enable bit does
     * (the device signals the line again), so the handler runs here at the latest.
     * Under cli so that a handler that already ran cannot be re-armed by the write. */
    key = emb_irq_lock();
    if (fired == masked_before) {
        UCSR0B |= (uint8_t)(1u << UDRIE0);
    }
    emb_irq_unlock(key);
    while (fired == masked_before && spin < 20000u) {
        spin++;
    }
}

bool emb_test_fork_begin(void)
{
    return false;
}

void emb_test_fork_child_no_fault(void)
{
    for (;;) {
    }
}

void emb_test_fork_expect_fault(unsigned line, const char *what_flash)
{
    (void)line;
    (void)what_flash;
}
