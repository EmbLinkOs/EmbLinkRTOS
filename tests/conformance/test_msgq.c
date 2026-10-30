/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Message queue conformance (SPEC-007 §2; KRN-IPC-010 to 012, 018, 019). */
#include <emb_test.h>

#define CAP   3u
#define IRQ_Q 2u /* software interrupt slot */

static emb_msgq_storage_t storage;
static EMB_MSGQ_RING(ring, sizeof(uint16_t), CAP);
static emb_msgq_t mq;

static void make(uint16_t capacity, uint8_t flags)
{
    emb_msgq_attr_t attr;
    emb_msgq_attr_default(&attr);
    attr.ring = ring;
    attr.ring_size = sizeof(ring);
    attr.item_size = sizeof(uint16_t);
    attr.capacity = capacity;
    attr.flags = flags;
    EMB_ASSERT_OK(emb_msgq_init(&storage, &attr, &mq));
}

static uint16_t take(emb_timeout_t timeout)
{
    uint16_t v = 0u;
    EMB_ASSERT_OK(emb_msgq_receive(mq, &v, timeout));
    return v;
}

static void put(uint16_t v)
{
    EMB_ASSERT_OK(emb_msgq_send(mq, &v, EMB_NO_WAIT));
}

/* Receives with arg's timeout in ticks (0: forever) and marks id * 100 + value, or id + 500
 * timed out, id + 600 canceled, id + 700 destroyed. arg packs ticks in bits 0 to 7 and the id
 * in bits 8 to 11, which a 16-bit uintptr_t (AVR) holds. */
static void receiver(void *arg)
{
    uintptr_t a = (uintptr_t)arg;
    uint32_t ticks = (uint32_t)(a & 0xFFu);
    int id = (int)((a >> 8) & 0x0Fu);
    uint16_t v = 0u;
    emb_status_t st =
        emb_msgq_receive(mq, &v, ticks == 0u ? EMB_WAIT_FOREVER : EMB_TIMEOUT(EMB_TICKS(ticks)));
    emb_test_mark(st == EMB_OK           ? id * 100 + (int)v
                  : st == EMB_ETIMEDOUT  ? id + 500
                  : st == EMB_ECANCELED  ? id + 600
                  : st == EMB_EDESTROYED ? id + 700
                                         : id + 900);
}

/* Sends the value id * 10 with arg's timeout and marks id + 10 sent, or the codes above. */
static void sender(void *arg)
{
    uintptr_t a = (uintptr_t)arg;
    uint32_t ticks = (uint32_t)(a & 0xFFu);
    int id = (int)((a >> 8) & 0x0Fu);
    uint16_t v = (uint16_t)(id * 10);
    emb_status_t st =
        emb_msgq_send(mq, &v, ticks == 0u ? EMB_WAIT_FOREVER : EMB_TIMEOUT(EMB_TICKS(ticks)));
    emb_test_mark(st == EMB_OK           ? id + 10
                  : st == EMB_ETIMEDOUT  ? id + 500
                  : st == EMB_ECANCELED  ? id + 600
                  : st == EMB_EDESTROYED ? id + 700
                                         : id + 900);
}

#define ARG(ticks, id) ((void *)(uintptr_t)((ticks) | ((id) << 8)))

EMB_TEST(msgq_fifo_order_counts_and_peek)
{
    uint16_t v = 0u;
    uint16_t full = 9u;
    EMB_TEST_REQ("KRN-IPC-010", "KRN-IPC-019");
    make(CAP, 0u);
    EMB_ASSERT_STATUS(emb_msgq_peek(mq, &v), EMB_ETIMEDOUT); /* empty */
    EMB_ASSERT_STATUS(emb_msgq_receive(mq, &v, EMB_NO_WAIT), EMB_ETIMEDOUT);
    put(1u);
    put(2u);
    put(3u);
    EMB_ASSERT_STATUS(emb_msgq_send(mq, &full, EMB_NO_WAIT), EMB_ETIMEDOUT); /* never dropped */
    EMB_ASSERT_EQ(emb_msgq_count(mq), 3u);
    EMB_ASSERT_EQ(emb_msgq_free(mq), 0u);
    EMB_ASSERT_OK(emb_msgq_peek(mq, &v));
    EMB_ASSERT_EQ(v, 1u);
    EMB_ASSERT_EQ(emb_msgq_count(mq), 3u); /* peek removes nothing */
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 1u);
    put(4u); /* wraps around the ring */
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 2u);
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 3u);
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 4u);
    EMB_ASSERT_STATUS(emb_msgq_receive_until(mq, &v, emb_time_now()), EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_msgq_send_until(mq, &full, emb_time_now())); /* a slot is free */
    EMB_ASSERT_EQ(emb_msgq_free(mq), 2u);
    EMB_ASSERT_OK(emb_msgq_destroy(mq)); /* the stored item is dropped */
}

