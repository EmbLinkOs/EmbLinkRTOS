/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Minimal test framework for the conformance and kernel suites (05 §4.3, CODING-STANDARD
 * §14). Tests run inside the kernel on a low-priority runner thread; each EMB_TEST
 * function creates the threads it needs at higher priorities and synchronizes with
 * kernel primitives only (CS-14.2). The same sources run on every port: the platform
 * layer (emb_test_platform.h) supplies output, the software interrupt, and the exit.
 * Requirement citations (EMB_TEST_REQ) are collected by the traceability generator.
 */
#ifndef EMB_TEST_H
#define EMB_TEST_H

#include <emb/emb.h>

#include <emb_test_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*emb_test_fn_t)(void);

/* Aligned to a power of two not below its size so that the linker-bracketed table walks
 * with pointer arithmetic: GCC over-aligns aggregates per input section otherwise. */
typedef struct EMB_ALIGNED(sizeof(void *) * 4) emb_test_case {
    const char *name; /* in flash on Harvard ports: print with emb_test_puts_flash */
    emb_test_fn_t fn;
    const char *reqs;
} emb_test_case_t;

/* Defines a test and registers it. MISRA Dev CS-12: rule 20.7, `name` is a token. */
#define EMB_TEST(name)                                                                        \
    static void name(void);                                                                   \
    static EMB_FLASH_CONST char name##_name_[] = #name;                                       \
    static EMB_TABLE_CONST emb_test_case_t name##_case_                                       \
        EMB_USED EMB_SECTION(EMB_TABLE_SECTION("emb_test_table")) = {name##_name_, name, ""}; \
    static void name(void)

/* Requirement citation inside a test body: collected by the traceability tool. */
#define EMB_TEST_REQ(...) emb_test_note_reqs(__VA_ARGS__, (const char *)0)

void emb_test_note_reqs(const char *first, ...);
void emb_test_fail(unsigned line, const char *what_flash);
void emb_test_skip(const char *why_flash);

/* Strings the framework prints: in flash on Harvard ports (EMB_TEST_STR), so the
 * assertion texts do not consume the 2 KB of SRAM of the ATmega328P. */
#define EMB_ASSERT_TRUE(cond)                                          \
    do {                                                               \
        if (!(cond)) {                                                 \
            emb_test_fail(__LINE__, EMB_TEST_STR("expected: " #cond)); \
        }                                                              \
    } while (0)

#define EMB_ASSERT_EQ(a, b)                                                         \
    do {                                                                            \
        long va_ = (long)(a);                                                       \
        long vb_ = (long)(b);                                                       \
        if (va_ != vb_) {                                                           \
            emb_test_logf(EMB_TEST_STR("  values %ld and %ld"), va_, vb_);          \
            emb_test_fail(__LINE__, EMB_TEST_STR("expected equal: " #a " == " #b)); \
        }                                                                           \
    } while (0)

#define EMB_ASSERT_STATUS(expr, expected)                                                       \
    do {                                                                                        \
        emb_status_t st_ = (expr);                                                              \
        if (st_ != (expected)) {                                                                \
            emb_test_logf(EMB_TEST_STR("  status %d, expected %d"), (int)st_, (int)(expected)); \
            emb_test_fail(__LINE__, EMB_TEST_STR("status: " #expr));                            \
        }                                                                                       \
    } while (0)

#define EMB_ASSERT_OK(expr) EMB_ASSERT_STATUS((expr), EMB_OK)

/* Runs `stmt` in a forked copy of the process and asserts that it ends in a kernel
 * fault (checked builds, TEST-010). The copy carries only the calling host thread, so
 * `stmt` must fault before any switch. Ports without fork skip the check. */
#if EMB_TEST_HAS_FORK
#define EMB_ASSERT_FAULTS(stmt)                                    \
    do {                                                           \
        if (emb_test_fork_begin()) {                               \
            stmt;                                                  \
            emb_test_fork_child_no_fault();                        \
        }                                                          \
        emb_test_fork_expect_fault(__LINE__, EMB_TEST_STR(#stmt)); \
    } while (0)
#else
#define EMB_ASSERT_FAULTS(stmt)                                                  \
    do {                                                                         \
        if (0) {                                                                 \
            stmt; /* compiled, never run: keeps the test's symbols referenced */ \
        }                                                                        \
        emb_test_skip(EMB_TEST_STR("fault check needs fork"));                   \
    } while (0)
#endif

bool emb_test_fork_begin(void);
EMB_NORETURN void emb_test_fork_child_no_fault(void);
void emb_test_fork_expect_fault(unsigned line, const char *what_flash);

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
void emb_test_marks_check(unsigned line, const int *expected, size_t n);
#define EMB_ASSERT_MARKS(...)                                  \
    emb_test_marks_check(__LINE__, (const int[]){__VA_ARGS__}, \
                         sizeof((const int[]){__VA_ARGS__}) / sizeof(int))

/* Ticks since the kernel started. */
#define EMB_TEST_NOW() ((uint32_t)emb_time_now().ticks)

/* Elapsed ticks since @t0: exactly @n in virtual time, @n or @n + 1 in real time
 * (a tick may pass during the test's own work on a real-time port). */
void emb_test_check_elapsed(unsigned line, uint32_t elapsed, uint32_t expected);
#define EMB_ASSERT_ELAPSED(t0, n) \
    emb_test_check_elapsed(__LINE__, (uint32_t)(EMB_TEST_NOW() - (uint32_t)(t0)), (uint32_t)(n))

/* Software interrupts for the tests: a handler bound to a slot, raised on demand.
 * Delivery follows the port's rules: at once when unmasked, at the unlock when masked,
 * held while the slot is disabled. */
#define EMB_TEST_IRQ_SLOTS 4
void emb_test_irq_connect(unsigned slot, emb_isr_fn_t fn, void *arg);
void emb_test_irq_raise(unsigned slot);
/* After a raise made while interrupts were masked: returns once the handler has run.
 * The hardware takes the interrupt one instruction after the unmask; an emulator may
 * take it only at its next scheduling point (SIM-002), so a test that asserts the
 * order of events around an unlock calls this before its next observable step. */
void emb_test_irq_settle(void);
void emb_test_irq_enable(unsigned slot);
void emb_test_irq_disable(unsigned slot);
bool emb_test_irq_is_enabled(unsigned slot);

/* Helper threads: a fixed pool of stacks and storages the tests draw from. */
emb_thread_t emb_test_thread(const char *name, uint8_t priority, uint8_t flags,
                             emb_thread_entry_t entry, void *arg);
void emb_test_threads_reset(void); /* destroy every helper thread that is reusable */
emb_status_t emb_test_thread_destroy(emb_thread_t h); /* destroy a helper and free its pool slot */
/* Storage and stack for a thread a test builds by hand: the last pool slot's, which the
 * test must then leave unused (saves a stack on the 2 KB targets). */
emb_thread_storage_t *emb_test_scratch_storage(void);
uint8_t *emb_test_scratch_stack(size_t *out_size);

#define EMB_TEST_RUNNER_PRIORITY 1u

#ifdef __cplusplus
}
#endif

#endif /* EMB_TEST_H */
