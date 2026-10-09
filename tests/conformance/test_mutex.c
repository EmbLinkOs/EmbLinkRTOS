/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Mutex conformance (SPEC-005 §2, §3; KRN-SYNC-001 to 019). */
#include <emb_test.h>

static emb_mutex_storage_t st1;
static emb_mutex_storage_t st2;
static emb_mutex_t m1;
static emb_mutex_t m2;

static void make(emb_mutex_storage_t *st, emb_mutex_t *out, uint8_t protocol, uint8_t ceiling,
                 uint8_t flags)
{
    emb_mutex_attr_t attr;
    emb_mutex_attr_default(&attr);
    attr.protocol = protocol;
    attr.ceiling = ceiling;
    attr.flags = flags;
    EMB_ASSERT_OK(emb_mutex_init(st, &attr, out));
}

EMB_TEST(mutex_lock_unlock_and_ownership)
{
    EMB_TEST_REQ("KRN-SYNC-001", "KRN-SYNC-008");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
    EMB_ASSERT_TRUE(!emb_mutex_is_owner(m1));
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_NO_WAIT));
    EMB_ASSERT_TRUE(emb_mutex_is_owner(m1));
    EMB_ASSERT_MISUSE(emb_mutex_lock(m1, EMB_NO_WAIT), EMB_EDEADLK); /* non-recursive relock */
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    EMB_ASSERT_MISUSE(emb_mutex_unlock(m1), EMB_EPERM); /* not the owner */
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
}

EMB_TEST(mutex_recursive_counts)
{
    EMB_TEST_REQ("KRN-SYNC-004");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, EMB_MUTEX_RECURSIVE);
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    EMB_ASSERT_TRUE(emb_mutex_is_owner(m1));
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    EMB_ASSERT_TRUE(!emb_mutex_is_owner(m1));
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
}

static void hold_m1_for(void *arg)
{
    uint32_t ticks = (uint32_t)(uintptr_t)arg;
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_WAIT_FOREVER));
    emb_test_mark(1);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(ticks)));
    emb_test_mark(2);
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    emb_test_mark(3);
}

static void lock_m1_report(void *arg)
{
    emb_status_t st = emb_mutex_lock(m1, EMB_TIMEOUT(EMB_TICKS((uint32_t)(uintptr_t)arg)));
    if (st == EMB_OK) {
        emb_test_mark(10);
        EMB_ASSERT_OK(emb_mutex_unlock(m1));
    } else {
        emb_test_mark(st == EMB_ETIMEDOUT ? 11 : 12);
    }
}

EMB_TEST(mutex_contended_lock_inherits_and_hands_off)
{
    emb_thread_t low;
    EMB_TEST_REQ("KRN-SYNC-003", "KRN-SYNC-009", "KRN-SYNC-010", "KRN-WAIT-012");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
    low = emb_test_thread(EMB_TEST_NAME("low"), 2u, 0u, hold_m1_for, (void *)10);
    (void)emb_test_thread(EMB_TEST_NAME("high"), 4u, 0u, lock_m1_report, (void *)100);
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(low), 4u); /* raised by the waiter */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(15)));
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(low), 2u);
    /* low unlocks at tick 10: high gets it by hand-off and runs before low continues */
    EMB_ASSERT_MARKS(1, 2, 10, 3);
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
}

EMB_TEST(mutex_timeout_removes_the_contribution)
{
    emb_thread_t low;
    EMB_TEST_REQ("KRN-SYNC-014", "KRN-WAIT-013");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
    low = emb_test_thread(EMB_TEST_NAME("low"), 2u, 0u, hold_m1_for, (void *)20);
    (void)emb_test_thread(EMB_TEST_NAME("high"), 4u, 0u, lock_m1_report, (void *)5);
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(low), 4u);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(7)));
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(low), 2u); /* the waiter timed out at 5 */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(20)));
    EMB_ASSERT_MARKS(1, 11, 2, 3);
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
}

/* chain: a owns m1; b owns m2 and waits for m1; c waits for m2 (SPEC-005 §2.2) */
static void chain_a(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_WAIT_FOREVER));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    emb_test_mark(1);
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
}

