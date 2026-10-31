/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Work queue objects (SPEC-006 §4). Included by the layout probe, so it names the public
 * work item only as an incomplete type. */
#ifndef EMBK_WORK_H
#define EMBK_WORK_H

#include <embk/kernel.h>

#if CONFIG_EMB_WORKQ
struct emb_work;
struct embk_workq;

#define EMBK_WORK_TAG         ((uint8_t)0x5Au) /* set by emb_work_init() */
#define EMBK_WORK_RESUBMITTED ((uint8_t)0x01u) /* submitted while running: queue it again */
#define EMBK_WORK_MARKER      ((uint8_t)0x02u) /* a flush marker on the flusher's stack */

typedef struct embk_work {
    embk_list_node_t node; /* in the queue's FIFO while PENDING */
    void (*fn)(struct emb_work *work);
    struct embk_workq *queue; /* the queue it is pending or running on, else NULL */
    uint8_t tag;              /* EMBK_WORK_TAG once initialized */
    uint8_t state;            /* EMB_WORK_IDLE | EMB_WORK_PENDING | EMB_WORK_RUNNING */
    uint8_t flags;            /* EMBK_WORK_RESUBMITTED | EMBK_WORK_MARKER */
} embk_work_t;

typedef struct embk_workq {
    embk_obj_t obj;
    embk_thread_t thread;     /* the queue's kernel thread */
    embk_list_t fifo;         /* pending items and flush markers, in submission order */
    embk_wait_queue_t done_q; /* cancel_sync and flush; wake_data holds the awaited item */
    embk_work_t *running;     /* the item whose handler runs, or NULL */
    uint8_t stopping;         /* destroy has begun: the thread exits at an empty FIFO */
    uint8_t timer_refs;       /* timers whose callbacks run here: destroy is refused */
#if CONFIG_EMB_OBJ_LIST
    embk_list_node_t all_node; /* in embk_workq_all from init to destroy */
#endif
} embk_workq_t;

#if CONFIG_EMB_OBJ_LIST
extern embk_list_t embk_workq_all; /* every active work queue; lock: scheduler domain */
#endif

/* The queue a handle's raw value names, or NULL. */
embk_workq_t *embk_workq_from_raw(uintptr_t raw);
/* Initialize @w to run @fn (emb_work_init()). */
void embk_work_init(embk_work_t *w, void (*fn)(struct emb_work *work));
/* emb_work_submit() on resolved objects, lock held; the caller reschedules. */
emb_status_t embk_work_submit_locked(embk_workq_t *q, embk_work_t *w);
/* emb_work_cancel() on a resolved item, lock held; returns the state it was in. */
uint8_t embk_work_cancel_locked(embk_work_t *w);
/* emb_work_cancel_sync() on a resolved item, with faults and traces naming @api_id and the
 * deadline fixed by the caller; thread context. */
emb_status_t embk_work_cancel_sync(uint16_t api_id, embk_work_t *w, emb_tick_t deadline,
                                   bool nowait);
#endif

#endif /* EMBK_WORK_H */
