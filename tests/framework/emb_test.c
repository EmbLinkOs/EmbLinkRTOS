/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Test runner: main() starts the kernel, a runner thread executes every EMB_TEST. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <emb_board.h>
#include <emb_native.h>
#include <emb_test.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern const emb_test_case_t __start_emb_test_table[]
    EMB_WEAK; /* NOLINT(bugprone-reserved-identifier) */
extern const emb_test_case_t __stop_emb_test_table[]
    EMB_WEAK; /* NOLINT(bugprone-reserved-identifier) */

static unsigned failures;
static unsigned current_failed;
static const char *current_name;
static const char *filter;

void emb_test_logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    (void)vprintf(fmt, ap);
    va_end(ap);
    (void)printf("\n");
}

void emb_test_fail(const char *file, int line, const char *what)
{
    (void)printf("  FAIL %s:%d: %s\n", file, line, what);
    current_failed = 1u;
}

void emb_test_note_reqs(const char *first, ...)
{
    (void)first; /* collected statically by the traceability tool */
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

void emb_test_fork_expect_fault(const char *file, int line, const char *what)
{
    int status = 0;
    if (fork_child <= 0) {
        emb_test_fail(file, line, "fork failed");
        return;
    }
    (void)waitpid(fork_child, &status, 0);
    fork_child = -1;
#if CONFIG_EMB_CHECKED
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EMB_NATIVE_EXIT_FAULT) {
        (void)printf("  child exit status %d (signaled %d)\n",
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                     WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        emb_test_fail(file, line, what);
    }
#else
    (void)what;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 99) {
        emb_test_fail(file, line, "release build: misuse must return a status, not fault");
    }
#endif
}

/* ---- helper threads ------------------------------------------------------------- */

static emb_thread_storage_t pool_storage[EMB_TEST_MAX_THREADS];
static EMB_ALIGNED(EMB_STACK_ALIGN) uint8_t pool_stack[EMB_TEST_MAX_THREADS][EMB_TEST_STACK_SIZE];
static emb_thread_t pool_handle[EMB_TEST_MAX_THREADS];
static uint8_t pool_used[EMB_TEST_MAX_THREADS];

emb_thread_t emb_test_thread(const char *name, uint8_t priority, uint8_t flags,
                             emb_thread_entry_t entry, void *arg)
{
    unsigned i;
    emb_thread_attr_t attr;
    emb_thread_t h;
    h.raw = 0u;
    for (i = 0u; i < (unsigned)EMB_TEST_MAX_THREADS; i++) {
        if (pool_used[i] == 0u) {
            break;
        }
    }
    if (i == (unsigned)EMB_TEST_MAX_THREADS) {
        emb_test_fail(__FILE__, __LINE__, "helper thread pool exhausted");
        return h;
    }
    emb_thread_attr_default(&attr);
    attr.name = name;
    attr.priority = priority;
    attr.flags = flags;
    attr.stack = pool_stack[i];
    attr.stack_size = sizeof(pool_stack[i]);
    EMB_ASSERT_OK(emb_thread_init(&pool_storage[i], &attr, entry, arg, &h));
    if (!EMB_HANDLE_IS_NULL(h)) {
        pool_used[i] = 1u;
        pool_handle[i] = h;
        EMB_ASSERT_OK(emb_thread_start(h));
    }
    return h;
}

void emb_test_threads_reset(void)
{
    unsigned i;
    for (i = 0u; i < (unsigned)EMB_TEST_MAX_THREADS; i++) {
        if (pool_used[i] != 0u) {
            uint8_t state = 0u;
            (void)emb_thread_state(pool_handle[i], &state, NULL);
            if (state == EMB_THREAD_TERMINATED) {
                (void)emb_thread_detach(pool_handle[i]);
            }
            if (emb_thread_destroy(pool_handle[i]) == EMB_OK) {
                pool_used[i] = 0u;
            }
        }
    }
}

/* ---- runner ---------------------------------------------------------------------- */

static void runner(void *arg)
{
    const emb_test_case_t *tc = __start_emb_test_table;
    const emb_test_case_t *stop = __stop_emb_test_table;
    unsigned ran = 0u;
    (void)arg;
    for (; tc != NULL && tc < stop; tc++) {
        if (filter != NULL && strstr(tc->name, filter) == NULL) {
            continue;
        }
        current_name = tc->name;
        current_failed = 0u;
        (void)printf("TEST %s\n", tc->name);
        (void)fflush(stdout);
        tc->fn();
        emb_test_threads_reset();
        ran++;
        if (current_failed != 0u) {
            failures++;
            (void)printf("  -> FAILED\n");
        } else {
            (void)printf("  -> ok\n");
        }
    }
    (void)printf("%u tests, %u failed, %lu switches, virtual time %lu ticks\n", ran, failures,
                 (unsigned long)emb_native_switch_count(), (unsigned long)emb_native_virtual_now());
    emb_native_exit((failures != 0u) ? 1 : 0);
}

static emb_thread_storage_t runner_storage;
static EMB_ALIGNED(EMB_STACK_ALIGN) uint8_t runner_stack[EMB_TEST_STACK_SIZE];

int main(int argc, char **argv)
{
    emb_thread_attr_t attr;
    emb_thread_t h;
    if (argc > 1) {
        filter = argv[1];
    }
    emb_board_init();
    emb_kernel_init();
    emb_thread_attr_default(&attr);
    attr.name = "runner";
    attr.priority = EMB_TEST_RUNNER_PRIORITY;
    attr.stack = runner_stack;
    attr.stack_size = sizeof(runner_stack);
    EMB_CHECK(emb_thread_init(&runner_storage, &attr, runner, NULL, &h));
    EMB_CHECK(emb_thread_start(h));
    emb_kernel_start();
}
