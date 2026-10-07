/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Thread lifecycle conformance (SPEC-008; KRN-THR-001 to 022). */
#include <emb_test.h>

static emb_sem_storage_t sem_storage;
static emb_sem_t sem;

static void make_sem(void)
{
    EMB_ASSERT_OK(emb_sem_init(&sem_storage, NULL, &sem));
}

static void exit_with_code(void *arg)
{
    emb_thread_exit((int)(intptr_t)arg);
}

static void sleep_then_exit(void *arg)
{
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS((uint32_t)(uintptr_t)arg)));
    emb_thread_exit(42);
}

static void returning_entry(void *arg)
{
    (void)arg;
}

EMB_TEST(thread_init_validates_arguments)
{
    static emb_thread_storage_t st;
    static EMB_ALIGNED(EMB_STACK_ALIGN) uint8_t stack[EMB_TEST_STACK_SIZE];
    emb_thread_attr_t attr;
    emb_thread_t h;
    EMB_TEST_REQ("KRN-THR-001", "API-015");
    emb_thread_attr_default(&attr);
    attr.stack = stack;
    attr.stack_size = sizeof(stack);
    attr.priority = 3u;
    EMB_ASSERT_MISUSE(emb_thread_init(&st, &attr, NULL, NULL, &h), EMB_EINVAL);
    attr.priority = 0u;
    EMB_ASSERT_MISUSE(emb_thread_init(&st, &attr, returning_entry, NULL, &h), EMB_EINVAL);
    attr.priority = (uint8_t)(EMB_PRIORITY_MAX + 1u);
    EMB_ASSERT_MISUSE(emb_thread_init(&st, &attr, returning_entry, NULL, &h), EMB_EINVAL);
    attr.priority = 3u;
    attr.stack = NULL;
    EMB_ASSERT_MISUSE(emb_thread_init(&st, &attr, returning_entry, NULL, &h), EMB_EINVAL);
    attr.stack = stack;
    attr.stack_size = 4u;
    EMB_ASSERT_MISUSE(emb_thread_init(&st, &attr, returning_entry, NULL, &h), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_thread_init(NULL, &attr, returning_entry, NULL, &h), EMB_EINVAL);
}

EMB_TEST(thread_start_twice_is_estate_and_destroy_rules)
{
    static emb_thread_storage_t st;
    static EMB_ALIGNED(EMB_STACK_ALIGN) uint8_t stack[EMB_TEST_STACK_SIZE];
    emb_thread_attr_t attr;
    emb_thread_t h;
    uint8_t state = 0xFFu;
    EMB_TEST_REQ("KRN-THR-016", "KRN-THR-022", "KRN-OBJ-001");
    emb_thread_attr_default(&attr);
    attr.name = "inactive";
    attr.stack = stack;
    attr.stack_size = sizeof(stack);
    attr.priority = 3u;
    EMB_ASSERT_OK(emb_thread_init(&st, &attr, exit_with_code, (void *)5, &h));
    EMB_ASSERT_OK(emb_thread_state(h, &state, NULL));
    EMB_ASSERT_EQ(state, EMB_THREAD_INACTIVE);
    EMB_ASSERT_EQ(emb_thread_index(h) != 0xFFu, true);
    EMB_ASSERT_OK(emb_thread_destroy(h)); /* INACTIVE: destroy allowed */
    EMB_ASSERT_OK(emb_thread_init(&st, &attr, exit_with_code, (void *)5, &h));
    EMB_ASSERT_OK(emb_thread_start(h)); /* runs to its exit at once (priority 3) */
    EMB_ASSERT_STATUS(emb_thread_start(h), EMB_ESTATE);
    EMB_ASSERT_OK(emb_thread_state(h, &state, NULL));
    EMB_ASSERT_EQ(state, EMB_THREAD_TERMINATED);
    EMB_ASSERT_MISUSE(emb_thread_destroy(h), EMB_EBUSY); /* not joined yet */
    EMB_ASSERT_OK(emb_thread_join(h, EMB_NO_WAIT, NULL));
    EMB_ASSERT_OK(emb_thread_destroy(h));
}

