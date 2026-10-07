/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Test platform for the native port: host stdio, fork-based fault checks, an injected interrupt. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <emb_native.h>
#include <emb_test.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_IRQ 5u

void emb_test_logf(const char *fmt_flash, ...)
{
    va_list ap;
    va_start(ap, fmt_flash);
    (void)vprintf(fmt_flash, ap);
    va_end(ap);
}

void emb_test_puts_flash(const char *s_flash)
{
    (void)fputs(s_flash, stdout);
}

void emb_test_puts(const char *s)
{
    (void)fputs(s, stdout);
}

void emb_test_exit(int code)
{
    (void)fflush(stdout);
    emb_native_exit(code);
}

static void (*bound_dispatch)(void);

static void test_isr(void *arg)
{
    (void)arg;
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
    EMB_CHECK(emb_irq_connect(TEST_IRQ, test_isr, NULL));
}

void emb_test_platform_irq_trigger(void)
{
    emb_native_irq_raise(TEST_IRQ);
}

/* ---- fault expectation by fork (TEST-010) -------------------------------------- */

static pid_t fork_child = -1;

bool emb_test_fork_begin(void)
{
    (void)fflush(NULL);
    fork_child = fork();
    if (fork_child == 0) {
        alarm(5u); /* the misuse must fault before any switch; never hang */
        return true;
    }
    return false;
}

void emb_test_fork_child_no_fault(void)
{
    _exit(99);
}

void emb_test_fork_expect_fault(unsigned line, const char *what_flash)
{
    int status = 0;
    if (fork_child <= 0) {
        emb_test_fail(line, "fork failed");
        return;
    }
    (void)waitpid(fork_child, &status, 0);
    fork_child = -1;
#if CONFIG_EMB_CHECKED
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EMB_NATIVE_EXIT_FAULT) {
        (void)printf("  child exit status %d (signaled %d)\n",
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                     WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        emb_test_fail(line, what_flash);
    }
#else
    (void)what_flash;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 99) {
        emb_test_fail(line, "release build: misuse must return a status, not fault");
    }
#endif
}
