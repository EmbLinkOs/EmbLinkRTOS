/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Work queue conformance (SPEC-006 §4; KRN-WQ-001, 002, 004 to 006, 008 to 010). */
#include <emb_test.h>

#define IRQ_W 2u /* software interrupt slot */

static emb_workq_storage_t wq_storage;
EMB_THREAD_STACK(wq_stack, EMB_TEST_STACK_SIZE);
static emb_workq_t wq;

/* An item that marks its id and, if asked, sleeps or resubmits itself while it runs. */
typedef struct {
    emb_work_t work;
    int id;
    uint32_t sleep_ticks;
    bool resubmit_once;
} item_t;

static item_t items[3];

static void handler(emb_work_t *work)
{
    item_t *it = EMB_CONTAINER_OF(work, item_t, work);
    emb_test_mark(it->id);
    if (it->resubmit_once) {
        it->resubmit_once = false;
        EMB_ASSERT_OK(emb_work_submit(wq, work)); /* while running: queued again after */
    }
    if (it->sleep_ticks != 0u) {
        EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(it->sleep_ticks)));
    }
    emb_test_mark(it->id + 10);
}

static void make(uint8_t priority)
{
    emb_workq_attr_t attr;
    unsigned i;
    emb_workq_attr_default(&attr);
    attr.name = "wq";
    attr.stack = wq_stack;
    attr.stack_size = sizeof(wq_stack);
    attr.priority = priority;
    EMB_ASSERT_OK(emb_workq_init(&wq_storage, &attr, &wq));
    for (i = 0u; i < 3u; i++) {
        emb_work_init(&items[i].work, handler);
        items[i].id = (int)i + 1;
        items[i].sleep_ticks = 0u;
        items[i].resubmit_once = false;
    }
}

EMB_TEST(workq_runs_items_in_fifo_order_on_its_thread)
{
    EMB_TEST_REQ("KRN-WQ-001", "KRN-WQ-002", "KRN-WQ-005", "KRN-WQ-009");
    make(2u);
    emb_sched_lock(); /* queue three before the queue's thread can run */
    EMB_ASSERT_OK(emb_work_submit(wq, &items[0].work));
    EMB_ASSERT_OK(emb_work_submit(wq, &items[1].work));
    EMB_ASSERT_STATUS(emb_work_submit(wq, &items[0].work), EMB_EEXIST); /* not queued twice */
    EMB_ASSERT_OK(emb_work_submit(wq, &items[2].work));
    EMB_ASSERT_EQ(emb_work_state(&items[1].work), EMB_WORK_PENDING);
    emb_sched_unlock(); /* the higher queue thread drains them, one at a time */
    EMB_ASSERT_MARKS(1, 11, 2, 12, 3, 13);
    EMB_ASSERT_EQ(emb_work_state(&items[2].work), EMB_WORK_IDLE);
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}

EMB_TEST(workq_resubmit_while_running_runs_again)
{
    EMB_TEST_REQ("KRN-WQ-005");
    make(2u);
    items[0].resubmit_once = true;
    EMB_ASSERT_OK(emb_work_submit(wq, &items[0].work));
    EMB_ASSERT_MARKS(1, 11, 1, 11); /* a second run, never concurrent with the first */
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}

EMB_TEST(workq_item_belongs_to_one_queue_at_a_time)
{
    static emb_workq_storage_t other_storage;
    EMB_THREAD_STACK(other_stack, EMB_TEST_STACK_SIZE); /* static storage */
    emb_workq_attr_t attr;
    emb_workq_t other;
    EMB_TEST_REQ("KRN-WQ-005");
    make(2u);
    emb_workq_attr_default(&attr);
    attr.stack = other_stack;
    attr.stack_size = sizeof(other_stack);
    attr.priority = 2u;
    EMB_ASSERT_OK(emb_workq_init(&other_storage, &attr, &other));
    items[0].sleep_ticks = 2u;
    EMB_ASSERT_OK(emb_work_submit(wq, &items[0].work)); /* running on wq: sleeps */
    EMB_ASSERT_STATUS(emb_work_submit(other, &items[0].work), EMB_EBUSY);
    EMB_ASSERT_OK(emb_work_cancel_sync(&items[0].work, EMB_WAIT_FOREVER));
    EMB_ASSERT_OK(emb_work_submit(other, &items[0].work)); /* idle again: any queue */
    EMB_ASSERT_OK(emb_workq_flush(other, EMB_WAIT_FOREVER));
    EMB_ASSERT_MARKS(1, 11, 1, 11);
    EMB_ASSERT_OK(emb_workq_destroy(other));
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}

EMB_TEST(workq_cancel_reports_pending_running_and_idle)
{
    emb_work_state_t st = EMB_WORK_IDLE;
    EMB_TEST_REQ("KRN-WQ-004", "KRN-WQ-006");
    make(2u);
    emb_sched_lock();
    EMB_ASSERT_OK(emb_work_submit(wq, &items[0].work));
    EMB_ASSERT_OK(emb_work_cancel(&items[0].work, &st));
    EMB_ASSERT_EQ(st, EMB_WORK_PENDING); /* unlinked: it will not run */
    EMB_ASSERT_OK(emb_work_cancel(&items[0].work, &st));
    EMB_ASSERT_EQ(st, EMB_WORK_IDLE);
    emb_sched_unlock();
    items[1].sleep_ticks = 5u;
    EMB_ASSERT_OK(emb_work_submit(wq, &items[1].work)); /* starts, then sleeps */
    EMB_ASSERT_MARKS(2);
    EMB_ASSERT_OK(emb_work_cancel(&items[1].work, &st));
    EMB_ASSERT_EQ(st, EMB_WORK_RUNNING); /* the handler completes */
    EMB_ASSERT_STATUS(emb_work_cancel_sync(&items[1].work, EMB_NO_WAIT), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_work_cancel_sync(&items[1].work, EMB_WAIT_FOREVER)); /* until it returns */
    EMB_ASSERT_MARKS(2, 12);
    EMB_ASSERT_EQ(emb_work_state(&items[1].work), EMB_WORK_IDLE);
    EMB_ASSERT_OK(emb_work_cancel_sync(&items[1].work, EMB_NO_WAIT)); /* idle: at once */
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}