EMB_TEST(thread_join_after_exit_returns_code)
{
    emb_thread_t h = emb_test_thread("h", 3u, 0u, exit_with_code, (void *)77);
    int code = 0;
    EMB_TEST_REQ("KRN-THR-004", "KRN-THR-006", "KRN-THR-017");
    EMB_ASSERT_OK(emb_thread_join(h, EMB_NO_WAIT, &code));
    EMB_ASSERT_EQ(code, 77);
}

EMB_TEST(thread_join_before_exit_blocks_and_hands_off)
{
    uint32_t t0 = EMB_TEST_NOW();
    emb_thread_t h = emb_test_thread("h", 3u, 0u, sleep_then_exit, (void *)5);
    int code = 0;
    EMB_TEST_REQ("KRN-THR-017", "KRN-WAIT-012");
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, &code));
    EMB_ASSERT_EQ(code, 42);
    EMB_ASSERT_EQ(EMB_TEST_NOW() - t0, 5u);
}

EMB_TEST(thread_join_timeout_then_success)
{
    emb_thread_t h = emb_test_thread("h", 3u, 0u, sleep_then_exit, (void *)20);
    EMB_TEST_REQ("KRN-THR-017", "KRN-WAIT-004");
    EMB_ASSERT_STATUS(emb_thread_join(h, EMB_TIMEOUT(EMB_TICKS(5)), NULL), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
}

static void joiner_entry(void *arg)
{
    emb_thread_t target = *(emb_thread_t *)arg;
    int code = 0;
    EMB_ASSERT_OK(emb_thread_join(target, EMB_WAIT_FOREVER, &code));
    EMB_ASSERT_EQ(code, 42);
    emb_test_mark(1);
}

EMB_TEST(thread_second_joiner_is_ebusy)
{
    static emb_thread_t target;
    EMB_TEST_REQ("KRN-THR-018");
    target = emb_test_thread("target", 2u, 0u, sleep_then_exit, (void *)10);
    (void)emb_test_thread("joiner", 3u, 0u, joiner_entry, &target);
    EMB_ASSERT_STATUS(emb_thread_join(target, EMB_WAIT_FOREVER, NULL), EMB_EBUSY);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(15)));
    EMB_ASSERT_MARKS(1);
}

EMB_TEST(thread_join_self_and_detached_are_errors)
{
    emb_thread_t d = emb_test_thread("d", 3u, EMB_THREAD_DETACHED, exit_with_code, (void *)1);
    EMB_TEST_REQ("KRN-THR-017");
    EMB_ASSERT_MISUSE(emb_thread_join(emb_thread_self(), EMB_NO_WAIT, NULL), EMB_EDEADLK);
    EMB_ASSERT_STATUS(emb_thread_join(d, EMB_NO_WAIT, NULL), EMB_EINVAL);
    EMB_ASSERT_OK(emb_test_thread_destroy(d)); /* detached and terminated: reusable at exit */
}

EMB_TEST(thread_detach_after_exit_makes_reusable)
{
    emb_thread_t h = emb_test_thread("h", 3u, 0u, exit_with_code, (void *)1);
    EMB_TEST_REQ("KRN-THR-017", "KRN-THR-022");
    EMB_ASSERT_MISUSE(emb_thread_destroy(h), EMB_EBUSY);
    EMB_ASSERT_OK(emb_thread_detach(h));
    EMB_ASSERT_OK(emb_test_thread_destroy(h));
}

static void periodic_counter(void *arg)
{
    uint32_t *counter = (uint32_t *)arg;
    for (;;) {
        emb_status_t st = emb_thread_sleep(EMB_TICKS(1));
        if (st == EMB_ECANCELED) {
            return;
        }
        (*counter)++;
    }
}

