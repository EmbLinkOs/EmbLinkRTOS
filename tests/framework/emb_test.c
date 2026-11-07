/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Test runner: main() starts the kernel, a runner thread executes every EMB_TEST. */
#include <emb/arch.h>

#include <stdarg.h>
#include <stddef.h>

#include <emb_board.h>
#include <emb_test.h>
#if CONFIG_EMB_ARCH_NATIVE
#include <emb_native.h>
#endif

EMB_TABLE_BOUNDS(const emb_test_case_t, emb_test_table);

static unsigned failures;
static unsigned skips;
static unsigned ran;
static unsigned current_failed;
static unsigned current_asserts;
static unsigned current_skips;

/* The case's requirement citations, printed after it ends: output inside a case would
 * take ticks on a real-time port. The pointers are string literals. */
#define REQS_MAX 8u
static const char *current_reqs[REQS_MAX];
static unsigned current_reqs_n;

/* Output held back while a case replays to the check its last boot faulted at (A89). */
static bool muted;

bool emb_test_muted(void)
{
    return muted;
}

void emb_test_fail(unsigned line, const char *what_flash)
{
    bool was_muted = muted;
    muted = false; /* a failure is always printed, even inside a replay */
    /* no formatter: a failure can be deep in a case, and the AVR runner's stack is small */
    emb_test_puts_flash(EMB_TEST_STR("  FAIL line "));
    emb_board_put_u32((uint32_t)line);
    emb_test_puts_flash(EMB_TEST_STR(": "));
    emb_test_puts_flash(what_flash);
    emb_test_puts_flash(EMB_TEST_STR("\n"));
    muted = was_muted;
    current_failed = 1u;
}

void emb_test_skip(const char *why_flash)
{
    emb_test_puts_flash(EMB_TEST_STR("  skip: "));
    emb_test_puts_flash(why_flash);
    emb_test_puts_flash(EMB_TEST_STR("\n"));
    skips++;
    current_skips++;
}

void emb_test_asserted(void)
{
    current_asserts++;
}

void emb_test_note_reqs(const char *first, ...)
{
    va_list ap;
    const char *id = first;
    va_start(ap, first);
    /* bounded by the citation list, which ends with a null pointer */
    while (id != NULL) {
        if (current_reqs_n < REQS_MAX) {
            current_reqs[current_reqs_n] = id;
        }
        current_reqs_n++; /* counts past the buffer, so an overflow is reported */
        id = va_arg(ap, const char *);
    }
    va_end(ap);
}

static void print_reqs(void)
{
    unsigned i;
    if (current_reqs_n == 0u) {
        return;
    }
    emb_test_puts_flash(EMB_TEST_STR("  req"));
    for (i = 0u; i < current_reqs_n && i < REQS_MAX; i++) {
        emb_test_puts_flash(EMB_TEST_STR(" "));
        emb_test_puts(current_reqs[i]);
    }
    if (current_reqs_n > REQS_MAX) {
        emb_test_logf(EMB_TEST_STR(" +%u"), current_reqs_n - REQS_MAX); /* not recorded */
    }
    emb_test_puts_flash(EMB_TEST_STR("\n"));
}

void emb_test_check_elapsed(unsigned line, uint32_t elapsed, uint32_t expected)
{
    /* exact in virtual time; a tick may pass during the test's own work in real time */
    uint32_t slack = EMB_TEST_EXACT_TIME ? 0u : 1u;
    emb_test_asserted();
    if (elapsed < expected || elapsed > expected + slack) {
        emb_test_logf(EMB_TEST_STR("  elapsed %lu, expected %lu"), (unsigned long)elapsed,
                      (unsigned long)expected);
        emb_test_fail(line, EMB_TEST_STR("elapsed ticks"));
    }
}

