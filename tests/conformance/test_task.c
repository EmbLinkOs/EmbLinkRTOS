/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_task.c
 * @brief   Tasks: threads written as state machines (SPEC-020, KRN-TSK-001 to 004).
 *
 * @details
 * The tasks here are written by hand as the generator writes them: a description, an event
 * buffer, a queue and a dispatch function calling the handlers by name. A door (event model):
 * CLOSED, OPEN with a timeout, JAMMED; a logger with an event type of its own; a periodic tick.
 * Marks: 100 + state at an entry, 200 + state at an exit, 300 + the event's id at a handler.
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>

#define DOOR_CLOSED 0u
#define DOOR_OPEN   1u
#define DOOR_JAMMED 2u
#define EV_OPEN     (EMB_EV_USER + 0u)
#define EV_CLOSE    (EMB_EV_USER + 1u)
#define EV_JAM      (EMB_EV_USER + 2u)
#define CAP         4u
#define IRQ_T       2u /* software interrupt slot */
#define TASK_PRIO   3u

/* ---- the door: what the generator writes ---------------------------------------------- */

static emb_task_t door;
static emb_task_event_t door_event;
static emb_msgq_storage_t door_q_storage;
static EMB_MSGQ_RING(door_ring, sizeof(emb_task_event_t), CAP);
static emb_msgq_t door_q;
static const uint32_t door_timeouts[3] = {0u, 30u, 0u}; /* OPEN closes itself after 30 ms */

static emb_state_t door_dispatch(emb_state_t state, uint8_t why)
{
    emb_state_t next = state;
    if (why == EMB_TASK_WHY_TIMEOUT) {
        door_event.id = EMB_EV_TIMEOUT;
        door_event.value = 0u;
    }
    emb_test_mark(300 + (int)door_event.id);
    if (state == DOOR_CLOSED) {
        next = (door_event.id == EV_OPEN) ? DOOR_OPEN : DOOR_CLOSED;
    } else if (state == DOOR_OPEN) {
        if ((door_event.id == EV_CLOSE) || (door_event.id == EMB_EV_TIMEOUT)) {
            next = DOOR_CLOSED;
        } else if (door_event.id == EV_JAM) {
            next = DOOR_JAMMED;
        } else {
            next = DOOR_OPEN;
        }
    } else {
        next = (door_event.id == EV_CLOSE) ? DOOR_CLOSED : DOOR_JAMMED;
    }
    return next;
}

static void door_enter(emb_state_t state)
{
    emb_test_mark(100 + (int)state);
}

static void door_leave(emb_state_t state)
{
    if (state == DOOR_OPEN) {
        emb_test_mark(200 + (int)state);
    }
}

static const emb_task_desc_t door_desc = {"door",
                                          door_dispatch,
                                          door_enter,
                                          door_leave,
                                          &door_event,
                                          &door_q,
                                          door_timeouts,
                                          0u,
                                          (uint16_t)sizeof(emb_task_event_t),
                                          DOOR_CLOSED,
                                          3u,
                                          EMB_TASK_EVENT,
                                          0u};

static void door_main(void *arg)
{
    (void)arg;
    emb_task_run(&door);
}

static emb_thread_t door_thread;

/* A task ends when its thread is canceled (SPEC-008 §7): the framework then reclaims it. */
static void stop(emb_thread_t h)
{
    unsigned i;
    uint8_t state = 0u;
    EMB_ASSERT_OK(emb_thread_cancel(h));
    for (i = 0u; i < 20u; i++) { /* bounded: 200 ms */
        (void)emb_thread_state(h, &state, NULL);
        if (state == EMB_THREAD_TERMINATED) {
            break;
        }
        (void)emb_thread_sleep(EMB_MS(10u));
    }
    EMB_ASSERT_EQ(state, EMB_THREAD_TERMINATED);
}

/* A fresh door, its thread at TASK_PRIO: it runs at once, ahead of the runner. */
static void door_start(void)
{
    emb_msgq_attr_t attr;
    emb_msgq_attr_default(&attr);
    attr.ring = door_ring;
    attr.ring_size = sizeof(door_ring);
    attr.item_size = sizeof(emb_task_event_t);
    attr.capacity = CAP;
    EMB_ASSERT_OK(emb_msgq_init(&door_q_storage, &attr, &door_q));
    door.desc = &door_desc;
    door.stats = (emb_task_stats_t){0u, 0u, 0u, 0u, 0u};
    emb_test_marks_clear();
    door_thread = emb_test_thread(EMB_TEST_NAME("door"), TASK_PRIO, 0u, door_main, NULL);
}

/* ---- the tests ------------------------------------------------------------------------- */

EMB_TEST(task_starts_with_its_initial_entry)
{
    EMB_TEST_REQ("KRN-TSK-002");
    door_start();
    EMB_ASSERT_MARKS(100);
    EMB_ASSERT_EQ(emb_task_state(&door), DOOR_CLOSED);
    stop(door_thread);
    EMB_ASSERT_OK(emb_msgq_destroy(door_q));
}

