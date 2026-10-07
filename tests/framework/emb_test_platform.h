/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The per-port part of the test framework: output, flash strings, exit, fork, sizes,
 * and the software interrupt. One implementation per port under tests/framework/.
 */
#ifndef EMB_TEST_PLATFORM_H
#define EMB_TEST_PLATFORM_H

#include <emb/emb.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_EMB_ARCH_AVR
#include <avr/pgmspace.h>
#define EMB_TEST_STR(s)       PSTR(s)
#define EMB_TEST_HAS_FORK     0
#define EMB_TEST_EXACT_TIME   0   /* real time under QEMU and on the board */
#define EMB_TEST_STACK_SIZE   160 /* helpers: frame 35 + kernel depth + the 64-byte ISR reserve */
#define EMB_TEST_RUNNER_STACK 256 /* the runner formats output (vsnprintf) */
#define EMB_TEST_MAX_THREADS  3   /* the suites need at most three helpers at once */
#define EMB_TEST_MARKS_MAX    32
#define EMB_TEST_ISR(name)    static void name(void *emb_isr_arg_)
#else
#define EMB_TEST_STR(s)       (s)
#define EMB_TEST_HAS_FORK     1
#define EMB_TEST_EXACT_TIME   1 /* virtual time (SPEC-013 §6) */
#define EMB_TEST_STACK_SIZE   4096
#define EMB_TEST_RUNNER_STACK 4096
#define EMB_TEST_MAX_THREADS  8
#define EMB_TEST_MARKS_MAX    128
#define EMB_TEST_ISR(name)    EMB_ISR(name)
#endif

/* Object names: kept only where the kernel keeps them (CONFIG_EMB_OBJ_NAMES), so the
 * tiny profile does not pay RAM for name literals. */
#if CONFIG_EMB_OBJ_NAMES
#define EMB_TEST_NAME(s) (s)
#else
#define EMB_TEST_NAME(s) ((const char *)0)
#endif

/* Formatted output; the format string is a flash string on Harvard ports. */
void emb_test_logf(const char *fmt_flash, ...);
void emb_test_puts_flash(const char *s_flash);
void emb_test_puts(const char *s);

/* End the run: the process exit status on native, a marker on the console elsewhere. */
EMB_NORETURN void emb_test_exit(int code);

void emb_test_platform_init(void);

/* Port-level software interrupt, used by the framework's slot layer. */
void emb_test_platform_irq_bind(void (*dispatch)(void));
void emb_test_platform_irq_trigger(void);

#ifdef __cplusplus
}
#endif

#endif /* EMB_TEST_PLATFORM_H */