static void chain_b(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(1)));
    EMB_ASSERT_OK(emb_mutex_lock(m2, EMB_WAIT_FOREVER));
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_WAIT_FOREVER));
    emb_test_mark(2);
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    EMB_ASSERT_OK(emb_mutex_unlock(m2));
}

static void chain_c(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(2)));
    EMB_ASSERT_OK(emb_mutex_lock(m2, EMB_WAIT_FOREVER));
    emb_test_mark(3);
    EMB_ASSERT_OK(emb_mutex_unlock(m2));
}

EMB_TEST(mutex_inheritance_propagates_along_the_chain)
{
    emb_thread_t a;
    emb_thread_t b;
    EMB_TEST_REQ("KRN-SYNC-009", "KRN-SYNC-010", "KRN-SYNC-016");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
    make(&st2, &m2, EMB_MUTEX_INHERIT, 0u, 0u);
    a = emb_test_thread(EMB_TEST_NAME("a"), 2u, 0u, chain_a, NULL);
    b = emb_test_thread(EMB_TEST_NAME("b"), 3u, 0u, chain_b, NULL);
    (void)emb_test_thread(EMB_TEST_NAME("c"), 4u, 0u, chain_c, NULL);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(3)));
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(b), 4u);
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(a), 4u); /* two hops */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(a), 2u);
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(b), 3u);
    EMB_ASSERT_MARKS(1, 2, 3);
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
    EMB_ASSERT_OK(emb_mutex_destroy(m2));
}

static emb_status_t deadlock_status;

static void dl_a(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_WAIT_FOREVER));
    EMB_ASSERT_OK(emb_thread_suspend(emb_thread_self()));
    deadlock_status = emb_mutex_lock(m2, EMB_WAIT_FOREVER); /* owner b waits for m1: a cycle */
    if (deadlock_status == EMB_OK) {
        EMB_ASSERT_OK(emb_mutex_unlock(m2));
    }
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
}

static void dl_b(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_mutex_lock(m2, EMB_WAIT_FOREVER));
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_WAIT_FOREVER)); /* blocks on a */
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    EMB_ASSERT_OK(emb_mutex_unlock(m2));
}

static void deadlock_scenario(void)
{
    emb_thread_t a = emb_test_thread(EMB_TEST_NAME("a"), 3u, 0u, dl_a, NULL);
    (void)emb_test_thread(EMB_TEST_NAME("b"), 4u, 0u, dl_b, NULL);
    EMB_ASSERT_OK(emb_thread_resume(a));
}

EMB_TEST(mutex_deadlock_is_detected_on_the_chain)
{
    EMB_TEST_REQ("KRN-SYNC-015");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
    make(&st2, &m2, EMB_MUTEX_INHERIT, 0u, 0u);
    deadlock_status = EMB_OK;
#if CONFIG_EMB_CHECKED
    EMB_ASSERT_FAULTS(deadlock_scenario());
#else
    deadlock_scenario();
    EMB_ASSERT_EQ(deadlock_status, EMB_EDEADLK);
#endif
}

static void ceiling_locker(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_NO_WAIT));
    emb_test_mark((int)emb_thread_get_effective_priority(emb_thread_self()));
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    emb_test_mark((int)emb_thread_get_effective_priority(emb_thread_self()));
}

EMB_TEST(mutex_ceiling_raises_the_owner)
{
    EMB_TEST_REQ("KRN-SYNC-005", "KRN-SYNC-006");
    make(&st1, &m1, EMB_MUTEX_CEILING, 5u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("l"), 3u, 0u, ceiling_locker, NULL);
    EMB_ASSERT_MARKS(5, 3);
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
}

static void above_ceiling(void *arg)
{
    (void)arg;
    emb_test_mark(emb_mutex_lock(m1, EMB_NO_WAIT) == EMB_EPERM ? 1 : 2);
}