EMB_TEST(task_transitions_exit_then_entry)
{
    EMB_TEST_REQ("KRN-TSK-002");
    door_start();
    EMB_ASSERT_OK(emb_task_post(&door, EV_OPEN, 0u));
    EMB_ASSERT_OK(emb_task_post(&door, EV_CLOSE, 0u));
    /* the open: handler, entry to OPEN; the close: handler, exit from OPEN, entry to CLOSED */
    EMB_ASSERT_MARKS(100, 316, 101, 317, 201, 100);
    EMB_ASSERT_EQ(emb_task_state(&door), DOOR_CLOSED);
    stop(door_thread);
    EMB_ASSERT_OK(emb_msgq_destroy(door_q));
}

EMB_TEST(task_state_timeout)
{
    emb_task_stats_t st;
    EMB_TEST_REQ("KRN-TSK-002");
    door_start();
    EMB_ASSERT_OK(emb_task_post(&door, EV_OPEN, 0u));
    EMB_ASSERT_EQ(emb_task_state(&door), DOOR_OPEN);
    (void)emb_thread_sleep(EMB_MS(80u)); /* OPEN's 30 ms pass with no event */
    EMB_ASSERT_MARKS(100, 316, 101, 300 + (int)EMB_EV_TIMEOUT, 201, 100);
    EMB_ASSERT_EQ(emb_task_state(&door), DOOR_CLOSED);
    EMB_ASSERT_OK(emb_task_stats_get(&door, &st));
    EMB_ASSERT_EQ(st.timeouts, 1u);
    EMB_ASSERT_EQ(st.transitions, 2u);
    stop(door_thread);
    EMB_ASSERT_OK(emb_msgq_destroy(door_q));
}

/* Posts three events at a higher priority than the task's: the task runs once it leaves. */
static void poster(void *arg)
{
    (void)arg;
    (void)emb_task_post(&door, EV_OPEN, 0u);
    (void)emb_task_post(&door, EV_JAM, 0u);
    (void)emb_task_post(&door, EV_CLOSE, 0u);
}

EMB_TEST(task_handles_every_event_in_order)
{
    EMB_TEST_REQ("KRN-TSK-001");
    door_start();
    (void)emb_test_thread(EMB_TEST_NAME("poster"), TASK_PRIO + 2u, 0u, poster, NULL);
    EMB_ASSERT_MARKS(100, 316, 101, 318, 201, 102, 317, 100);
    stop(door_thread);
    EMB_ASSERT_OK(emb_msgq_destroy(door_q));
}

static emb_status_t flood_status[CAP + 2u];

/* Posts more events than the queue holds, before the task can take any. */
static void flooder(void *arg)
{
    unsigned i;
    (void)arg;
    for (i = 0u; i < (CAP + 2u); i++) {
        flood_status[i] = emb_task_post(&door, EV_CLOSE, i);
    }
}

EMB_TEST(task_full_queue_is_refused_and_counted)
{
    emb_task_stats_t st;
    unsigned i;
    EMB_TEST_REQ("KRN-TSK-003");
    door_start();
    (void)emb_test_thread(EMB_TEST_NAME("flood"), TASK_PRIO + 2u, 0u, flooder, NULL);
    /* the first goes straight to the waiting task, CAP more fill the queue, the last is refused */
    for (i = 0u; i <= CAP; i++) {
        EMB_ASSERT_OK(flood_status[i]);
    }
    EMB_ASSERT_STATUS(flood_status[CAP + 1u], EMB_ETIMEDOUT);
    EMB_ASSERT_OK(emb_task_stats_get(&door, &st));
    EMB_ASSERT_EQ(st.drops, 1u);
    EMB_ASSERT_EQ(st.events, CAP + 1u);
    stop(door_thread);
    EMB_ASSERT_OK(emb_msgq_destroy(door_q));
}

EMB_TEST_ISR(isr_post)
{
    (void)emb_isr_arg_;
    (void)emb_task_post(&door, EV_OPEN, 7u);
}

EMB_TEST(task_post_from_an_interrupt)
{
    EMB_TEST_REQ("KRN-TSK-003");
    door_start();
    emb_test_irq_connect(IRQ_T, isr_post, NULL);
    emb_test_irq_raise(IRQ_T); /* the task runs at the interrupt's exit */
    emb_test_irq_settle();
    EMB_ASSERT_MARKS(100, 316, 101);
    EMB_ASSERT_EQ(door_event.value, 7u);
    stop(door_thread);
    EMB_ASSERT_OK(emb_msgq_destroy(door_q));
}

/* ---- a task with an event type of its own --------------------------------------------- */

typedef struct log_rec {
    uint32_t id;
    uint32_t a;
    uint32_t b;
} log_rec_t;

static emb_task_t logger;
static log_rec_t logger_event;
static emb_msgq_storage_t logger_q_storage;
static EMB_MSGQ_RING(logger_ring, sizeof(log_rec_t), CAP);
static emb_msgq_t logger_q;

