/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Software timer objects (SPEC-003 §7). Included by the layout probe, so it names the
 * public handle only as an incomplete type. */
#ifndef EMBK_TIMER_H
#define EMBK_TIMER_H

#include <embk/kernel.h>
#if CONFIG_EMB_WORKQ
#include <embk/work.h>
#endif

#if CONFIG_EMB_TIMER
struct emb_timer_t;

#define EMBK_TIMER_STARTED ((uint8_t)0x02u) /* obj.flags: restart has an interval */

typedef struct embk_timer {
    embk_obj_t obj;      /* obj.flags: EMB_TIMER_ISR_CONTEXT | EMBK_TIMER_STARTED */
    embk_timeout_t node; /* in the timer list while armed (ADR-047) */
    emb_tick_t period;   /* 0: one-shot */
    emb_tick_t interval; /* what restart re-arms with: the first delay or the period */
    uint32_t expiries;   /* saturating */
    uint32_t overruns;   /* saturating: missed periods and coalesced callbacks */
    void (*fn)(struct emb_timer_t timer, void *arg);
    void *arg;
    uint8_t pass;        /* the expiry pass that armed it: due again only in a later one */
    uint8_t in_callback; /* an EMB_TIMER_ISR_CONTEXT callback runs */
#if CONFIG_EMB_WORKQ
    embk_workq_t *queue; /* where the callback runs; NULL with EMB_TIMER_ISR_CONTEXT */
    embk_work_t work;
#endif
#if CONFIG_EMB_OBJ_LIST
    embk_list_node_t all_node; /* in embk_timer_all from init to destroy */
#endif
} embk_timer_t;

#if CONFIG_EMB_OBJ_LIST
extern embk_list_t embk_timer_all; /* every active timer; lock: scheduler domain */
#endif

/* An armed timer's deadline passed (SPEC-003 §7.4); called by the expiry pass with the
 * timeout lock held as *key, which an EMB_TIMER_ISR_CONTEXT callback runs without. */
void embk_timer_expired(embk_timer_t *t, emb_tick_t now, emb_irq_key_t *key);
#endif

#endif /* EMBK_TIMER_H */