EMB_TEST(msgq_send_front_is_received_first_and_never_waits)
{
    uint16_t v = 7u;
    EMB_TEST_REQ("KRN-IPC-012");
    make(2u, 0u);
    put(1u);
    EMB_ASSERT_OK(emb_msgq_send_front(mq, &v));
    EMB_ASSERT_STATUS(emb_msgq_send_front(mq, &v), EMB_ETIMEDOUT); /* full */
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 7u);
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 1u);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}

EMB_TEST(msgq_waiting_receivers_get_items_directly_in_priority_order)
{
    EMB_TEST_REQ("KRN-IPC-010", "KRN-IPC-005", "KRN-WAIT-001");
    make(CAP, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("r3"), 3u, 0u, receiver, ARG(0u, 3u));
    (void)emb_test_thread(EMB_TEST_NAME("r4"), 4u, 0u, receiver, ARG(0u, 4u));
    put(5u); /* the higher receiver, at once */
    EMB_ASSERT_MARKS(405);
    EMB_ASSERT_EQ(emb_msgq_count(mq), 0u); /* the ring was not used */
    put(6u);
    EMB_ASSERT_MARKS(405, 306);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}

EMB_TEST(msgq_receive_refills_the_freed_slot_from_a_waiting_sender)
{
    EMB_TEST_REQ("KRN-IPC-010", "KRN-IPC-011");
    make(1u, 0u);
    put(1u);
    (void)emb_test_thread(EMB_TEST_NAME("s3"), 3u, 0u, sender, ARG(0u, 3u)); /* waits */
    EMB_ASSERT_EQ(emb_msgq_count(mq), 1u);
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 1u); /* its item moves in, and it wakes */
    EMB_ASSERT_MARKS(13);
    EMB_ASSERT_EQ(emb_msgq_count(mq), 1u);
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 30u);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}

EMB_TEST(msgq_waits_time_out_and_cancel_without_storing)
{
    emb_thread_t h;
    uint32_t t0 = EMB_TEST_NOW();
    EMB_TEST_REQ("KRN-IPC-010", "KRN-WAIT-004", "KRN-WAIT-017");
    make(1u, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("r3"), 3u, 0u, receiver, ARG(5u, 3u));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    EMB_ASSERT_MARKS(503);
    EMB_ASSERT_ELAPSED(t0, 10u);
    put(1u);
    h = emb_test_thread(EMB_TEST_NAME("s4"), 4u, 0u, sender, ARG(0u, 4u));
    EMB_ASSERT_OK(emb_thread_cancel(h));
    EMB_ASSERT_MARKS(503, 604);
    EMB_ASSERT_EQ(emb_msgq_count(mq), 1u); /* the canceled item was never stored */
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 1u);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}

EMB_TEST_ISR(isr_send)
{
    uint16_t v = 8u;
    (void)emb_isr_arg_;
    emb_test_mark(100);
    EMB_ASSERT_OK(emb_msgq_send(mq, &v, EMB_NO_WAIT));
}

EMB_TEST_ISR(isr_receive)
{
    uint16_t v = 0u;
    (void)emb_isr_arg_;
    EMB_ASSERT_OK(emb_msgq_receive(mq, &v, EMB_NO_WAIT));
    emb_test_mark(200 + (int)v);
}

EMB_TEST(msgq_isr_send_and_receive_never_wait)
{
    EMB_TEST_REQ("KRN-IPC-003", "KRN-IPC-010", "KRN-IRQ-010");
    make(1u, 0u);
    emb_test_irq_connect(IRQ_Q, isr_send, NULL);
    (void)emb_test_thread(EMB_TEST_NAME("r3"), 3u, 0u, receiver, ARG(0u, 3u));
    emb_test_irq_raise(IRQ_Q); /* hands the item to the receiver, which runs at ISR exit */
    emb_test_mark(1);
    EMB_ASSERT_MARKS(100, 308, 1);
    put(2u);
    (void)emb_test_thread(EMB_TEST_NAME("s4"), 4u, 0u, sender, ARG(0u, 4u)); /* waits */
    emb_test_irq_connect(IRQ_Q, isr_receive, NULL);
    emb_test_irq_raise(IRQ_Q); /* takes 2 and moves the sender's 40 in */
    EMB_ASSERT_MARKS(100, 308, 1, 202, 14);
    EMB_ASSERT_EQ(take(EMB_NO_WAIT), 40u);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}

