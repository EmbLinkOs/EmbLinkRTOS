/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Numeric public-entry identity and complete misuse records (SPEC-001 §5.3). */
#include <emb/api_ids.h>

#include <emb_test.h>

EMB_TEST(fault_successful_check_preserves_status)
{
    EMB_TEST_REQ("API-015", "API-017");
    EMB_ASSERT_EQ(emb_check_status(EMB_OK, NULL), EMB_OK);
    EMB_ASSERT_TRUE(EMB_API_ID_MUTEX_LOCK != EMB_API_ID_MUTEX_LOCK_UNTIL);
    EMB_ASSERT_TRUE(EMB_API_ID_SEM_TAKE != EMB_API_ID_SEM_TAKE_UNTIL);
}

EMB_TEST(fault_status_policy_is_preserved)
{
    emb_thread_t null_thread = EMB_HANDLE_NULL;
    EMB_TEST_REQ("API-015");
    EMB_ASSERT_MISUSE(emb_thread_start(null_thread), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_thread_set_priority(emb_thread_self(), 0u), EMB_EINVAL);
}

#if CONFIG_EMB_ARCH_NATIVE && CONFIG_EMB_CHECKED
#include <embk/kernel.h>
#include <embk/sync.h>

#include <emb_native.h>

static uint16_t expected_api;
static uint8_t expected_class;
static uint8_t expected_argument;
static uint16_t expected_code;
static uint8_t expected_context;
static uint8_t expected_thread;

static void check_fault_record(const emb_fault_info_t *info)
{
    if (info->api_id != expected_api || info->fault_class != expected_class ||
        info->argument != expected_argument || info->code != expected_code ||
        info->context != expected_context || info->thread_index != expected_thread) {
        emb_native_exit(97);
    }
#if !CONFIG_EMB_FAULT_WHERE
    if (expected_api != EMB_API_ID_NONE && info->where != NULL) {
        emb_native_exit(97);
    }
#endif
    if (expected_class == EMB_FAULT_HARDWARE &&
        (info->address != 11u || info->pc != 22u || info->sp != 33u)) {
        emb_native_exit(97);
    }
}

static void expect_record(uint16_t api, uint8_t fault_class, uint8_t argument, uint16_t code)
{
    expected_api = api;
    expected_class = fault_class;
    expected_argument = argument;
    expected_code = code;
    expected_context = EMB_CONTEXT_THREAD;
    expected_thread = emb_thread_index(emb_thread_self());
    emb_fault_set_hook(check_fault_record);
}