EMB_TEST(mutex_ceiling_violation_is_misuse)
{
    EMB_TEST_REQ("KRN-SYNC-006");
    make(&st1, &m1, EMB_MUTEX_CEILING, 3u, 0u);
#if CONFIG_EMB_CHECKED
    EMB_ASSERT_FAULTS((void)emb_test_thread(EMB_TEST_NAME("hi"), 6u, 0u, above_ceiling, NULL));
#else
    (void)emb_test_thread(EMB_TEST_NAME("hi"), 6u, 0u, above_ceiling, NULL);
    EMB_ASSERT_MARKS(1);
#endif
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
}

static void dies_owning(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_NO_WAIT));
}

EMB_TEST(mutex_owner_death_policy)
{
    EMB_TEST_REQ("KRN-SYNC-011");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
#if CONFIG_EMB_CHECKED && CONFIG_EMB_MUTEX_OWNER_DEATH_FAULT
    EMB_ASSERT_FAULTS((void)emb_test_thread(EMB_TEST_NAME("d"), 3u, 0u, dies_owning, NULL));
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
#else
    (void)emb_test_thread(EMB_TEST_NAME("d"), 3u, 0u, dies_owning, NULL);
    EMB_ASSERT_STATUS(emb_mutex_lock(m1, EMB_NO_WAIT),
                      EMB_EOWNERDEAD); /* released, marked inconsistent */
    EMB_ASSERT_TRUE(emb_mutex_is_owner(m1));
    EMB_ASSERT_OK(emb_mutex_unlock(m1)); /* clears the mark */
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
#endif
}

EMB_TEST(mutex_destroy_rules)
{
    EMB_TEST_REQ("KRN-OBJ-001");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("low"), 2u, 0u, hold_m1_for, (void *)5);
    EMB_ASSERT_MISUSE(emb_mutex_destroy(m1), EMB_EBUSY); /* owned by another thread */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, EMB_OBJ_ABORT_WAITERS);
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_NO_WAIT));
    (void)emb_test_thread(EMB_TEST_NAME("w"), 3u, 0u, lock_m1_report, (void *)100);
    EMB_ASSERT_OK(emb_mutex_destroy(m1)); /* the waiter leaves with EMB_EDESTROYED */
    EMB_ASSERT_MARKS(1, 2, 3, 12);
}

static void lock_mark_unlock(void *arg)
{
    int id = (int)(intptr_t)arg;
    EMB_ASSERT_OK(emb_mutex_lock(m1, EMB_WAIT_FOREVER));
    emb_test_mark(id);
    EMB_ASSERT_OK(emb_mutex_unlock(m1));
}

EMB_TEST(mutex_waiter_priority_change_reorders_and_propagates)
{
    emb_thread_t low;
    emb_thread_t b;
    EMB_TEST_REQ("KRN-WAIT-003", "KRN-SYNC-018");
    make(&st1, &m1, EMB_MUTEX_INHERIT, 0u, 0u);
    low = emb_test_thread(EMB_TEST_NAME("low"), 2u, 0u, hold_m1_for, (void *)10);
    b = emb_test_thread(EMB_TEST_NAME("b"), 3u, 0u, lock_mark_unlock, (void *)3);
    (void)emb_test_thread(EMB_TEST_NAME("c"), 4u, 0u, lock_mark_unlock, (void *)4);
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(low), 4u);
    EMB_ASSERT_OK(emb_thread_set_priority(b, 6u));
    EMB_ASSERT_EQ(emb_thread_get_effective_priority(low), 6u);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(15)));
    EMB_ASSERT_MARKS(1, 2, 3, 4, 3); /* b (now 6) gets the mutex before c; low finishes last */
    EMB_ASSERT_OK(emb_mutex_destroy(m1));
}

EMB_TEST(mutex_init_validates_arguments)
{
    emb_mutex_attr_t attr;
    EMB_TEST_REQ("API-015");
    emb_mutex_attr_default(&attr);
    attr.protocol = 7u;
    EMB_ASSERT_MISUSE(emb_mutex_init(&st1, &attr, &m1), EMB_EINVAL);
    attr.protocol = EMB_MUTEX_CEILING;
    attr.ceiling = 0u;
    EMB_ASSERT_MISUSE(emb_mutex_init(&st1, &attr, &m1), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_mutex_init(NULL, NULL, &m1), EMB_EINVAL);
}
