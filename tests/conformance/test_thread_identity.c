/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Diagnostic identity is independent of scheduler priority and execution lifetime. */
#include <embk/kernel.h>
#include <embk/sched.h>

#include <emb_test.h>

static void identity_entry(void *arg)
{
    (void)arg;
}

EMB_TEST(thread_identity_survives_priority_and_termination)
{
    emb_thread_attr_t attr;
    emb_thread_t a;
    emb_thread_t b;
    emb_thread_t null_thread = EMB_HANDLE_NULL;
    uint8_t index;
    EMB_TEST_REQ("KRN-THR-025");
    emb_thread_attr_default(&attr);
    attr.priority = 2u;
    attr.stack = emb_test_scratch_stack(&attr.stack_size);
    EMB_ASSERT_OK(emb_thread_init(emb_test_scratch_storage(), &attr, identity_entry, NULL, &a));
    index = emb_thread_index(a);
    EMB_ASSERT_TRUE(index < 0xFEu);
    EMB_ASSERT_TRUE(index != emb_thread_index(emb_thread_self()));
    EMB_ASSERT_OK(emb_thread_set_priority(a, 5u));
    EMB_ASSERT_EQ(emb_thread_index(a), index);
    EMB_ASSERT_OK(emb_thread_start(a));
    EMB_ASSERT_EQ(emb_thread_index(a), index);
    EMB_ASSERT_MISUSE(emb_thread_destroy(a), EMB_EBUSY); /* rejected destroy keeps ID */
    b = emb_test_thread(EMB_TEST_NAME("other"), 3u, 0u, identity_entry, NULL);
    EMB_ASSERT_TRUE(emb_thread_index(b) != index); /* exit does not release a's ID */
    EMB_ASSERT_OK(emb_thread_join(a, EMB_NO_WAIT, NULL));
    EMB_ASSERT_OK(emb_thread_destroy(a));
    EMB_ASSERT_EQ(emb_thread_index(a), 0xFFu); /* storage still exists, state is UNINIT */
    EMB_ASSERT_EQ(emb_thread_index(null_thread), 0xFFu);
}

EMB_TEST(thread_identity_repeated_construction_does_not_alias_live_object)
{
    emb_thread_attr_t attr;
    emb_thread_t h;
    unsigned i;
    uint8_t runner_index = emb_thread_index(emb_thread_self());
    EMB_TEST_REQ("KRN-THR-025");
    emb_thread_attr_default(&attr);
    attr.priority = 2u;
    attr.stack = emb_test_scratch_stack(&attr.stack_size);
    for (i = 0u; i < 300u; i++) {
        EMB_ASSERT_OK(emb_thread_init(emb_test_scratch_storage(), &attr, identity_entry, NULL, &h));
        EMB_ASSERT_TRUE(emb_thread_index(h) < 0xFEu);
        EMB_ASSERT_TRUE(emb_thread_index(h) != runner_index);
        EMB_ASSERT_OK(emb_thread_destroy(h));
    }
}

#if CONFIG_EMB_SEM
static emb_sem_storage_t identity_sem_storage;
static emb_sem_t identity_sem;

static void identity_wait(void *arg)
{
    (void)arg;
    EMB_ASSERT_OK(emb_sem_take(identity_sem, EMB_WAIT_FOREVER));
}

EMB_TEST(thread_identity_survives_suspend_and_resume)
{
    emb_thread_t h;
    uint8_t index;
    EMB_TEST_REQ("KRN-THR-025");
    EMB_ASSERT_OK(emb_sem_init(&identity_sem_storage, NULL, &identity_sem));
    h = emb_test_thread(EMB_TEST_NAME("identity"), 2u, 0u, identity_wait, NULL);
    index = emb_thread_index(h);
    EMB_ASSERT_OK(emb_thread_suspend(h));
    EMB_ASSERT_EQ(emb_thread_index(h), index);
    EMB_ASSERT_OK(emb_thread_resume(h));
    EMB_ASSERT_EQ(emb_thread_index(h), index);
    EMB_ASSERT_OK(emb_sem_give(identity_sem));
    EMB_ASSERT_OK(emb_thread_join(h, EMB_NO_WAIT, NULL));
    EMB_ASSERT_EQ(emb_thread_index(h), index);
    EMB_ASSERT_OK(emb_sem_destroy(identity_sem));
}
#endif

#if CONFIG_EMB_ARCH_NATIVE && !CONFIG_EMB_SCHED_TABLE
/* Never started: native context init creates no host thread. Distinct caller-owned
 * stacks make this a real admission test without borrowing another object's storage. */
static emb_thread_storage_t full_storage[254];
static EMB_ALIGNED(EMB_STACK_ALIGN) uint8_t full_stacks[254][EMB_TEST_STACK_SIZE];
static emb_thread_t full_handles[254];