EMB_TEST(fault_public_wrapper_ids_survive_shared_helpers)
{
    emb_sem_t null_sem = EMB_HANDLE_NULL;
    emb_mutex_t null_mutex = EMB_HANDLE_NULL;
    EMB_TEST_REQ("API-015", "API-017");
    expect_record(EMB_API_ID_SEM_TAKE, EMB_FAULT_API_HANDLE, 1u, (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_sem_take(null_sem, EMB_NO_WAIT));
    expect_record(EMB_API_ID_SEM_TAKE_UNTIL, EMB_FAULT_API_HANDLE, 1u, (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_sem_take_until(null_sem, emb_time_now()));
    expect_record(EMB_API_ID_MUTEX_LOCK, EMB_FAULT_API_HANDLE, 1u, (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_mutex_lock(null_mutex, EMB_NO_WAIT));
    expect_record(EMB_API_ID_MUTEX_LOCK_UNTIL, EMB_FAULT_API_HANDLE, 1u, (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_mutex_lock_until(null_mutex, emb_time_now()));
    expect_record(EMB_API_ID_NOTIFY_WAIT, EMB_FAULT_API_ARGUMENT, 1u, (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_notify_wait(0u, 0u, EMB_NO_WAIT, NULL));
    expect_record(EMB_API_ID_NOTIFY_WAIT_UNTIL, EMB_FAULT_API_ARGUMENT, 1u,
                  (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_notify_wait_until(0u, 0u, emb_time_now(), NULL));
    emb_fault_set_hook(NULL);
}

EMB_TEST(fault_all_misuse_classes_and_public_argument_numbers)
{
    EMB_TEST_REQ("API-013", "API-015", "API-017");
    expect_record(EMB_API_ID_THREAD_SET_PRIORITY, EMB_FAULT_API_ARGUMENT, 2u,
                  (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_thread_set_priority(emb_thread_self(), 0u));
    expect_record(EMB_API_ID_THREAD_JOIN, EMB_FAULT_API_OWNER, 1u, (uint16_t)(-EMB_EDEADLK));
    EMB_ASSERT_FAULTS((void)emb_thread_join(emb_thread_self(), EMB_NO_WAIT, NULL));
    expect_record(EMB_API_ID_KERNEL_INIT, EMB_FAULT_API_LIFECYCLE, 0u, 0u);
    EMB_ASSERT_FAULTS(emb_kernel_init());
    expect_record(EMB_API_ID_KERNEL_START, EMB_FAULT_API_LIFECYCLE, 0u, 1u);
    EMB_ASSERT_FAULTS(emb_kernel_start());
    emb_fault_set_hook(NULL);
}

#if CONFIG_EMB_MUTEX_OWNER_DEATH_FAULT
EMB_TEST(fault_owner_death_helper_attributes_thread_exit)
{
    static emb_mutex_storage_t storage;
    emb_mutex_t mutex;
    EMB_TEST_REQ("API-015", "API-017", "KRN-SYNC-011");
    EMB_ASSERT_OK(emb_mutex_init(&storage, NULL, &mutex));
    EMB_ASSERT_OK(emb_mutex_lock(mutex, EMB_NO_WAIT));
    expect_record(EMB_API_ID_THREAD_EXIT, EMB_FAULT_API_LIFECYCLE, 0u, 2u);
    /* White-box call faults before any switch, preserving the fork contract. */
    EMB_ASSERT_FAULTS(embk_mutex_release_all_on_exit(embk_cpu.current));
    emb_fault_set_hook(NULL);
    EMB_ASSERT_OK(emb_mutex_unlock(mutex));
    EMB_ASSERT_OK(emb_mutex_destroy(mutex));
}
#endif

static void invalid_irq_in_isr(void *arg)
{
    (void)arg;
    (void)emb_irq_set_level(UINT16_MAX, UINT8_MAX);
}

EMB_TEST(fault_isr_context_precedes_bad_arguments_and_has_no_thread)
{
    EMB_TEST_REQ("API-013", "API-015", "API-017");
    emb_test_irq_connect(0u, invalid_irq_in_isr, NULL);
    expect_record(EMB_API_ID_IRQ_SET_LEVEL, EMB_FAULT_API_CONTEXT, 0u, (uint16_t)(-EMB_EPERM));
    expected_context = EMB_CONTEXT_ISR;
    expected_thread = 0xFFu;
    EMB_ASSERT_FAULTS(emb_test_irq_raise(0u));
    emb_fault_set_hook(NULL);
}

EMB_TEST(fault_prekernel_context_and_blocking_substate_keep_entry_id)
{
    emb_sem_t null_sem = EMB_HANDLE_NULL;
    EMB_TEST_REQ("API-013", "API-015", "API-017");
    expect_record(EMB_API_ID_SEM_TAKE, EMB_FAULT_API_CONTEXT, 0u, (uint16_t)(-EMB_EPERM));
    expected_context = EMB_CONTEXT_PREKERNEL;
    expected_thread = 0xFFu;
    /* Synthetic prekernel CPU state in the fork; the API must reject before switch. */
    EMB_ASSERT_FAULTS({
        embk_cpu.kernel_state = EMBK_KERNEL_INIT;
        (void)emb_sem_take(null_sem, EMB_NO_WAIT);
    });
    expect_record(EMB_API_ID_THREAD_SLEEP, EMB_FAULT_API_CONTEXT, 0u, (uint16_t)(-EMB_EPERM));
    EMB_ASSERT_FAULTS({
        emb_sched_lock();
        (void)emb_thread_sleep(EMB_TICKS(1));
    });
    expect_record(EMB_API_ID_THREAD_SLEEP_UNTIL, EMB_FAULT_API_CONTEXT, 0u, (uint16_t)(-EMB_EPERM));
    EMB_ASSERT_FAULTS({
        emb_sched_lock();
        (void)emb_thread_sleep_until(emb_instant_add(emb_time_now(), EMB_TICKS(1)));
    });
    emb_fault_set_hook(NULL);
}

EMB_TEST(fault_generic_manual_check_invariant_and_hardware_ids_are_zero)
{
    EMB_TEST_REQ("API-015", "API-017");
    expect_record(EMB_API_ID_NONE, EMB_FAULT_API_ARGUMENT, 0u, 42u);
    EMB_ASSERT_FAULTS(emb_fault(EMB_FAULT_API_ARGUMENT, 42u, NULL));
    expect_record(EMB_API_ID_NONE, EMB_FAULT_CHECK, 0u, (uint16_t)(-EMB_EINVAL));
    EMB_ASSERT_FAULTS((void)emb_check_status(EMB_EINVAL, NULL));
    expect_record(EMB_API_ID_NONE, EMB_FAULT_KERNEL_INVARIANT, 0u, 17u);
    EMB_ASSERT_FAULTS(embk_fault_raise(EMB_FAULT_KERNEL_INVARIANT, 17u, 0u, NULL));
    expect_record(EMB_API_ID_NONE, EMB_FAULT_HARDWARE, 0u, 9u);
    EMB_ASSERT_FAULTS(embk_fault_raise_hw(9u, 11u, 22u, 33u));
    emb_fault_set_hook(NULL);
}
#endif
