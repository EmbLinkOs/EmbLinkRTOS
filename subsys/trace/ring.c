/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    ring.c
 * @brief   The trace ring: the kernel's events in RAM, dumped or streamed on channel 1 (A128).
 *
 * @details
 * The ring is the trace sink: each event is written as a 20-byte record in the critical section
 * it happens in, with the cycle counter's time. The ring keeps the newest events; a full ring
 * overwrites the oldest, and a reader that finds its next event overwritten counts the loss.
 * Events are numbered from emb_trace_start(): event n is in slot n modulo the ring's size.
 *
 * A packet on channel 1 is a 16-byte header (version, kind, count, the first event's number,
 * the cycle counter's rate, the events lost so far) and its events or the threads' names. The
 * host (tools/trace) builds CTF's packets and Perfetto's tracks from them: the target formats
 * nothing (08 Q12).
 *
 * @see     include/emb/trace.h
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/config.h>
#include <emb/trace.h>

#if CONFIG_EMB_TRACE_RING

#include <emb/arch.h>
#include <emb/console.h>
#include <emb/context.h>
#include <emb/frame.h>
#include <emb/irq.h>
#include <emb/kernel.h>
#include <emb/sched.h>
#include <emb/storage.h>
#include <emb/thread.h>
#include <emb/time.h>

#include <stdbool.h>

#include <embs/trace.h>

#define RING_EVENTS ((uint32_t)CONFIG_EMB_TRACE_RING_EVENTS)
#define RING_MASK   (RING_EVENTS - 1u)
EMB_STATIC_ASSERT((RING_EVENTS & RING_MASK) == 0u,
                  "CONFIG_EMB_TRACE_RING_EVENTS is a power of two");

#define PACKET_HEADER     16u
#define PACKET_EVENTS     12u /* a frame's payload of 256 bytes at most */
#define PACKET_VERSION    1u
#define NAME_MAX          15u
#define NAMES_PAYLOAD_MAX 240u

typedef struct trace_rec {
    uint32_t ts;
    uint16_t id;
    uint8_t cpu;
    uint8_t context;
    uint32_t a[3];
} trace_rec_t;

static trace_rec_t trace_ring[RING_EVENTS];
/* Lock domain: the critical section (emb_irq_lock), for every variable below. */
static uint32_t ring_next; /* the next event's number: events [next - size, next) are kept */
static bool recording;
static emb_trace_stats_t trace_stats;

static void ring_sink(const emb_trace_event_t *ev, void *ctx)
{
    emb_irq_key_t key;
    (void)ctx;
    key = emb_irq_lock();
    if (recording) {
        trace_rec_t *r = &trace_ring[ring_next & RING_MASK];
        r->ts = (uint32_t)emb_arch_cycles();
        r->id = ev->id;
        r->cpu = ev->cpu;
        r->context = ev->context;
        r->a[0] = (uint32_t)ev->a0;
        r->a[1] = (uint32_t)ev->a1;
        r->a[2] = (uint32_t)ev->a2;
        ring_next++;
        trace_stats.recorded++;
    }
    emb_irq_unlock(key);
}

emb_status_t emb_trace_start(void)
{
    emb_irq_key_t key = emb_irq_lock();
    recording = true;
    emb_irq_unlock(key);
    emb_trace_set_sink(ring_sink, NULL);
    return EMB_OK;
}

void emb_trace_stop(void)
{
    emb_irq_key_t key = emb_irq_lock();
    recording = false;
    emb_irq_unlock(key);
}

emb_status_t emb_trace_stats_get(emb_trace_stats_t *out)
{
    emb_irq_key_t key;
    if (out == NULL) {
        return EMB_EINVAL;
    }
    key = emb_irq_lock();
    *out = trace_stats;
    emb_irq_unlock(key);
    return EMB_OK;
}

uint32_t embs_trace_next(void)
{
    emb_irq_key_t key = emb_irq_lock();
    uint32_t n = ring_next;
    emb_irq_unlock(key);
    return n;
}

bool embs_trace_read(uint32_t seq, uint8_t out[EMB_TRACE_RECORD_SIZE])
{
    emb_irq_key_t key = emb_irq_lock();
    bool kept = ((ring_next - seq) - 1u) < RING_EVENTS; /* seq in [next - size, next) */
    if (kept) {
        const trace_rec_t *r = &trace_ring[seq & RING_MASK];
        uint32_t i;
        uint32_t k;
        uint32_t w[5];
        w[0] = r->ts;
        w[1] = (uint32_t)r->id | ((uint32_t)r->cpu << 16) | ((uint32_t)r->context << 24);
        w[2] = r->a[0];
        w[3] = r->a[1];
        w[4] = r->a[2];
        for (i = 0u; i < 5u; i++) { /* bounded: five words, little-endian */
            for (k = 0u; k < 4u; k++) {
                out[(4u * i) + k] = (uint8_t)(w[i] >> (8u * k));
            }
        }
    }
    emb_irq_unlock(key);
    return kept;
}

/* ---- packets ------------------------------------------------------------------------ */

static void put_u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    put_u16(p, v);
    put_u16(&p[2], v >> 16);
}