EMB_TEST(thread_suspend_resume_are_idempotent_overlays)
{
    static uint32_t counter;
    emb_thread_t h;
    uint32_t before;
    uint8_t state = 0u;
    uint8_t reason = 0u;
    EMB_TEST_REQ("KRN-THR-010", "KRN-THR-019");
    counter = 0u;
    h = emb_test_thread("tick", 3u, 0u, periodic_counter, &counter);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(5)));
    EMB_ASSERT_EQ(counter, 5u);
    EMB_ASSERT_OK(emb_thread_suspend(h));
    EMB_ASSERT_OK(emb_thread_suspend(h)); /* idempotent */
    before = counter;
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(5)));
    EMB_ASSERT_EQ(counter, before); /* its deadline passed while suspended: wake kept, not run */
    EMB_ASSERT_OK(emb_thread_state(h, &state, &reason));
    EMB_ASSERT_EQ(state, EMB_THREAD_BLOCKED);
    EMB_ASSERT_EQ(reason, EMB_WAIT_REASON_SUSPEND);
    EMB_ASSERT_OK(emb_thread_resume(h));
    EMB_ASSERT_OK(emb_thread_resume(h));
    EMB_ASSERT_EQ(counter, before + 1u); /* the recorded wake took effect at resume */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(3)));
    EMB_ASSERT_EQ(counter, before + 4u);
    EMB_ASSERT_OK(emb_thread_cancel(h));
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
}

static void take_and_mark(void *arg)
{
    (void)arg;
    emb_test_mark(emb_sem_take(sem, EMB_WAIT_FOREVER) == EMB_OK ? 1 : 2);
}

EMB_TEST(thread_wake_while_suspended_is_applied_at_resume)
{
    emb_thread_t h;
    uint8_t state = 0u;
    EMB_TEST_REQ("KRN-WAIT-016", "KRN-THR-010");
    make_sem();
    h = emb_test_thread("taker", 3u, 0u, take_and_mark, NULL);
    EMB_ASSERT_OK(emb_thread_suspend(h));
    EMB_ASSERT_OK(emb_sem_give(sem)); /* the hand-off happens; the thread stays suspended */
    EMB_ASSERT_OK(emb_thread_state(h, &state, NULL));
    EMB_ASSERT_EQ(state, EMB_THREAD_BLOCKED);
    EMB_ASSERT_EQ(emb_test_marks_count(), 0u);
    EMB_ASSERT_OK(emb_thread_resume(h));
    EMB_ASSERT_MARKS(1);
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
}

static void sleeper_reports(void *arg)
{
    (void)arg;
    emb_test_mark(emb_thread_sleep(EMB_TICKS(100)) == EMB_ECANCELED ? 1 : 2);
}

EMB_TEST(thread_cancel_wakes_a_sleeping_thread)
{
    uint32_t t0 = EMB_TEST_NOW();
    emb_thread_t h = emb_test_thread("s", 3u, 0u, sleeper_reports, NULL);
    EMB_TEST_REQ("KRN-THR-011", "KRN-WAIT-017");
    EMB_ASSERT_OK(emb_thread_cancel(h));
    EMB_ASSERT_MARKS(1);
    EMB_ASSERT_EQ(EMB_TEST_NOW(), t0);
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
}

static void cancel_disabled_worker(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_thread_cancel_disable());
    emb_test_mark(emb_sem_take(sem, EMB_WAIT_FOREVER) == EMB_OK ? 1 : 2);
    EMB_ASSERT_OK(emb_thread_cancel_enable());
    emb_test_mark(emb_thread_sleep(EMB_TICKS(1)) == EMB_ECANCELED ? 3 : 4);
    emb_test_mark(emb_thread_sleep(EMB_TICKS(1)) == EMB_OK ? 5 : 6); /* delivered once */
}

EMB_TEST(thread_cancel_pending_delivered_at_next_point)
{
    emb_thread_t h;
    EMB_TEST_REQ("KRN-THR-011", "KRN-THR-020");
    make_sem();
    h = emb_test_thread("w", 3u, 0u, cancel_disabled_worker, NULL);
    EMB_ASSERT_OK(emb_thread_cancel(h)); /* not delivered: disabled */
    EMB_ASSERT_EQ(emb_test_marks_count(), 0u);
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_MARKS(1, 3, 5);
}

