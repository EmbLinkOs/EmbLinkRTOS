/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Test runner: main() starts the kernel, a runner thread executes every EMB_TEST. */
#include <emb_board.h>
#include <emb_test.h>

extern const emb_test_case_t __start_emb_test_table[]
    EMB_WEAK; /* NOLINT(bugprone-reserved-identifier) */
extern const emb_test_case_t __stop_emb_test_table[]
    EMB_WEAK; /* NOLINT(bugprone-reserved-identifier) */

static unsigned failures;
static unsigned skips;
static unsigned current_failed;

void emb_test_fail(unsigned line, const char *what_flash)
{
    emb_test_logf(EMB_TEST_STR("  FAIL line %u: "), line);
    emb_test_puts_flash(what_flash);
    emb_test_puts_flash(EMB_TEST_STR("\n"));
    current_failed = 1u;
}

void emb_test_skip(const char *why_flash)
{
    emb_test_puts_flash(EMB_TEST_STR("  skip: "));
    emb_test_puts_flash(why_flash);
    emb_test_puts_flash(EMB_TEST_STR("\n"));
    skips++;
}

void emb_test_note_reqs(const char *first, ...)
{
    (void)first; /* collected statically by the traceability tool */
}

void emb_test_check_elapsed(unsigned line, uint32_t elapsed, uint32_t expected)
{
    /* exact in virtual time; a tick may pass during the test's own work in real time */
    uint32_t slack = EMB_TEST_EXACT_TIME ? 0u : 1u;
    if (elapsed < expected || elapsed > expected + slack) {
        emb_test_logf(EMB_TEST_STR("  elapsed %lu, expected %lu"), (unsigned long)elapsed,
                      (unsigned long)expected);
        emb_test_fail(line, EMB_TEST_STR("elapsed ticks"));
    }
}

/* ---- marks ------------------------------------------------------------------------ */

static int marks[EMB_TEST_MARKS_MAX]; /* lock: pushed from any context; the runner reads when all
                                         are quiet */
static unsigned marks_n;

void emb_test_mark(int code)
{
    emb_irq_key_t key = emb_irq_lock();
    if (marks_n < (unsigned)EMB_TEST_MARKS_MAX) {
        marks[marks_n] = code;
        marks_n++;
    }
    emb_irq_unlock(key);
}

void emb_test_marks_clear(void)
{
    emb_irq_key_t key = emb_irq_lock();
    marks_n = 0u;
    emb_irq_unlock(key);
}

unsigned emb_test_marks_count(void)
{
    return marks_n;
}

int emb_test_mark_at(unsigned i)
{
    return (i < marks_n) ? marks[i] : -1;
}

void emb_test_marks_check(unsigned line, const int *expected, size_t n)
{
    size_t i;
    bool ok = marks_n == n;
    for (i = 0u; ok && i < n; i++) {
        ok = marks[i] == expected[i];
    }
    if (!ok) {
        emb_test_puts_flash(EMB_TEST_STR("  marks:"));
        for (i = 0u; i < marks_n; i++) {
            emb_test_logf(EMB_TEST_STR(" %d"), marks[i]);
        }
        emb_test_puts_flash(EMB_TEST_STR("\n  expected:"));
        for (i = 0u; i < n; i++) {
            emb_test_logf(EMB_TEST_STR(" %d"), expected[i]);
        }
        emb_test_puts_flash(EMB_TEST_STR("\n"));
        emb_test_fail(line, EMB_TEST_STR("mark sequence"));
    }
}

/* ---- software interrupt slots ------------------------------------------------------- */

typedef struct irq_slot {
    emb_isr_fn_t fn;
    void *arg;
    uint8_t enabled;
    uint8_t pending;
} irq_slot_t;

static irq_slot_t slots[EMB_TEST_IRQ_SLOTS]; /* lock: critical section */
static unsigned raised_slot;

static void dispatch(void)
{
    irq_slot_t *s = &slots[raised_slot];
    if (s->fn != NULL) {
        s->fn(s->arg);
    }
}

