/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Kernel trace points (SPEC-015 catalogue, M1 subset). With CONFIG_EMB_TRACE every
 * kernel event is delivered to one registered sink; the native port's test harness
 * forwards them to the reference-model bridge (SPEC-013 §8). The CTF encoding and
 * the packetized rings of SPEC-015 come with the observability milestone.
 */
#ifndef EMB_TRACE_H
#define EMB_TRACE_H

#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum emb_trace_id {
    EMB_TRACE_NONE = 0,
    EMB_TRACE_SWITCH = 1,         /* a0 = from thread index, a1 = to thread index */
    EMB_TRACE_THREAD_INIT = 2,    /* a0 = index, a1 = priority */
    EMB_TRACE_THREAD_START = 3,   /* a0 = index */
    EMB_TRACE_THREAD_EXIT = 4,    /* a0 = index, a1 = code */
    EMB_TRACE_THREAD_JOIN = 5,    /* a0 = joiner, a1 = target, a2 = result */
    EMB_TRACE_BLOCK = 6,          /* a0 = index, a1 = reason, a2 = queue address */
    EMB_TRACE_WAKE = 7,           /* a0 = index, a1 = result, a2 = data */
    EMB_TRACE_TIMEOUT_ARM = 8,    /* a0 = index, a1 = deadline low word */
    EMB_TRACE_TIMEOUT_EXPIRE = 9, /* a0 = index */
    EMB_TRACE_IRQ_ENTER = 10,     /* a0 = irq number */
    EMB_TRACE_IRQ_EXIT = 11,      /* a0 = irq number, a1 = switched */
    EMB_TRACE_SCHED_LOCK = 12,    /* a0 = depth after */
    EMB_TRACE_SCHED_UNLOCK = 13,  /* a0 = depth after */
    EMB_TRACE_RESCHED_PEND = 14,
    EMB_TRACE_PRIORITY = 15,     /* a0 = index, a1 = base, a2 = effective */
    EMB_TRACE_MUTEX_LOCK = 16,   /* a0 = index, a1 = mutex address, a2 = status */
    EMB_TRACE_MUTEX_UNLOCK = 17, /* a0 = index, a1 = mutex address, a2 = new owner index */
    EMB_TRACE_SEM_TAKE = 18,     /* a0 = index, a1 = sem address, a2 = status */
    EMB_TRACE_SEM_GIVE = 19,     /* a0 = index or 0xFF, a1 = sem address, a2 = count after */
    EMB_TRACE_NOTIFY_SET = 20,   /* a0 = target index, a1 = bits */
    EMB_TRACE_NOTIFY_WAIT = 21,  /* a0 = index, a1 = status, a2 = bits */
    EMB_TRACE_SUSPEND = 22,      /* a0 = index */
    EMB_TRACE_RESUME = 23,       /* a0 = index */
    EMB_TRACE_CANCEL = 24,       /* a0 = index, a1 = delivered now */
    EMB_TRACE_FAULT = 25,        /* a0 = class, a1 = code */
    EMB_TRACE_PI_DEPTH = 26,     /* a0 = index, a1 = depth reached */
    EMB_TRACE_IDLE_ENTER = 27,
    EMB_TRACE_IDLE_EXIT = 28,
    EMB_TRACE_TICK = 29, /* a0 = ticks low word */
    EMB_TRACE_SLEEP = 30 /* a0 = index, a1 = deadline low word */
};

typedef struct emb_trace_event {
    uint16_t id;     /* enum emb_trace_id */
    uint8_t cpu;     /* 0 on uniprocessor */
    uint8_t context; /* emb_context_t */
    uint32_t ticks;  /* kernel clock low word at the event */
    uintptr_t a0;
    uintptr_t a1;
    uintptr_t a2;
} emb_trace_event_t;

typedef void (*emb_trace_sink_t)(const emb_trace_event_t *event, void *ctx);

/**
 * emb_trace_set_sink() - Register the function that receives every trace event.
 * @sink: Runs in the context of the event, often with interrupts masked; must be
 *        ISR-safe and bounded. NULL removes the sink.
 * @ctx:  Passed to @sink.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_TRACE
 * @req      OBS-001
 * @since    0.2
 * @stable   experimental
 */
void emb_trace_set_sink(emb_trace_sink_t sink, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* EMB_TRACE_H */