static emb_state_t logger_dispatch(emb_state_t state, uint8_t why)
{
    (void)why;
    emb_test_mark((int)(logger_event.a + logger_event.b));
    return state;
}

static const emb_task_desc_t logger_desc = {"logger",
                                            logger_dispatch,
                                            NULL,
                                            NULL,
                                            &logger_event,
                                            &logger_q,
                                            NULL,
                                            0u,
                                            (uint16_t)sizeof(log_rec_t),
                                            0u,
                                            1u,
                                            EMB_TASK_EVENT,
                                            0u};

static void logger_main(void *arg)
{
    (void)arg;
    emb_task_run(&logger);
}

EMB_TEST(task_event_of_its_own_type)
{
    emb_msgq_attr_t attr;
    log_rec_t rec = {EMB_EV_USER, 40u, 2u};
    emb_thread_t h;
    EMB_TEST_REQ("KRN-TSK-001");
    emb_msgq_attr_default(&attr);
    attr.ring = logger_ring;
    attr.ring_size = sizeof(logger_ring);
    attr.item_size = sizeof(log_rec_t);
    attr.capacity = CAP;
    EMB_ASSERT_OK(emb_msgq_init(&logger_q_storage, &attr, &logger_q));
    logger.desc = &logger_desc;
    emb_test_marks_clear();
    h = emb_test_thread(EMB_TEST_NAME("logger"), TASK_PRIO, 0u, logger_main, NULL);
    EMB_ASSERT_STATUS(emb_task_post(&logger, EMB_EV_USER, 1u),
                      EMB_EINVAL); /* not an emb_task_event_t */
    EMB_ASSERT_OK(emb_task_post_event(&logger, &rec));
    EMB_ASSERT_MARKS(42);
    stop(h);
    EMB_ASSERT_OK(emb_msgq_destroy(logger_q));
}

/* ---- a periodic task ------------------------------------------------------------------- */

#define PERIOD_MS 50u
static emb_task_t tick;
static emb_instant_t tick_at[4];
static unsigned tick_n;

static emb_state_t tick_dispatch(emb_state_t state, uint8_t why)
{
    if ((why == EMB_TASK_WHY_PERIOD) && (tick_n < 4u)) {
        tick_at[tick_n] = emb_time_now();
        tick_n++;
        if (tick_n == 2u) {
            (void)emb_thread_sleep(EMB_MS(120u)); /* overruns the next two periods */
        }
    }
    return state;
}

static const emb_task_desc_t tick_desc = {
    "tick", tick_dispatch,     NULL, NULL, NULL, NULL, NULL, PERIOD_MS, 0u, 0u,
    1u,     EMB_TASK_PERIODIC, 0u};

static void tick_main(void *arg)
{
    (void)arg;
    emb_task_run(&tick);
}

EMB_TEST(task_periodic_schedule_and_overruns)
{
    emb_task_stats_t st;
    emb_duration_t d;
    EMB_TEST_REQ("KRN-TSK-004");
    tick.desc = &tick_desc;
    tick_n = 0u;
    emb_thread_t h = emb_test_thread(EMB_TEST_NAME("tick"), TASK_PRIO, 0u, tick_main, NULL);
    (void)emb_thread_sleep(EMB_MS(300u));
    EMB_ASSERT_EQ(tick_n, 4u);
    EMB_ASSERT_OK(emb_task_stats_get(&tick, &st));
    EMB_ASSERT_EQ(st.overruns, 2u); /* the periods at 100 and 150 ms, skipped */
    /* the third period is the schedule's fifth start, 200 ms after the first; the fourth 250 */
    d = emb_instant_sub(tick_at[2], tick_at[0]);
    EMB_ASSERT_TRUE(d.ticks == EMB_MS(4u * PERIOD_MS).ticks);
    d = emb_instant_sub(tick_at[3], tick_at[2]);
    EMB_ASSERT_TRUE(d.ticks == EMB_MS(PERIOD_MS).ticks);
    stop(h);
}

/* ---- misuse ---------------------------------------------------------------------------- */

static const emb_task_desc_t bad_desc = {"bad",
                                         door_dispatch,
                                         NULL,
                                         NULL,
                                         &door_event,
                                         &door_q,
                                         NULL,
                                         0u,
                                         (uint16_t)sizeof(emb_task_event_t),
                                         5u,
                                         3u,
                                         EMB_TASK_EVENT,
                                         0u}; /* its initial state is not one of its three */
static emb_task_t bad;

EMB_TEST(task_misuse_faults)
{
    EMB_TEST_REQ("KRN-TSK-002");
    bad.desc = &bad_desc;
    EMB_ASSERT_FAULTS(emb_task_run(&bad));
    EMB_ASSERT_FAULTS((void)emb_task_stats_get(&door, NULL));
    EMB_ASSERT_FAULTS((void)emb_task_state(NULL));
}