EMB_TEST(workq_flush_waits_for_items_queued_before_it)
{
    EMB_TEST_REQ("KRN-WQ-006", "KRN-WAIT-012");
    make(2u);
    EMB_ASSERT_OK(emb_workq_flush(wq, EMB_NO_WAIT)); /* idle: at once */
    items[0].sleep_ticks = 3u;
    emb_sched_lock();
    EMB_ASSERT_OK(emb_work_submit(wq, &items[0].work));
    EMB_ASSERT_OK(emb_work_submit(wq, &items[1].work));
    emb_sched_unlock(); /* item 1 starts and sleeps */
    EMB_ASSERT_STATUS(emb_workq_flush(wq, EMB_TIMEOUT(EMB_TICKS(1))), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_workq_flush(wq, EMB_WAIT_FOREVER));
    EMB_ASSERT_MARKS(1, 11, 2, 12);       /* both ran before the flush returned */
    EMB_ASSERT_OK(emb_workq_destroy(wq)); /* the timed-out flush left no marker behind */
}

EMB_TEST_ISR(isr_submit)
{
    (void)emb_isr_arg_;
    emb_test_mark(100);
    EMB_ASSERT_OK(emb_work_submit(wq, &items[0].work));
    EMB_ASSERT_STATUS(emb_work_submit(wq, &items[0].work), EMB_EEXIST);
}

EMB_TEST(workq_submit_from_isr_runs_at_exit)
{
    EMB_TEST_REQ("KRN-WQ-001", "KRN-IRQ-010");
    make(2u);
    emb_test_irq_connect(IRQ_W, isr_submit, NULL);
    emb_test_irq_raise(IRQ_W);
    emb_test_mark(5);
    EMB_ASSERT_MARKS(100, 1, 11, 5);
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}

static emb_thread_t handler_self;

static void identity_handler(emb_work_t *work)
{
    (void)work;
    handler_self = emb_thread_self();
}

EMB_TEST(workq_thread_is_a_kernel_thread)
{
    emb_work_t w;
    EMB_TEST_REQ("KRN-WQ-009", "KRN-WQ-010");
    make(2u);
    emb_work_init(&w, identity_handler);
    EMB_ASSERT_OK(emb_work_submit(wq, &w));
    EMB_ASSERT_TRUE(!EMB_HANDLE_IS_NULL(handler_self));
    EMB_ASSERT_EQ(emb_thread_get_priority(handler_self), 2u);
    EMB_ASSERT_MISUSE(emb_thread_cancel(handler_self), EMB_EPERM); /* kernel-owned */
    EMB_ASSERT_MISUSE(emb_notify_set(handler_self, EMB_NOTIFY_BIT(0)), EMB_EPERM);
    EMB_ASSERT_STATUS(emb_thread_join(handler_self, EMB_NO_WAIT, NULL), EMB_EPERM);
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}

EMB_TEST(workq_destroy_refuses_a_busy_queue)
{
    EMB_TEST_REQ("KRN-OBJ-001", "KRN-WQ-010", "API-013");
    make(2u);
    items[0].sleep_ticks = 2u;
    EMB_ASSERT_OK(emb_work_submit(wq, &items[0].work)); /* running: sleeps */
    EMB_ASSERT_MISUSE(emb_workq_destroy(wq), EMB_EBUSY);
    EMB_ASSERT_OK(emb_work_cancel_sync(&items[0].work, EMB_WAIT_FOREVER));
    EMB_ASSERT_MARKS(1, 11);
    EMB_ASSERT_OK(emb_workq_destroy(wq)); /* idle: its thread exits and is joined */
    EMB_ASSERT_MISUSE(emb_work_submit(wq, &items[0].work), EMB_EINVAL); /* stale handle */
}

#if CONFIG_EMB_OBJ_LIST
EMB_TEST(workq_storage_reuse_requires_destroy)
{
    emb_workq_attr_t attr;
    emb_workq_t other;
    EMB_TEST_REQ("KRN-OBJ-007", "KRN-OBJ-011");
    make(2u);
    emb_workq_attr_default(&attr);
    attr.stack = wq_stack;
    attr.stack_size = sizeof(wq_stack);
    attr.priority = 2u;
    EMB_ASSERT_MISUSE(emb_workq_init(&wq_storage, &attr, &other), EMB_EBUSY);
    EMB_ASSERT_OK(emb_workq_destroy(wq));
}
#endif

#if CONFIG_EMB_SYSTEM_WORKQ
EMB_TEST(workq_system_queue_runs_items)
{
    emb_workq_t sys = emb_workq_system();
    EMB_TEST_REQ("KRN-WQ-008");
    EMB_ASSERT_TRUE(!EMB_HANDLE_IS_NULL(sys));
    emb_work_init(&items[0].work, handler);
    items[0].id = 7;
    items[0].sleep_ticks = 0u;
    items[0].resubmit_once = false;
    EMB_ASSERT_OK(emb_work_submit(sys, &items[0].work)); /* a high priority: runs at once */
    EMB_ASSERT_MARKS(7, 17);
}
#endif
