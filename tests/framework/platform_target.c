/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Test platform for the 32-bit embedded ports (Cortex-M, RISC-V): console output through
 * the board, a small formatter instead of the C library's, no fork, the run's end through
 * the board (an emulator exit), and a real software interrupt: the board names a line no
 * device drives (EMB_HW_TEST_IRQ) and the test pends it through the interrupt controller.
 */
#include <stdarg.h>

#include <emb_board.h>
#include <emb_test.h>

/* ---- output ---------------------------------------------------------------------------- */

static void put_unsigned(char *out, size_t cap, size_t *n, unsigned long v, unsigned base)
{
    char digits[24];
    unsigned i = 0u;
    do { /* bounded by the digits of an unsigned long */
        unsigned d = (unsigned)(v % base);
        digits[i] = (char)((d < 10u) ? ('0' + (char)d) : ('a' + (char)(d - 10u)));
        i++;
        v /= base;
    } while (v != 0u && i < sizeof(digits));
    while (i > 0u) {
        i--;
        if (*n + 1u < cap) {
            out[*n] = digits[i];
            (*n)++;
        }
    }
}

/* %d %u %x %s %c %% with an optional l: what the framework and the suites use. */
static void format(char *out, size_t cap, const char *fmt, va_list ap)
{
    size_t n = 0u;
    while (*fmt != '\0' && n + 1u < cap) { /* bounded by the format and the buffer */
        bool is_long = false;
        if (*fmt != '%') {
            out[n] = *fmt;
            n++;
            fmt++;
            continue;
        }
        fmt++;
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
        }
        switch (*fmt) {
        case 'd': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            unsigned long m = (unsigned long)v;
            if (v < 0) {
                out[n] = '-';
                n++;
                m = 0u - m;
            }
            put_unsigned(out, cap, &n, m, 10u);
            break;
        }
        case 'u':
            put_unsigned(out, cap, &n,
                         is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned),
                         10u);
            break;
        case 'x':
            put_unsigned(out, cap, &n,
                         is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned),
                         16u);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            while (*s != '\0' && n + 1u < cap) {
                out[n] = *s;
                n++;
                s++;
            }
            break;
        }
        case 'c':
            out[n] = (char)va_arg(ap, int);
            n++;
            break;
        case '%':
            out[n] = '%';
            n++;
            break;
        default:
            out[n] = '?';
            n++;
            break;
        }
        if (*fmt != '\0') {
            fmt++;
        }
    }
    out[n] = '\0';
}

void emb_test_logf(const char *fmt_flash, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt_flash);
    format(buf, sizeof(buf), fmt_flash, ap);
    va_end(ap);
    emb_board_puts(buf);
}

void emb_test_puts_flash(const char *s_flash)
{
    emb_board_puts(s_flash);
}

void emb_test_puts(const char *s)
{
    emb_board_puts(s);
}

void emb_test_exit(int code)
{
    emb_board_puts("EMB_TEST_END\n");
    emb_board_exit(code);
}

/* ---- software interrupt ------------------------------------------------------------------ */

static void (*bound_dispatch)(void);
static volatile uint32_t fired; /* written by the handler, polled by the raiser */
static uint32_t masked_before;
static unsigned masked_pending;

static void test_isr(void *arg)
{
    (void)arg;
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
    EMB_CHECK(emb_irq_connect((emb_irq_t)EMB_HW_TEST_IRQ, test_isr, NULL));
    EMB_CHECK(emb_irq_enable((emb_irq_t)EMB_HW_TEST_IRQ));
}

void emb_test_platform_irq_trigger(void)
{
    uint32_t before = fired;
    uint32_t spin = 0u;
    EMB_CHECK(emb_irq_pend((emb_irq_t)EMB_HW_TEST_IRQ));
    if (emb_irq_is_locked() || emb_in_isr()) {
        masked_before = before;
        masked_pending = 1u;
        return; /* pending: taken at the unlock, or when the running handler returns */
    }
    /* unmasked: taken within a few instructions; bounded spin for an emulator */
    while (fired == before && spin < 1000000u) {
        spin++;
    }
}

void emb_test_platform_irq_settle(void)
{
    uint32_t spin = 0u;
    if (masked_pending == 0u) {
        return;
    }
    masked_pending = 0u;
    while (fired == masked_before && spin < 1000000u) { /* bounded */
        spin++;
    }
}

/* ---- no fork on a microcontroller (TEST-010 runs on the native port) ------------------------ */

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
