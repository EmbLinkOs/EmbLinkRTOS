/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Condition variable conformance (SPEC-005 §6; KRN-SYNC-030, KRN-WAIT-013). */
#include <emb_test.h>

static emb_cond_storage_t cond_storage;
static emb_mutex_storage_t mutex_storage;
static emb_cond_t cv;
static emb_mutex_t mx;

static void make(uint8_t cond_flags, uint8_t mutex_flags)
{
    emb_cond_attr_t ca;
    emb_mutex_attr_t ma;
    emb_cond_attr_default(&ca);
    ca.flags = cond_flags;
    emb_mutex_attr_default(&ma);
    ma.flags = mutex_flags;
    EMB_ASSERT_OK(emb_cond_init(&cond_storage, &ca, &cv));
    EMB_ASSERT_OK(emb_mutex_init(&mutex_storage, &ma, &mx));
}

static void teardown(void)
{
    EMB_ASSERT_OK(emb_cond_destroy(cv));
    EMB_ASSERT_OK(emb_mutex_destroy(mx));
}

/* Locks the mutex, waits up to arg ticks (0: forever), marks the status and ownership, unlocks:
 * mark = id * 10 + (owner ? 1 : 0) on EMB_OK, + 100 timed out, + 200 canceled, + 300
 * destroyed. */
static void waiter(void *arg)
{
    uintptr_t a = (uintptr_t)arg;
    uint32_t ticks = (uint32_t)(a & 0xFFu);
    int id = (int)((a >> 8) & 0xFFu);
    emb_status_t st;
    int base;
    EMB_ASSERT_OK(emb_mutex_lock(mx, EMB_WAIT_FOREVER));
    st = emb_cond_wait(cv, mx, ticks == 0u ? EMB_WAIT_FOREVER : EMB_TIMEOUT(EMB_TICKS(ticks)));
    base = (st == EMB_OK)           ? 0
           : (st == EMB_ETIMEDOUT)  ? 100
           : (st == EMB_ECANCELED)  ? 200
           : (st == EMB_EDESTROYED) ? 300
                                    : 900;
    emb_test_mark(base + id * 10 + (emb_mutex_is_owner(mx) ? 1 : 0));
    EMB_ASSERT_OK(emb_mutex_unlock(mx));
}

#define WAIT_ARG(ticks, id) ((void *)(uintptr_t)((ticks) | ((id) << 8)))

EMB_TEST(cond_signal_wakes_one_waiter_which_owns_the_mutex)
{
    EMB_TEST_REQ("KRN-SYNC-030", "KRN-WAIT-001");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(0u, 3u));
    (void)emb_test_thread(EMB_TEST_NAME("w4"), 4u, 0u, waiter, WAIT_ARG(0u, 4u));
    EMB_ASSERT_OK(emb_cond_signal(cv)); /* the higher waiter */
    EMB_ASSERT_MARKS(41);
    EMB_ASSERT_OK(emb_cond_signal(cv));
    EMB_ASSERT_MARKS(41, 31);
    EMB_ASSERT_OK(emb_cond_signal(cv)); /* nobody waits: the signal is lost */
    teardown();
}

EMB_TEST(cond_broadcast_wakes_all_in_priority_order)
{
    EMB_TEST_REQ("KRN-SYNC-030", "KRN-WAIT-013");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w2"), 2u, 0u, waiter, WAIT_ARG(0u, 2u));
    (void)emb_test_thread(EMB_TEST_NAME("w4"), 4u, 0u, waiter, WAIT_ARG(0u, 4u));
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(0u, 3u));
    EMB_ASSERT_OK(emb_cond_broadcast(cv));
    EMB_ASSERT_MARKS(41, 31, 21); /* each relocks in turn and owns the mutex */
    teardown();
}

EMB_TEST(cond_signal_is_not_stored)
{
    uint32_t t0;
    EMB_TEST_REQ("KRN-SYNC-030");
    make(0u, 0u);
    EMB_ASSERT_OK(emb_cond_signal(cv));
    EMB_ASSERT_OK(emb_cond_broadcast(cv));
    t0 = EMB_TEST_NOW();
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(5u, 3u));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    EMB_ASSERT_MARKS(131); /* timed out, owning the mutex */
    EMB_ASSERT_ELAPSED(t0, 10u);
    teardown();
}