void emb_test_spin_ticks(uint32_t n)
{
    uint32_t t0 = EMB_TEST_NOW();
    /* bounded by @n ticks of the kernel clock */
    while ((uint32_t)(EMB_TEST_NOW() - t0) < n) {
#if CONFIG_EMB_ARCH_NATIVE
        emb_native_tick();
#endif
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
    emb_test_asserted();
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

void emb_test_irq_settle(void)
{
    emb_test_platform_irq_settle();
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

/* ---- fault checks by reset and replay (EMB_TEST_FAULT_BY_RESET, A89) ------------------- */

#if EMB_TEST_FAULT_BY_RESET
#define RESUME_MAGIC          0x4D52u /* "RM" */
#define RESUME_PENDING        0xA0u
#define RESULT_KERNEL_FAULT   1u
#define RESULT_HARDWARE_FAULT 2u
#define NO_CASE               0xFFFFu

/* What a boot leaves for the next one in the platform's retained memory: 16 bytes, which
 * the 2 KB of the ATmega328P can spare. A plain struct: only the same image reads it back,
 * and `image` and `sum` refuse anything else. */
typedef struct resume {
    uint16_t magic;
    uint16_t image;     /* this image: its configuration, case count and layout */
    uint8_t case_index; /* the case whose check faulted */
    uint8_t check;      /* that check's number in the case, from 1 */
    uint8_t ran;        /* totals before the case */
    uint8_t failures;
    uint8_t skips;
    uint8_t state;       /* RESUME_PENDING in the high nibble when the framework asked for
          this reset; RESULT_* in the low one */
    uint8_t settled;     /* the case's checks settled before this one */
    uint8_t case_failed; /* the case had failed before the check */
    uint32_t sum;        /* FNV-1a over the bytes before it */
} resume_t;

static resume_t resume; /* the record being built or the one read at boot */
static uint16_t resume_case = NO_CASE;
static uint16_t case_index;  /* the running case's position in the table */
static uint16_t case_check;  /* fault checks begun in the running case */
static uint8_t armed;        /* a check's statement is running */
static uint8_t case_settled; /* the running case's checks that reset the board as expected */
#if CONFIG_EMB_CHECKED
static unsigned armed_line; /* and where it is: read only where a check that returns fails */
static const char *armed_what;
#endif

static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h)
{
    size_t i;
    for (i = 0u; i < n; i++) { /* bounded by @n */
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint16_t image_id(void)
{
    static const char config[] = EMB_CONFIG_HASH_STRING;
    uintptr_t layout[3];
    layout[0] = (uintptr_t)EMB_TABLE_START(emb_test_table);
    layout[1] = (uintptr_t)EMB_TABLE_STOP(emb_test_table);
    layout[2] = (uintptr_t)emb_test_suite;
    uint32_t h = fnv((const uint8_t *)layout, sizeof(layout),
                     fnv((const uint8_t *)config, sizeof(config), 2166136261u));
    return (uint16_t)(h ^ (h >> 16));
}

static uint32_t resume_sum(const resume_t *r)
{
    return fnv((const uint8_t *)r, offsetof(resume_t, sum), 2166136261u);
}

static void resume_store(void)
{
    size_t cap = 0u;
    uint8_t *mem = emb_test_platform_retained(&cap);
    const uint8_t *src = (const uint8_t *)&resume;
    size_t i;
    resume.sum = resume_sum(&resume);
    for (i = 0u; i < sizeof(resume) && i < cap; i++) { /* bounded by the record */
        mem[i] = src[i];
    }
}

/* Read the record the last boot left; true when it is this image's and was left for this
 * reset. Either way it is cleared, so a reset the framework did not ask for starts over. */
static bool resume_load(void)
{
    size_t cap = 0u;
    uint8_t *mem = emb_test_platform_retained(&cap);
    uint8_t *dst = (uint8_t *)&resume;
    size_t i;
    bool ok;
    if (cap < sizeof(resume)) {
        return false;
    }
    for (i = 0u; i < sizeof(resume); i++) { /* bounded by the record */
        dst[i] = mem[i];
    }
    ok = resume.magic == RESUME_MAGIC && resume.sum == resume_sum(&resume) &&
         (resume.state & 0xF0u) == RESUME_PENDING && resume.image == image_id();
    for (i = 0u; i < sizeof(resume); i++) {
        mem[i] = 0u;
    }
    return ok;
}

/* The kernel calls this after it wrote the crash record and before its fault action: an
 * armed check resets the board, which the next boot reads as the check's outcome. */
static void fault_observed(const emb_fault_info_t *info)
{
    if (armed == 0u) {
        return; /* not a check's fault: the configured action follows */
    }
    resume.state = (uint8_t)(RESUME_PENDING |
                             ((info->fault_class == EMB_FAULT_HARDWARE) ? RESULT_HARDWARE_FAULT
                                                                        : RESULT_KERNEL_FAULT));
    resume_store();
    emb_arch_system_reset();
}

#if CONFIG_EMB_FAULT_HOOK_RUNTIME
static void hook_install(bool on)
{
    emb_fault_set_hook(on ? fault_observed : NULL);
}
#else
/* Bound at link time (CONFIG_EMB_FAULT_HOOK_RUNTIME=n): replaces the kernel's weak default. */
void emb_fault_hook(const emb_fault_info_t *info)
{
    fault_observed(info);
}

static void hook_install(bool on)
{
    (void)on; /* always installed; `armed` decides */
}
#endif

bool emb_test_fault_begin(unsigned line, const char *what_flash)
{
    case_check++;
    if (case_index == resume_case) {
        if (case_check < resume.check) {
            return false; /* settled on an earlier boot */
        }
        if (case_check == resume.check) {
            /* the check the last boot faulted at: its outcome */
            muted = false;
            resume_case = NO_CASE;
            case_settled = resume.settled;
            if (resume.case_failed != 0u) {
                current_failed = 1u; /* printed on the boot that failed */
            }
#if CONFIG_EMB_CHECKED
            if ((resume.state & 0x0Fu) != RESULT_KERNEL_FAULT) {
                emb_test_fail(line, EMB_TEST_STR("a hardware fault, not a kernel fault"));
            } else {
                case_settled++;
            }
#else
            emb_test_fail(line, EMB_TEST_STR("release build: misuse must return a status, not "
                                             "fault"));
#endif
            (void)what_flash;
            return false;
        }
    }
    if (case_index >= 0xFFu || case_check >= 0xFFu) {
        emb_test_skip(EMB_TEST_STR("fault check past the record's range"));
        return false;
    }
    resume.magic = RESUME_MAGIC;
    resume.image = image_id();
    resume.case_index = (uint8_t)case_index;
    resume.check = (uint8_t)case_check;
    resume.ran = (uint8_t)ran;
    resume.failures = (uint8_t)failures;
    resume.skips = (uint8_t)(skips - current_skips); /* the case's own are counted again */
    resume.state = 0u;
    resume.settled = case_settled;
    resume.case_failed = (uint8_t)current_failed;
    hook_install(true);
#if CONFIG_EMB_CHECKED
    armed_line = line;
    armed_what = what_flash;
#else
    (void)line; /* a release build returns the misuse's status: nothing to report */
    (void)what_flash;
#endif
    armed = 1u;
    return true;
}

void emb_test_fault_none(void)
{
    armed = 0u;
    hook_install(false);
#if CONFIG_EMB_CHECKED
    /* the statement returned: no fault */
    emb_test_fail(armed_line,
                  (armed_what != NULL) ? armed_what : EMB_TEST_STR("no fault from the check"));
#endif
    /* a release build returns the misuse's status */
}
#endif /* EMB_TEST_FAULT_BY_RESET */

/* ---- runner ---------------------------------------------------------------------- */

static void runner(void *arg)
{
    const emb_test_case_t *tc = EMB_TABLE_START(emb_test_table);
    const emb_test_case_t *stop = EMB_TABLE_STOP(emb_test_table);
    unsigned index = 0u;
    bool resumed = false;
    (void)arg;
    emb_test_platform_irq_bind(dispatch);
#if EMB_TEST_FAULT_BY_RESET
    if (resume_load()) {
        /* a fault check reset the board: continue from its case with the earlier totals */
        resumed = true;
        resume_case = resume.case_index;
        ran = resume.ran;
        failures = resume.failures;
        skips = resume.skips;
    }
#endif
    if (!resumed) {
        /* one identity line per run (docs/engineering/case-evidence.md) */
        emb_test_puts_flash(EMB_TEST_STR("EVIDENCE v1 suite="));
        emb_test_puts_flash(emb_test_suite);
        emb_test_logf(EMB_TEST_STR(" config=" EMB_CONFIG_HASH_STRING " profile=" CONFIG_EMB_PROFILE
                                   " board=" EMB_HW_BOARD_NAME " checked=%u\n"),
                      (unsigned)CONFIG_EMB_CHECKED);
    }
    for (; tc != NULL && tc < stop; tc++, index++) {
        emb_test_fn_t fn = (emb_test_fn_t)EMB_FLASH_READ_FNPTR(&tc->fn);
        const char *name = (const char *)EMB_FLASH_READ_PTR(&tc->name);
        bool replay = false;
        (void)index; /* the case's position: used by the fault checks' record */
#if EMB_TEST_FAULT_BY_RESET
        if (resumed && index < (unsigned)resume.case_index) {
            continue; /* finished on an earlier boot */
        }
        replay = index == (unsigned)resume_case;
        case_index = (uint16_t)index;
        case_check = 0u;
        case_settled = 0u;
#endif
        current_failed = 0u;
        current_asserts = 0u;
        current_skips = 0u;
        current_reqs_n = 0u;
        emb_test_marks_clear();
        if (!replay) {
            emb_test_puts_flash(EMB_TEST_STR("TEST "));
            emb_test_puts_flash(name);
            emb_test_puts_flash(EMB_TEST_STR("\n"));
        }
        muted = replay; /* until the check the last boot faulted at */
        fn();
        emb_test_threads_reset();
#if EMB_TEST_FAULT_BY_RESET
        if (index == (unsigned)resume_case) {
            muted = false;
            resume_case = NO_CASE;
            emb_test_fail(0u, EMB_TEST_STR("the replay did not reach the fault check that reset "
                                           "the board"));
        }
#endif
        muted = false;
#if EMB_TEST_FAULT_BY_RESET
        if (case_settled != 0u) {
            /* not emb_test_logf: its formatter's frames are more than the AVR runner's stack
             * spares; printed only now, since output inside a case takes ticks */
            emb_test_puts_flash(EMB_TEST_STR("  fault checks that reset the board: "));
            emb_board_put_u32((uint32_t)case_settled);
            emb_test_puts_flash(EMB_TEST_STR("\n"));
            case_settled = 0u;
        }
#endif
        ran++;
        if (current_failed != 0u) {
            failures++;
            emb_test_puts_flash(EMB_TEST_STR("  -> FAILED\n"));
        } else {
            emb_test_puts_flash(EMB_TEST_STR("  -> ok\n"));
        }
        print_reqs();
        emb_test_puts_flash(EMB_TEST_STR("CASE "));
        emb_test_puts_flash(name);
        if (current_failed != 0u) {
            emb_test_puts_flash(EMB_TEST_STR(" outcome=failed"));
        } else if (current_asserts == 0u && current_skips != 0u) {
            emb_test_puts_flash(EMB_TEST_STR(" outcome=skipped"));
        } else {
            emb_test_puts_flash(EMB_TEST_STR(" outcome=passed"));
        }
        emb_test_logf(EMB_TEST_STR(" asserts=%u skips=%u\n"), current_asserts, current_skips);
    }
    {
        /* the interrupt stack's deepest use over the run, for sizing it (A90) */
        size_t isr_size = 0u;
        size_t isr_used = 0u;
        if (emb_isr_stack_info(&isr_size, &isr_used) == EMB_OK) {
            emb_test_logf(EMB_TEST_STR("ISR_STACK size=%lu used=%lu\n"), (unsigned long)isr_size,
                          (unsigned long)isr_used);
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
