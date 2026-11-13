/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_trace_ring.c
 * @brief   The trace ring: what it keeps, in what order, and what it drops (SPEC-015 §3, A128).
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/sem.h>
#include <emb/trace.h>

#include <emb_test.h>
#include <embs/trace.h>
#include <string.h>

EMB_SEM_DEFINE(probe, 0u, 1000u);

static uint8_t rec[EMB_TRACE_RECORD_SIZE];

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Event fields as a record carries them. */
#define REC_TS(r)   le32(&(r)[0])
#define REC_ID(r)   ((uint32_t)(r)[4] | ((uint32_t)(r)[5] << 8))
#define REC_A(r, k) le32(&(r)[8u + (4u * (k))])

/* The number of the first event from @from with this id and object, or @end. */
static uint32_t find(uint32_t from, uint32_t end, uint32_t id, uint32_t object)
{
    uint32_t s;
    for (s = from; s != end; s++) { /* bounded by end */
        if (embs_trace_read(s, rec) && (REC_ID(rec) == id) && (REC_A(rec, 1u) == object)) {
            return s;
        }
    }
    return end;
}

EMB_TEST(trace_ring_keeps_events_in_order)
{
    uint32_t start;
    uint32_t end;
    uint32_t give;
    uint32_t take;
    uint32_t s;
    uint32_t prev = 0u;
    uint32_t object = (uint32_t)(uintptr_t)probe.raw;
    emb_trace_stats_t st;
    EMB_TEST_REQ("OBS-005");
    EMB_ASSERT_OK(emb_trace_start());
    start = embs_trace_next();
    EMB_ASSERT_OK(emb_sem_give(probe));
    EMB_ASSERT_OK(emb_sem_take(probe, EMB_NO_WAIT));
    end = embs_trace_next();
    EMB_ASSERT_TRUE((end - start) >= 2u);
    give = find(start, end, EMB_TRACE_SEM_GIVE, object);
    take = find(start, end, EMB_TRACE_SEM_TAKE, object);
    EMB_ASSERT_TRUE(give != end);
    EMB_ASSERT_TRUE(take != end);
    EMB_ASSERT_TRUE(give < take);
    EMB_ASSERT_TRUE(embs_trace_read(take, rec));
    EMB_ASSERT_EQ(REC_A(rec, 2u), (uint32_t)EMB_OK); /* the take's status */
    /* times grow in the ring's order (the cycle counter, unwrapped over a short span) */
    for (s = start; s != end; s++) { /* bounded by end */
        EMB_ASSERT_TRUE(embs_trace_read(s, rec));
        if (s != start) {
            EMB_ASSERT_TRUE((int32_t)(REC_TS(rec) - prev) >= 0);
        }
        prev = REC_TS(rec);
    }
    EMB_ASSERT_OK(emb_trace_stats_get(&st));
    EMB_ASSERT_TRUE(st.recorded >= (end - start));
    EMB_ASSERT_STATUS(emb_trace_stats_get(NULL), EMB_EINVAL);
}

EMB_TEST(trace_ring_overwrites_the_oldest)
{
    uint32_t i;
    uint32_t next;
    EMB_TEST_REQ("OBS-005");
    EMB_ASSERT_OK(emb_trace_start());
    for (i = 0u; i < (2u * CONFIG_EMB_TRACE_RING_EVENTS); i++) { /* bounded: twice the ring */
        EMB_ASSERT_OK(emb_sem_give(probe));
    }
    next = embs_trace_next();
    /* the newest CONFIG_EMB_TRACE_RING_EVENTS are kept, and nothing older */
    EMB_ASSERT_TRUE(embs_trace_read(next - 1u, rec));
    EMB_ASSERT_TRUE(embs_trace_read(next - CONFIG_EMB_TRACE_RING_EVENTS, rec));
    EMB_ASSERT_TRUE(!embs_trace_read(next - CONFIG_EMB_TRACE_RING_EVENTS - 1u, rec));
    EMB_ASSERT_TRUE(!embs_trace_read(next, rec));                /* not yet happened */
    for (i = 0u; i < (2u * CONFIG_EMB_TRACE_RING_EVENTS); i++) { /* bounded: the units given */
        EMB_ASSERT_OK(emb_sem_take(probe, EMB_NO_WAIT));
    }
}

EMB_TEST(trace_ring_stops_and_dumps)
{
    uint32_t before;
    EMB_TEST_REQ("OBS-007");
    EMB_ASSERT_OK(emb_trace_start());
    emb_trace_stop();
    before = embs_trace_next();
    EMB_ASSERT_OK(emb_sem_give(probe));
    EMB_ASSERT_OK(emb_sem_take(probe, EMB_NO_WAIT));
    EMB_ASSERT_EQ(embs_trace_next(), before); /* stopped: nothing recorded */
#if CONFIG_EMB_CONSOLE_FRAMED
    EMB_ASSERT_OK(emb_trace_dump());
#else
    EMB_ASSERT_STATUS(emb_trace_dump(), EMB_ENOTSUP); /* a plain console carries no frames */
#endif
    EMB_ASSERT_OK(emb_trace_start());
}
