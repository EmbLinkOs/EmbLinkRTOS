/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Minimal test framework for the conformance and kernel suites (05 §4.3, CODING-STANDARD
 * §14). Tests run inside the kernel on a low-priority runner thread; each EMB_TEST
 * function creates the threads it needs at higher priorities and synchronizes with
 * kernel primitives only (CS-14.2). Requirement citations (EMB_TEST_REQ) are collected
 * by the traceability generator (TEST-009).
 */
#ifndef EMB_TEST_H
#define EMB_TEST_H

#include <emb/emb.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*emb_test_fn_t)(void);

/* Aligned to a power of two not below its size so that the linker-bracketed table walks
 * with pointer arithmetic: GCC over-aligns aggregates per input section otherwise. */
typedef struct EMB_ALIGNED(sizeof(void *) * 4) emb_test_case {
    const char *name;
    emb_test_fn_t fn;
    const char *reqs;
} emb_test_case_t;

/* Defines a test and registers it. MISRA Dev CS-12: rule 20.7, `name` is a token. */
#define EMB_TEST(name)                                              \
    static void name(void);                                         \
    static const emb_test_case_t name##_case_                       \
        EMB_USED EMB_SECTION("emb_test_table") = {#name, name, ""}; \
    static void name(void)

/* Requirement citation inside a test body: collected by the traceability tool. */
#define EMB_TEST_REQ(...) emb_test_note_reqs(__VA_ARGS__, (const char *)0)

void emb_test_note_reqs(const char *first, ...);
void emb_test_fail(const char *file, int line, const char *what);
void emb_test_logf(const char *fmt, ...) EMB_PRINTF_LIKE(1, 2);

#define EMB_ASSERT_TRUE(cond)                                      \
    do {                                                           \
        if (!(cond)) {                                             \
            emb_test_fail(__FILE__, __LINE__, "expected: " #cond); \
        }                                                          \
    } while (0)

#define EMB_ASSERT_EQ(a, b)                                                     \
    do {                                                                        \
        long long va_ = (long long)(a);                                         \
        long long vb_ = (long long)(b);                                         \
        if (va_ != vb_) {                                                       \
            emb_test_logf("  %s = %lld, %s = %lld", #a, va_, #b, vb_);          \
            emb_test_fail(__FILE__, __LINE__, "expected equal: " #a " == " #b); \
        }                                                                       \
    } while (0)

#define EMB_ASSERT_STATUS(expr, expected)                                               \
    do {                                                                                \
        emb_status_t st_ = (expr);                                                      \
        if (st_ != (expected)) {                                                        \
            emb_test_logf("  %s returned %s, expected %s", #expr, emb_status_name(st_), \
                          emb_status_name(expected));                                   \
            emb_test_fail(__FILE__, __LINE__, "status mismatch");                       \
        }                                                                               \
    } while (0)

#define EMB_ASSERT_OK(expr) EMB_ASSERT_STATUS((expr), EMB_OK)

/* Runs `stmt` in a forked copy of the process and asserts that it ends in a kernel
 * fault (checked builds, TEST-010). The copy carries only the calling host thread, so
 * `stmt` must fault before any switch. */
#define EMB_ASSERT_FAULTS(stmt)                                \
    do {                                                       \
        if (emb_test_fork_begin()) {                           \
            stmt;                                              \
            emb_test_fork_child_no_fault();                    \
        }                                                      \
        emb_test_fork_expect_fault(__FILE__, __LINE__, #stmt); \
    } while (0)

bool emb_test_fork_begin(void);
EMB_NORETURN void emb_test_fork_child_no_fault(void);
void emb_test_fork_expect_fault(const char *file, int line, const char *what);

/* A misuse: faults in checked builds, returns @status in release builds (SPEC-001 §5.3). */
#if CONFIG_EMB_CHECKED
#define EMB_ASSERT_MISUSE(expr, status) EMB_ASSERT_FAULTS((void)(expr))
#else
#define EMB_ASSERT_MISUSE(expr, status) EMB_ASSERT_STATUS((expr), (status))
#endif

/* Event marks: threads and handlers push small integers; the runner checks the order. */
void emb_test_mark(int code);
void emb_test_marks_clear(void);
unsigned emb_test_marks_count(void);
int emb_test_mark_at(unsigned i);
void emb_test_marks_check(const char *file, int line, const int *expected, size_t n);
#define EMB_ASSERT_MARKS(...)                                            \
    emb_test_marks_check(__FILE__, __LINE__, (const int[]){__VA_ARGS__}, \
                         sizeof((const int[]){__VA_ARGS__}) / sizeof(int))

/* Virtual ticks since the kernel started. */
#define EMB_TEST_NOW() ((uint32_t)emb_time_now().ticks)

/* Helper threads: a fixed pool of stacks and storages the tests draw from. */
#define EMB_TEST_MAX_THREADS 8
#define EMB_TEST_STACK_SIZE  4096

emb_thread_t emb_test_thread(const char *name, uint8_t priority, uint8_t flags,
                             emb_thread_entry_t entry, void *arg);
void emb_test_threads_reset(void); /* destroy every helper thread that is reusable */
emb_status_t emb_test_thread_destroy(emb_thread_t h); /* destroy a helper and free its pool slot */

/* Advance virtual time by sleeping; the runner's priority is the lowest. */
#define EMB_TEST_RUNNER_PRIORITY 1u

#ifdef __cplusplus
}
#endif

#endif /* EMB_TEST_H */