EMB_TEST(thread_identity_exhaustion_is_atomic_and_destroy_releases_id)
{
    emb_thread_attr_t attr;
    bool seen[254] = {false};
    unsigned n;
    unsigned j;
    emb_status_t st = EMB_OK;
    emb_thread_t failed;
    uint8_t released;
    EMB_TEST_REQ("KRN-THR-025", "KRN-THR-023");
    emb_thread_attr_default(&attr);
    attr.priority = 2u;
    attr.stack_size = sizeof(full_stacks[0]);
    seen[emb_thread_index(emb_thread_self())] = true;
    for (n = 0u; n < 254u; n++) {
        uint8_t index;
        attr.stack = full_stacks[n];
        st = emb_thread_init(&full_storage[n], &attr, identity_entry, NULL, &full_handles[n]);
        if (st != EMB_OK) {
            break;
        }
        index = emb_thread_index(full_handles[n]);
        EMB_ASSERT_TRUE(index < 254u);
        if (index < 254u) {
            EMB_ASSERT_TRUE(!seen[index]);
            seen[index] = true;
        }
    }
    EMB_ASSERT_EQ(n, 253u); /* one ID belongs to the runner */
    EMB_ASSERT_EQ(st, EMB_ENOMEM);
    if (n != 253u) {
        return;
    }
    EMB_ASSERT_EQ(full_handles[n].raw, 0u);
    for (j = 0u; j < sizeof(full_storage[n]); j++) {
        ((uint8_t *)(void *)&full_storage[n])[j] = 0xA5u;
    }
    failed.raw = 1u;
    EMB_ASSERT_STATUS(emb_thread_init(&full_storage[n], &attr, identity_entry, NULL, &failed),
                      EMB_ENOMEM);
    EMB_ASSERT_EQ(failed.raw, 0u);
    for (j = 0u; j < sizeof(full_storage[n]); j++) {
        EMB_ASSERT_EQ(((uint8_t *)(void *)&full_storage[n])[j], 0xA5u);
    }
    released = emb_thread_index(full_handles[0]);
    EMB_ASSERT_OK(emb_thread_destroy(full_handles[0]));
    EMB_ASSERT_OK(emb_thread_init(&full_storage[n], &attr, identity_entry, NULL, &failed));
    EMB_ASSERT_EQ(emb_thread_index(failed), released); /* the only free ID */
    EMB_ASSERT_OK(emb_thread_destroy(failed));
    for (j = 1u; j < n; j++) {
        EMB_ASSERT_OK(emb_thread_destroy(full_handles[j]));
    }
}
#endif

EMB_TEST(thread_storage_reuse_requires_destroy)
{
    emb_thread_attr_t attr;
    emb_thread_t a;
    emb_thread_t again;
    uint8_t index;
    EMB_TEST_REQ("KRN-THR-015", "KRN-THR-023");
    emb_thread_attr_default(&attr);
    attr.priority = 2u;
    attr.stack = emb_test_scratch_stack(&attr.stack_size);
    EMB_ASSERT_OK(emb_thread_init(emb_test_scratch_storage(), &attr, identity_entry, NULL, &a));
    index = emb_thread_index(a);
    /* INACTIVE, not destroyed */
    EMB_ASSERT_MISUSE(
        emb_thread_init(emb_test_scratch_storage(), &attr, identity_entry, NULL, &again),
        EMB_EBUSY);
    EMB_ASSERT_OK(emb_thread_start(a)); /* runs to its end at once (priority 2) */
    EMB_ASSERT_OK(emb_thread_join(a, EMB_WAIT_FOREVER, NULL));
    /* joined: destroyable, but still not reusable by init */
    EMB_ASSERT_MISUSE(
        emb_thread_init(emb_test_scratch_storage(), &attr, identity_entry, NULL, &again),
        EMB_EBUSY);
    EMB_ASSERT_EQ(emb_thread_index(a), index); /* the refused init left the object intact */
    EMB_ASSERT_OK(emb_thread_destroy(a));
    EMB_ASSERT_OK(emb_thread_init(emb_test_scratch_storage(), &attr, identity_entry, NULL, &again));
    EMB_ASSERT_OK(emb_thread_destroy(again));
}

EMB_TEST(idle_context_is_not_an_application_thread)
{
    emb_thread_t idle;
    emb_thread_t self;
    embk_thread_t *saved;
    emb_irq_key_t key;
    uint8_t state = 0u;
    EMB_TEST_REQ("KRN-THR-025", "KRN-THR-026");
    idle.raw = embk_obj_to_raw(EMBK_IDLE);
    EMB_ASSERT_EQ(embk_thread_index(EMBK_IDLE), 0xFEu); /* traces and faults still name it */
    EMB_ASSERT_EQ(emb_thread_index(idle), 0xFFu);       /* no handle resolves to it */
    EMB_ASSERT_EQ(emb_thread_get_priority(idle), 0u);
    EMB_ASSERT_MISUSE(emb_thread_state(idle, &state, NULL), EMB_EINVAL);
#if CONFIG_EMB_NOTIFY
    EMB_ASSERT_MISUSE(emb_notify_set(idle, EMB_NOTIFY_BIT(0)), EMB_EINVAL);
#endif
    key = emb_irq_lock();
    saved = embk_cpu.current;
    embk_cpu.current = EMBK_IDLE; /* as seen from the idle context */
    self = emb_thread_self();
    embk_cpu.current = saved;
    emb_irq_unlock(key);
    EMB_ASSERT_EQ(self.raw, 0u);
}