static void packet_header(uint8_t *p, uint32_t kind, uint32_t count, uint32_t first)
{
    emb_trace_stats_t s;
    (void)emb_trace_stats_get(&s);
    p[0] = (uint8_t)PACKET_VERSION;
    p[1] = (uint8_t)kind;
    put_u16(&p[2], count);
    put_u32(&p[4], first);
    put_u32(&p[8], emb_arch_cycles_hz());
    put_u32(&p[12], s.overwritten);
}

/* The events from *from up to (not including) end, in packets of consecutive events; *from moves
 * past them. Events the ring overwrote before they were read are counted and skipped. */
static void send_events(uint8_t *packet, uint32_t *from, uint32_t end)
{
    uint8_t *slot = &packet[PACKET_HEADER];
    while (*from != end) { /* bounded: each pass moves *from at least one event on */
        uint32_t first;
        uint32_t n = 0u;
        while ((*from != end) && !embs_trace_read(*from, slot)) { /* bounded by end */
            emb_irq_key_t key = emb_irq_lock();
            trace_stats.overwritten++;
            emb_irq_unlock(key);
            (*from)++;
        }
        first = *from;
        while ((n < PACKET_EVENTS) && (*from != end) &&
               embs_trace_read(
                   *from, &slot[(size_t)n * EMB_TRACE_RECORD_SIZE])) { /* bounded by a packet */
            n++;
            (*from)++;
        }
        if (n != 0u) {
            emb_irq_key_t key;
            packet_header(packet, EMB_TRACE_PACKET_EVENTS, n, first);
            (void)emb_console_frame(EMB_FRAME_CHANNEL_TRACE, packet,
                                    PACKET_HEADER + (n * EMB_TRACE_RECORD_SIZE));
            key = emb_irq_lock();
            trace_stats.sent += n;
            emb_irq_unlock(key);
        }
    }
}

#if CONFIG_EMB_THREAD_LIST && CONFIG_EMB_OBJ_NAMES
typedef struct names {
    uint8_t *packet;
    uint32_t len;
    uint32_t count;
} names_t;

static void name_one(emb_thread_t thread, void *arg)
{
    names_t *n = (names_t *)arg;
    const char *name = emb_thread_name(thread);
    uint32_t len = 0u;
    uint32_t i;
    while ((len < NAME_MAX) && (name != NULL) && (name[len] != '\0')) { /* bounded */
        len++;
    }
    if ((n->len + 2u + len) > NAMES_PAYLOAD_MAX) {
        return; /* the packet is full: the rest stay "thread <index>" on the host */
    }
    n->packet[n->len] = emb_thread_index(thread);
    n->packet[n->len + 1u] = (uint8_t)len;
    for (i = 0u; i < len; i++) { /* bounded by NAME_MAX */
        n->packet[n->len + 2u + i] = (uint8_t)name[i];
    }
    n->len += 2u + len;
    n->count++;
}