EMB_TEST(msgq_destroy_aborts_receivers_and_senders)
{
    static emb_msgq_storage_t other_storage;
    static EMB_MSGQ_RING(other_ring, sizeof(uint16_t), 1u);
    emb_msgq_t full_q;
    emb_msgq_attr_t attr;
    uint16_t v = 1u;
    EMB_TEST_REQ("KRN-OBJ-001", "KRN-WAIT-018");
    make(CAP, EMB_OBJ_ABORT_WAITERS);
    (void)emb_test_thread(EMB_TEST_NAME("r3"), 3u, 0u, receiver, ARG(0u, 3u));
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
    EMB_ASSERT_MARKS(703);
    emb_msgq_attr_default(&attr);
    attr.ring = other_ring;
    attr.ring_size = sizeof(other_ring);
    attr.item_size = sizeof(uint16_t);
    attr.capacity = 1u;
    attr.flags = EMB_OBJ_ABORT_WAITERS;
    EMB_ASSERT_OK(emb_msgq_init(&other_storage, &attr, &full_q));
    EMB_ASSERT_OK(emb_msgq_send(full_q, &v, EMB_NO_WAIT));
    mq = full_q;
    (void)emb_test_thread(EMB_TEST_NAME("s4"), 4u, 0u, sender, ARG(0u, 4u));
    EMB_ASSERT_OK(emb_msgq_destroy(full_q)); /* the stored item is dropped */
    EMB_ASSERT_MARKS(703, 704);
}

EMB_TEST(msgq_destroy_with_waiters_is_misuse)
{
    EMB_TEST_REQ("KRN-OBJ-001", "API-013");
    make(CAP, 0u);
    (void)emb_test_thread(EMB_TEST_NAME("r3"), 3u, 0u, receiver, ARG(0u, 3u));
    EMB_ASSERT_MISUSE(emb_msgq_destroy(mq), EMB_EBUSY);
    put(9u);
    EMB_ASSERT_MARKS(309);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}

EMB_TEST(msgq_init_validates_the_ring)
{
    emb_msgq_attr_t attr;
    emb_msgq_t q;
    EMB_TEST_REQ("KRN-IPC-011", "KRN-IPC-019", "API-015");
    emb_msgq_attr_default(&attr);
    EMB_ASSERT_MISUSE(emb_msgq_init(&storage, &attr, &q), EMB_EINVAL); /* no ring */
    attr.ring = ring;
    attr.ring_size = sizeof(ring);
    attr.item_size = sizeof(uint16_t);
    attr.capacity = CAP + 1u;
    EMB_ASSERT_MISUSE(emb_msgq_init(&storage, &attr, &q), EMB_EINVAL); /* undersized */
    attr.capacity = 0u;
    EMB_ASSERT_MISUSE(emb_msgq_init(&storage, &attr, &q), EMB_EINVAL);
    attr.capacity = 1u;
    attr.item_size = (uint16_t)(CONFIG_EMB_MSGQ_MAX_ITEM + 1);
    attr.ring_size = (size_t)-1;
    EMB_ASSERT_MISUSE(emb_msgq_init(&storage, &attr, &q), EMB_EINVAL); /* item too large */
    attr.item_size = 1u;
    attr.ring = ring + 1; /* misaligned */
    EMB_ASSERT_MISUSE(emb_msgq_init(&storage, &attr, &q), EMB_EINVAL);
    EMB_ASSERT_MISUSE(emb_msgq_init(&storage, NULL, &q), EMB_EINVAL);
}

#if CONFIG_EMB_NOTIFY
EMB_TEST(msgq_binding_fires_on_empty_to_nonempty_only)
{
    emb_notify_bits_t got = 0u;
    EMB_TEST_REQ("KRN-IPC-018", "KRN-NOTIF-004", "KRN-NOTIF-013");
    make(CAP, 0u);
    EMB_ASSERT_OK(emb_msgq_bind_notify(mq, emb_thread_self(), EMB_NOTIFY_BIT(0)));
    EMB_ASSERT_STATUS(emb_msgq_bind_notify(mq, emb_thread_self(), EMB_NOTIFY_BIT(1)), EMB_EEXIST);
    (void)emb_test_thread(EMB_TEST_NAME("r3"), 3u, 0u, receiver, ARG(0u, 3u));
    put(1u); /* handed to the receiver: no signal */
    EMB_ASSERT_MARKS(301);
    EMB_ASSERT_STATUS(emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY, EMB_NO_WAIT, &got),
                      EMB_ETIMEDOUT);
    put(2u); /* empty to nonempty: signaled */
    put(3u); /* already nonempty: not again */
    EMB_ASSERT_OK(
        emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_NO_WAIT, &got));
    EMB_ASSERT_STATUS(emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY, EMB_NO_WAIT, &got),
                      EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_msgq_unbind_notify(mq));
    EMB_ASSERT_STATUS(emb_msgq_unbind_notify(mq), EMB_ENOENT);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}
#endif

#if CONFIG_EMB_OBJ_LIST
EMB_TEST(msgq_storage_reuse_requires_destroy)
{
    emb_msgq_attr_t attr;
    emb_msgq_t other;
    EMB_TEST_REQ("KRN-OBJ-007", "KRN-OBJ-011");
    make(CAP, 0u);
    emb_msgq_attr_default(&attr);
    attr.ring = ring;
    attr.ring_size = sizeof(ring);
    attr.item_size = sizeof(uint16_t);
    attr.capacity = CAP;
    EMB_ASSERT_MISUSE(emb_msgq_init(&storage, &attr, &other), EMB_EBUSY);
    EMB_ASSERT_OK(emb_msgq_destroy(mq));
}
#endif