EMB_TEST(thread_cancel_point_and_counts)
{
    EMB_TEST_REQ("KRN-THR-011", "KRN-THR-020");
    EMB_ASSERT_OK(emb_thread_cancel_point());
    EMB_ASSERT_OK(emb_thread_cancel(emb_thread_self()));
    EMB_ASSERT_STATUS(emb_thread_cancel_point(), EMB_ECANCELED);
    EMB_ASSERT_OK(emb_thread_cancel_point());
    EMB_ASSERT_MISUSE(emb_thread_cancel_enable(), EMB_ESTATE);
}

static void take_then_mark_id(void *arg)
{
    int id = (int)(intptr_t)arg;
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_WAIT_FOREVER));
    emb_test_mark(id);
}

EMB_TEST(thread_priority_change_requeues_waiter)
{
    emb_thread_t a;
    EMB_TEST_REQ("KRN-WAIT-003", "KRN-SCH-009", "KRN-SYNC-018");
    make_sem();
    a = emb_test_thread("a", 2u, 0u, take_then_mark_id, (void *)2);
    (void)emb_test_thread("b", 3u, 0u, take_then_mark_id, (void *)3);
    EMB_ASSERT_OK(emb_thread_set_priority(a, 5u));
    EMB_ASSERT_EQ(emb_thread_get_priority(a), 5u);
    EMB_ASSERT_OK(emb_sem_give(sem)); /* a is now first */
    EMB_ASSERT_OK(emb_sem_give(sem));
    EMB_ASSERT_MARKS(2, 3);
}

EMB_TEST(thread_state_reports_lifecycle)
{
    emb_thread_t h;
    uint8_t state = 0u;
    uint8_t reason = 0u;
    EMB_TEST_REQ("KRN-THR-021");
    EMB_ASSERT_OK(emb_thread_state(emb_thread_self(), &state, &reason));
    EMB_ASSERT_EQ(state, EMB_THREAD_RUNNING);
    h = emb_test_thread("s", 3u, 0u, sleeper_reports, NULL);
    EMB_ASSERT_OK(emb_thread_state(h, &state, &reason));
    EMB_ASSERT_EQ(state, EMB_THREAD_BLOCKED);
    EMB_ASSERT_EQ(reason, EMB_WAIT_REASON_SLEEP);
    EMB_ASSERT_OK(emb_thread_cancel(h));
    EMB_ASSERT_OK(emb_thread_state(h, &state, &reason));
    EMB_ASSERT_EQ(state, EMB_THREAD_TERMINATED);
    EMB_ASSERT_OK(emb_thread_join(h, EMB_NO_WAIT, NULL));
}

static void define_entry(void *arg)
{
    (void)arg;
    emb_test_mark(7);
}

EMB_THREAD_DEFINE(defined_thread, define_entry, NULL, 4u, EMB_TEST_STACK_SIZE,
                  EMB_THREAD_NO_AUTOSTART);

EMB_TEST(thread_define_registers_in_init_table)
{
    uint8_t state = 0xFFu;
    EMB_TEST_REQ("KRN-THR-015", "API-017");
    EMB_ASSERT_OK(emb_thread_state(defined_thread, &state, NULL));
    EMB_ASSERT_EQ(state, EMB_THREAD_INACTIVE);
    EMB_ASSERT_OK(emb_thread_start(defined_thread));
    EMB_ASSERT_MARKS(7);
    EMB_ASSERT_OK(emb_thread_join(defined_thread, EMB_NO_WAIT, NULL));
}

EMB_TEST(thread_stack_info_reports_size)
{
    size_t size = 0u;
    size_t hw = 1u;
    EMB_TEST_REQ("KRN-MEM-009");
    EMB_ASSERT_OK(emb_thread_stack_info(emb_thread_self(), &size, &hw));
    EMB_ASSERT_EQ(size, EMB_TEST_STACK_SIZE);
}