/* The threads' names, so the host shows names rather than indexes: from a thread only, where the
 * thread list may be walked. */
static void send_names(uint8_t *packet)
{
    names_t n;
    n.packet = packet;
    n.len = PACKET_HEADER;
    n.count = 0u;
    emb_thread_foreach(name_one, &n);
    packet_header(packet, EMB_TRACE_PACKET_NAMES, n.count, 0u);
    (void)emb_console_frame(EMB_FRAME_CHANNEL_TRACE, packet, n.len);
}
#endif

static uint8_t dump_packet[PACKET_HEADER + (PACKET_EVENTS * EMB_TRACE_RECORD_SIZE)];
static bool dumping;

emb_status_t emb_trace_dump(void)
{
    uint32_t end;
    uint32_t from;
    emb_irq_key_t key;
    bool busy;
    if (!CONFIG_EMB_CONSOLE_FRAMED) {
        return EMB_ENOTSUP;
    }
    key = emb_irq_lock();
    busy = dumping;
    dumping = true;
    end = ring_next;
    emb_irq_unlock(key);
    if (busy) {
        return EMB_OK; /* a dump this one interrupted is sending the ring */
    }
#if CONFIG_EMB_THREAD_LIST && CONFIG_EMB_OBJ_NAMES
    if ((emb_context() == EMB_CONTEXT_THREAD) && !emb_irq_is_locked()) {
        send_names(dump_packet);
    }
#endif
    from = (end > RING_EVENTS) ? (end - RING_EVENTS) : 0u;
    send_events(dump_packet, &from, end);
    key = emb_irq_lock();
    dumping = false;
    emb_irq_unlock(key);
    return EMB_OK;
}

/* ---- streaming ---------------------------------------------------------------------- */

#if CONFIG_EMB_TRACE_STREAM
static uint8_t stream_packet[PACKET_HEADER + (PACKET_EVENTS * EMB_TRACE_RECORD_SIZE)];
static emb_thread_storage_t stream_storage;
EMB_THREAD_STACK(stream_stack, CONFIG_EMB_TRACE_STREAM_STACK);

/* Sends the events since its last pass, every CONFIG_EMB_TRACE_STREAM_MS: what it causes itself
 * (its wake-up, the console's lock) is traced too, and the period bounds it. */
static void stream_thread(void *arg)
{
    uint32_t from = embs_trace_next();
    emb_instant_t next = emb_time_now();
    (void)arg;
#if CONFIG_EMB_THREAD_LIST && CONFIG_EMB_OBJ_NAMES
    send_names(stream_packet);
#endif
    for (;;) { /* bounded: the thread serves the ring for as long as the system runs */
        uint32_t end;
        next = emb_instant_add(next, EMB_MS(CONFIG_EMB_TRACE_STREAM_MS));
        (void)emb_thread_sleep_until(next);
        end = embs_trace_next();
        if ((end - from) > RING_EVENTS) { /* lapped: what was overwritten is lost */
            emb_irq_key_t key = emb_irq_lock();
            trace_stats.overwritten += (end - from) - RING_EVENTS;
            emb_irq_unlock(key);
            from = end - RING_EVENTS;
        }
        send_events(stream_packet, &from, end);
    }
}

static void stream_init(void)
{
    emb_thread_attr_t ta;
    emb_thread_t th;
    emb_thread_attr_default(&ta);
    ta.name = "emb_trace";
    ta.stack = stream_stack;
    ta.stack_size = sizeof(stream_stack);
#if CONFIG_EMB_TRACE_STREAM_PRIORITY == 0
    ta.priority = EMB_PRIORITY_MIN;
#else
    ta.priority = (uint8_t)CONFIG_EMB_TRACE_STREAM_PRIORITY;
#endif
    (void)emb_check_status(emb_thread_init(&stream_storage, &ta, stream_thread, NULL, &th),
                           "emb_trace stream thread");
    (void)emb_check_status(emb_thread_start(th), "emb_trace stream thread");
}
EMB_INIT_TABLE_ENTRY(stream_init);
#endif

#endif /* CONFIG_EMB_TRACE_RING */
