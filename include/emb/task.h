/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    task.h
 * @brief   Tasks: threads whose body is a state machine (SPEC-020, KRN-TSK).
 *
 * @details
 * A task's states, events and handlers are declared in the system description (SPEC-014 §5.3);
 * the generator writes its tables, its event queue, its thread and a dispatch function that calls
 * the application's handlers by name, so nothing here calls through a converted function pointer.
 * The runtime below runs that machine: the event model waits on the task's queue, the periodic
 * model on a drift-free schedule; a handler returns the next state, and a move runs the old
 * state's exit handler, then the new state's entry handler.
 *
 * @see     kernel/task/task.c
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#ifndef EMB_TASK_H
#define EMB_TASK_H

#include <emb/msgq.h>
#include <emb/status.h>
#include <emb/time.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The runtime's event ids, below EMB_EV_USER; a task's own events are numbered from it. */
#define EMB_EV_TIMEOUT 3u  /* the current state's timeout passed with no event */
#define EMB_EV_USER    16u /* the first id of a task's own events */

/* A task's model. */
#define EMB_TASK_EVENT    0u /* waits on its queue: a handler per event */
#define EMB_TASK_PERIODIC 1u /* a handler per period */

/* Why the dispatch function runs the current state's handler. */
#define EMB_TASK_WHY_EVENT 0u /* an event is in the task's event buffer */
#define EMB_TASK_WHY_TIMEOUT \
    1u /* the state's timeout passed: the buffer is to hold EMB_EV_TIMEOUT */
#define EMB_TASK_WHY_PERIOD 2u /* a period began */

typedef uint16_t emb_state_t;

/* An event: an id (EMB_EV_* or the task's own) and a datum, copied into the task's queue. A task
 * may have an event type of its own, whose first member is its uint32_t id. */
typedef struct emb_task_event {
    uint32_t id;
    uint32_t value;
} emb_task_event_t;

typedef struct emb_task_stats {
    uint32_t events;      /* handled */
    uint32_t drops;       /* posts refused by a full queue (KRN-TSK-003) */
    uint32_t timeouts;    /* states' timeouts that passed */
    uint32_t overruns;    /* periods a handler overran (KRN-TSK-004) */
    uint32_t transitions; /* moves to another state */
} emb_task_stats_t;

/* What the generator writes for a task: constant. */
typedef struct emb_task_desc {
    const char *name;
    /* the current state's handler, on the event in the buffer or the period; the next state */
    emb_state_t (*dispatch)(emb_state_t state, uint8_t why);
    void (*enter)(emb_state_t state); /* entry handlers, or NULL where no state has one */
    void (*leave)(emb_state_t state); /* exit handlers, or NULL */
    void *event;                /* the event buffer: an emb_task_event_t or the task's own type */
    const emb_msgq_t *queue;    /* its queue's handle (event model), or NULL */
    const uint32_t *timeout_ms; /* each state's timeout, 0 for none; NULL when none has one */
    uint32_t period_ms;         /* periodic model */
    uint16_t event_size;        /* sizeof the event type */
    emb_state_t initial;
    emb_state_t count; /* states */
    uint8_t model;     /* EMB_TASK_EVENT or EMB_TASK_PERIODIC */
    uint8_t reserved_;
} emb_task_desc_t;

/* A task's own state, first in the generated <task>_t. */
typedef struct emb_task {
    const emb_task_desc_t *desc;
    emb_task_stats_t stats; /* lock domain: the critical section */
    emb_state_t state;
    uint8_t reserved_[2];
} emb_task_t;

/**
 * emb_task_run() - Run a task's state machine: the body of the thread the generator writes.
 * @task: The task.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     unbounded until its thread is canceled; each step O(1) plus an item copy and
 *           its handlers'
 * @owns     none
 * @config   CONFIG_EMB_TASK
 * @req      KRN-TSK-001 KRN-TSK-002 KRN-TSK-004
 * @since    0.3
 * @stable   no
 *
 * Runs the initial state's entry handler, then, in the event model, waits on the task's queue
 * for at most the current state's timeout and runs its handler on each event or timeout; in
 * the periodic model, runs it at the start of each period, counting a period a handler overran.
 * A handler returning another state moves the task: the old state's exit handler, then the new
 * state's entry handler. A handler returning a state the task does not have faults in a checked
 * build; in a release build the task stays in its state. Canceling
 * the task's thread (emb_thread_cancel(), SPEC-008 §7) ends it: the current state's exit handler
 * runs and this returns, so the thread's entry returns too.
 *
 * Return: only once its thread is canceled.
 */
void emb_task_run(emb_task_t *task);

/**
 * emb_task_post() - Post an event to a task, without waiting.
 * @task:  An event-model task whose events are emb_task_event_t.
 * @id:    The event's id.
 * @value: Its datum.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus an item copy
 * @owns     none
 * @config   CONFIG_EMB_TASK
 * @req      KRN-TSK-003
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ETIMEDOUT when the queue is full, the drop counted; EMB_EINVAL for a
 * periodic task or one with its own event type.
 */
emb_status_t emb_task_post(emb_task_t *task, uint32_t id, uint32_t value);

/**
 * emb_task_post_wait() - Post an event to a task, waiting for room in its queue.
 * @task:    An event-model task whose events are emb_task_event_t.
 * @id:      The event's id.
 * @value:   Its datum.
 * @timeout: How long to wait for room.
 *
 * @ctx      thread
 * @blocks   yes
 * @time     O(1) plus an item copy; O(waiters + timeouts) finite wait enqueue
 * @owns     none
 * @config   CONFIG_EMB_TASK
 * @req      KRN-TSK-003
 * @since    0.3
 * @stable   no
 *
 * Return: as emb_task_post(); EMB_ETIMEDOUT when the queue stayed full, the drop counted.
 */
emb_status_t emb_task_post_wait(emb_task_t *task, uint32_t id, uint32_t value,
                                emb_timeout_t timeout);

/**
 * emb_task_post_event() - Post an event of the task's own type, without waiting.
 * @task:  An event-model task.
 * @event: The event, the task's event type (its first member the id); copied.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1) plus an item copy
 * @owns     none
 * @config   CONFIG_EMB_TASK
 * @req      KRN-TSK-003
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ETIMEDOUT when the queue is full, the drop counted; EMB_EINVAL for a
 * periodic task.
 */
emb_status_t emb_task_post_event(emb_task_t *task, const void *event);

/**
 * emb_task_state() - A task's current state.
 * @task: The task.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TASK
 * @req      KRN-TSK-002
 * @since    0.3
 * @stable   no
 *
 * Return: the state, as its generated constant.
 */
emb_state_t emb_task_state(const emb_task_t *task);

/**
 * emb_task_stats_get() - A task's counters.
 * @task: The task.
 * @out:  Receives them, read together.
 *
 * @ctx      thread isr
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TASK
 * @req      KRN-TSK-003 KRN-TSK-004
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK.
 */
emb_status_t emb_task_stats_get(const emb_task_t *task, emb_task_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EMB_TASK_H */