void emb_test_irq_connect(unsigned slot, emb_isr_fn_t fn, void *arg)
{
    if (slot < (unsigned)EMB_TEST_IRQ_SLOTS) {
        emb_irq_key_t key = emb_irq_lock();
        slots[slot].fn = fn;
        slots[slot].arg = arg;
        slots[slot].enabled = 1u;
        slots[slot].pending = 0u;
        emb_irq_unlock(key);
    }
}

void emb_test_irq_raise(unsigned slot)
{
    if (slot >= (unsigned)EMB_TEST_IRQ_SLOTS) {
        return;
    }
    if (slots[slot].enabled == 0u) {
        slots[slot].pending = 1u;
        return;
    }
    raised_slot = slot;
    emb_test_platform_irq_trigger();
}

void emb_test_irq_enable(unsigned slot)
{
    if (slot < (unsigned)EMB_TEST_IRQ_SLOTS) {
        slots[slot].enabled = 1u;
        if (slots[slot].pending != 0u) {
            slots[slot].pending = 0u;
            emb_test_irq_raise(slot);
        }
    }
}

void emb_test_irq_disable(unsigned slot)
{
    if (slot < (unsigned)EMB_TEST_IRQ_SLOTS) {
        slots[slot].enabled = 0u;
    }
}

bool emb_test_irq_is_enabled(unsigned slot)
{
    return slot < (unsigned)EMB_TEST_IRQ_SLOTS && slots[slot].enabled != 0u;
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
        emb_test_fail(__LINE__, EMB_TEST_STR("helper thread pool exhausted"));
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

emb_status_t emb_test_thread_destroy(emb_thread_t h)
{
    unsigned i;
    emb_status_t st = emb_thread_destroy(h);
    if (st != EMB_OK) {
        return st;
    }
    for (i = 0u; i < (unsigned)EMB_TEST_MAX_THREADS; i++) {
        if (pool_used[i] != 0u && EMB_HANDLE_EQ(pool_handle[i], h)) {
            pool_used[i] = 0u;
        }
    }
    return EMB_OK;
}

emb_thread_storage_t *emb_test_scratch_storage(void)
{
    return &pool_storage[EMB_TEST_MAX_THREADS - 1];
}

uint8_t *emb_test_scratch_stack(size_t *out_size)
{
    *out_size = sizeof(pool_stack[EMB_TEST_MAX_THREADS - 1]);
    return pool_stack[EMB_TEST_MAX_THREADS - 1];
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
    emb_test_platform_irq_bind(dispatch);
    for (; tc != NULL && tc < stop; tc++) {
        emb_test_fn_t fn = (emb_test_fn_t)EMB_FLASH_READ_FNPTR(&tc->fn);
        const char *name = (const char *)EMB_FLASH_READ_PTR(&tc->name);
        current_failed = 0u;
        emb_test_marks_clear();
        emb_test_puts_flash(EMB_TEST_STR("TEST "));
        emb_test_puts_flash(name);
        emb_test_puts_flash(EMB_TEST_STR("\n"));
        fn();
        emb_test_threads_reset();
        ran++;
        if (current_failed != 0u) {
            failures++;
            emb_test_puts_flash(EMB_TEST_STR("  -> FAILED\n"));
        } else {
            emb_test_puts_flash(EMB_TEST_STR("  -> ok\n"));
        }
    }
    emb_test_logf(EMB_TEST_STR("%u tests, %u failed, %u skipped, %lu ticks\n"), ran, failures,
                  skips, (unsigned long)EMB_TEST_NOW());
    emb_test_exit((failures != 0u) ? 1 : 0);
}

static emb_thread_storage_t runner_storage;
static EMB_ALIGNED(EMB_STACK_ALIGN) uint8_t runner_stack[EMB_TEST_RUNNER_STACK];

int main(void)
{
    emb_thread_attr_t attr;
    emb_thread_t h;
    emb_board_init();
    emb_test_platform_init();
    emb_kernel_init();
    emb_thread_attr_default(&attr);
    attr.name = EMB_TEST_NAME("runner");
    attr.priority = EMB_TEST_RUNNER_PRIORITY;
    attr.stack = runner_stack;
    attr.stack_size = sizeof(runner_stack);
    EMB_CHECK(emb_thread_init(&runner_storage, &attr, runner, NULL, &h));
    EMB_CHECK(emb_thread_start(h));
    emb_kernel_start();
}
