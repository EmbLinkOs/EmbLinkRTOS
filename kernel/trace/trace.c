/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Trace sink (SPEC-015, M1 subset). */
#include <emb/trace.h>

#include <embk/kernel.h>

#if CONFIG_EMB_TRACE

static emb_trace_sink_t trace_sink; /* lock: set from prekernel or thread context; read anywhere */
static void *trace_ctx;

void emb_trace_set_sink(emb_trace_sink_t sink, void *ctx)
{
    emb_irq_key_t key = embk_irq_lock();
    trace_sink = sink;
    trace_ctx = ctx;
    embk_irq_unlock(key);
}

void embk_trace_emit(uint16_t id, uintptr_t a0, uintptr_t a1, uintptr_t a2)
{
    emb_trace_event_t ev;
    emb_trace_sink_t sink = trace_sink;
    if (sink == NULL) {
        return;
    }
    ev.id = id;
    ev.cpu = 0u;
    ev.context = emb_context();
    ev.ticks = (uint32_t)emb_time_now().ticks;
    ev.a0 = a0;
    ev.a1 = a1;
    ev.a2 = a2;
    sink(&ev, trace_ctx);
}

#else

void emb_trace_set_sink(emb_trace_sink_t sink, void *ctx)
{
    (void)sink;
    (void)ctx;
}

#endif