EMB_TEST(cond_relock_after_timeout_waits_for_the_mutex)
{
    EMB_TEST_REQ("KRN-SYNC-030");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(2u, 3u));
    EMB_ASSERT_OK(emb_mutex_lock(mx, EMB_NO_WAIT)); /* released by the waiting thread */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(5)));  /* its wait times out meanwhile */
    emb_test_mark(1);
    EMB_ASSERT_OK(emb_mutex_unlock(mx)); /* only now can it relock and return */
    EMB_ASSERT_MARKS(1, 131);
    teardown();
}

EMB_TEST(cond_cancel_returns_with_the_mutex_owned)
{
    emb_thread_t h;
    EMB_TEST_REQ("KRN-SYNC-030", "KRN-WAIT-017");
    make(0u, 0u);
    h = emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(0u, 3u));
    EMB_ASSERT_OK(emb_thread_cancel(h));
    EMB_ASSERT_MARKS(231);
    teardown();
}

EMB_TEST(cond_destroy_aborts_waiters_which_relock)
{
    EMB_TEST_REQ("KRN-OBJ-001", "KRN-SYNC-030");
    make(EMB_OBJ_ABORT_WAITERS, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(0u, 3u));
    EMB_ASSERT_OK(emb_cond_destroy(cv));
    EMB_ASSERT_MARKS(331);
    EMB_ASSERT_OK(emb_mutex_destroy(mx));
}

EMB_TEST(cond_destroy_with_waiters_is_misuse)
{
    EMB_TEST_REQ("KRN-OBJ-001", "API-013");
    make(0u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("w3"), 3u, 0u, waiter, WAIT_ARG(0u, 3u));
    EMB_ASSERT_MISUSE(emb_cond_destroy(cv), EMB_EBUSY);
    EMB_ASSERT_OK(emb_cond_signal(cv));
    EMB_ASSERT_MARKS(31);
    teardown();
}

EMB_TEST(cond_wait_requires_single_ownership)
{
    EMB_TEST_REQ("KRN-SYNC-030", "API-013");
    make(0u, EMB_MUTEX_RECURSIVE);
    EMB_ASSERT_MISUSE(emb_cond_wait(cv, mx, EMB_NO_WAIT), EMB_EPERM); /* not the owner */
    EMB_ASSERT_OK(emb_mutex_lock(mx, EMB_NO_WAIT));
    EMB_ASSERT_STATUS(emb_cond_wait(cv, mx, EMB_NO_WAIT), EMB_ETIMEDOUT); /* kept */
    EMB_ASSERT_TRUE(emb_mutex_is_owner(mx));
    EMB_ASSERT_OK(emb_mutex_lock(mx, EMB_NO_WAIT)); /* recursion count 2 */
    EMB_ASSERT_STATUS(emb_cond_wait(cv, mx, EMB_TIMEOUT(EMB_TICKS(1))), EMB_ESTATE);
    EMB_ASSERT_OK(emb_mutex_unlock(mx));
    EMB_ASSERT_OK(emb_mutex_unlock(mx));
    teardown();
}

#if CONFIG_EMB_OBJ_LIST
EMB_TEST(cond_storage_reuse_requires_destroy)
{
    emb_cond_t other;
    EMB_TEST_REQ("KRN-OBJ-007", "KRN-OBJ-011");
    make(0u, 0u);
    EMB_ASSERT_MISUSE(emb_cond_init(&cond_storage, NULL, &other), EMB_EBUSY);
    teardown();
}
#endif

EMB_COND_DEFINE(defined_cond, 0u);

EMB_TEST(cond_define_constructs_in_init_table)
{
    EMB_TEST_REQ("API-021", "KRN-OBJ-009");
    EMB_ASSERT_OK(emb_cond_signal(defined_cond)); /* no waiter: a no-op on a live object */
    EMB_ASSERT_OK(emb_cond_destroy(defined_cond));
}
