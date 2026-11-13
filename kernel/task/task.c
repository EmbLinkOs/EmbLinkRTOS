/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    task.c
 * @brief   The task runtime (SPEC-020, KRN-TSK): a thread's state machine, by events or by period.
 *
 * @details
 * A task's generated thread calls emb_task_run(), which never returns. The event model waits on
 * the task's queue for at most the current state's timeout; the periodic model waits for the
 * next period on a schedule of whole periods from its start, so a handler's time does not make
 * it drift, and a period a handler overran is counted and skipped rather than run late. Either
 * way the generated dispatch function runs the state's handler, and a move to another state runs
 * the old state's exit handler, then the new state's entry handler (KRN-TSK-002). The counters
 * are written under the critical section, since posts from interrupts count drops.
 *
 * @see     include/emb/task.h
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/task.h>
#include <emb/thread.h>

#include <embk/kernel.h>

#include <stdbool.h>

static void task_count(uint32_t *counter)
{
    emb_irq_key_t key = embk_irq_lock();
    (*counter)++;
    embk_irq_unlock(key);
}

/* One step: the state's handler, then, on a move, exit and entry (KRN-TSK-002). */
static void task_step(emb_task_t *task, uint8_t why)
{
    const emb_task_desc_t *d = task->desc;
    emb_state_t from = task->state;
    emb_state_t to = d->dispatch(from, why);
    if (to >= d->count) { /* a handler returned a state the task does not have */
#if CONFIG_EMB_CHECKED
        embk_fault_raise_api(EMB_API_ID_TASK_RUN, EMB_FAULT_API_ARGUMENT, 2u, 1u, EMBK_WHERE);
#else
        to = from; /* a release build: the task stays where it is */
#endif
    }
    if (to != from) {
        if (d->leave != NULL) {
            d->leave(from);
        }
        task->state = to;
        if (d->enter != NULL) {
            d->enter(to);
        }
        task_count(&task->stats.transitions);
    }
}

/* Its thread canceled (SPEC-008 §7): the current state's exit handler, then the task ends. */
static void task_end(emb_task_t *task)
{
    if (task->desc->leave != NULL) {
        task->desc->leave(task->state);
    }
}

/* The periodic model: whole periods from the start (KRN-TSK-004). */
static void task_periodic(emb_task_t *task)
{
    emb_duration_t period = EMB_MS(task->desc->period_ms);
    emb_instant_t next = emb_time_now();
    emb_status_t st = EMB_OK;
    while (st != EMB_ECANCELED) { /* bounded: until its thread is canceled, one period at a time */
        task_step(task, EMB_TASK_WHY_PERIOD);
        next = emb_instant_add(next, period);
        while (!emb_instant_before(emb_time_now(), next)) { /* bounded: the periods overrun */
            task_count(&task->stats.overruns);
            next = emb_instant_add(next, period);
        }
        st = emb_thread_sleep_until(next);
    }
    task_end(task);
}

/* Whether the generated description can run. A thread's body cannot return a status: a checked
 * build faults on one that cannot, a release build returns, as other misuse does. */
static bool task_desc_ok(const emb_task_t *task)
{
    const emb_task_desc_t *d = (task != NULL) ? task->desc : NULL;
    bool ok = (d != NULL) && (d->dispatch != NULL) && (d->initial < d->count);
    if (ok) {
        ok = (d->model == EMB_TASK_PERIODIC) ? (d->period_ms != 0u)
                                             : ((d->queue != NULL) && (d->event != NULL));
    }
    return ok;
}

void emb_task_run(emb_task_t *task)
{
    const emb_task_desc_t *d;
    if (!task_desc_ok(task)) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise_api(EMB_API_ID_TASK_RUN, EMB_FAULT_API_ARGUMENT, 1u, 1u, EMBK_WHERE);
#else
        return; /* a release build: misuse returns, as a status would (the thread ends) */
#endif
    }
    d = task->desc;
    task->state = d->initial;
    if (d->enter != NULL) {
        d->enter(d->initial);
    }
    if (d->model == EMB_TASK_PERIODIC) {
        task_periodic(task);
        return;
    }
    for (;;) { /* bounded: until its thread is canceled, one event at a time */
        uint32_t ms = (d->timeout_ms != NULL) ? d->timeout_ms[task->state] : 0u;
        emb_timeout_t wait = (ms != 0u) ? EMB_TIMEOUT(EMB_MS(ms)) : EMB_WAIT_FOREVER;
        emb_status_t st = emb_msgq_receive(*d->queue, d->event, wait);
        if (st == EMB_OK) {
            task_count(&task->stats.events);
            task_step(task, EMB_TASK_WHY_EVENT);
        } else if (st == EMB_ETIMEDOUT) {
            task_count(&task->stats.timeouts);
            task_step(task, EMB_TASK_WHY_TIMEOUT);
        } else if (st == EMB_ECANCELED) {
            break;
        } else {
            /* the queue's handle is the generator's: anything else is a fault already raised */
        }
    }
    task_end(task);
}

static emb_status_t task_send(emb_task_t *task, const void *item, emb_timeout_t timeout,
                              uint16_t api)
{
    emb_status_t st;
    EMBK_API_REQUIRE(api, (task != NULL) && (task->desc != NULL) && (item != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if ((task->desc->model != EMB_TASK_EVENT) || (task->desc->queue == NULL)) {
        return EMB_EINVAL;
    }
    st = emb_msgq_send(*task->desc->queue, item, timeout);
    if (st == EMB_ETIMEDOUT) {
        task_count(&task->stats.drops); /* KRN-TSK-003: refused, and counted */
    }
    return st;
}

emb_status_t emb_task_post(emb_task_t *task, uint32_t id, uint32_t value)
{
    emb_task_event_t ev;
    ev.id = id;
    ev.value = value;
    if ((task != NULL) && (task->desc != NULL) &&
        (task->desc->event_size != (uint16_t)sizeof(emb_task_event_t))) {
        return EMB_EINVAL; /* the task has an event type of its own: emb_task_post_event() */
    }
    return task_send(task, &ev, EMB_NO_WAIT, EMB_API_ID_TASK_POST);
}

emb_status_t emb_task_post_wait(emb_task_t *task, uint32_t id, uint32_t value,
                                emb_timeout_t timeout)
{
    emb_task_event_t ev;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_TASK_POST_WAIT);
    ev.id = id;
    ev.value = value;
    if ((task != NULL) && (task->desc != NULL) &&
        (task->desc->event_size != (uint16_t)sizeof(emb_task_event_t))) {
        return EMB_EINVAL;
    }
    return task_send(task, &ev, timeout, EMB_API_ID_TASK_POST_WAIT);
}

emb_status_t emb_task_post_event(emb_task_t *task, const void *event)
{
    return task_send(task, event, EMB_NO_WAIT, EMB_API_ID_TASK_POST_EVENT);
}

emb_state_t emb_task_state(const emb_task_t *task)
{
    if (task == NULL) {
#if CONFIG_EMB_CHECKED
        embk_fault_raise_api(EMB_API_ID_TASK_STATE, EMB_FAULT_API_ARGUMENT, 1u, 1u, EMBK_WHERE);
#else
        return 0u;
#endif
    }
    return task->state;
}

emb_status_t emb_task_stats_get(const emb_task_t *task, emb_task_stats_t *out)
{
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_TASK_STATS_GET, (task != NULL) && (out != NULL),
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    key = embk_irq_lock();
    *out = task->stats;
    embk_irq_unlock(key);
    return EMB_OK;
}
