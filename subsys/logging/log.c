/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    log.c
 * @brief   The log ring, its producers and its consumers (SPEC-015 §2, A127).
 *
 * @details
 * The ring is a power-of-two byte array of records, each a two-byte header (its length, and the
 * committed bit in the second byte) and SPEC-015 §2.3's record: the identifier, the timestamp,
 * the arguments.
 *
 * A producer reserves a record's room with interrupts masked for a bounded number of
 * instructions (the free space, the head, the header), copies the record with interrupts enabled
 * and commits it with one byte store. A producer that preempts another reserves after it, so the
 * ring keeps the order of reservation. Records that do not fit are dropped and counted, and the
 * next record that fits brings a dropped record before it.
 *
 * A consumer takes the record at the tail when it is committed and stops at the first that is
 * not. It claims the record in a critical section, copies it out with interrupts enabled, and
 * advances the tail only if no other consumer claimed meanwhile; one that did (emb_log_flush()
 * from an interrupt over the log thread) took the record itself, and the first looks again. The
 * free space ends at the tail, so a record being copied is never overwritten.
 *
 * @see     include/emb/log.h
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/config.h>
#include <emb/log.h>

#if CONFIG_EMB_LOG

#include <emb/console.h>
#include <emb/context.h>
#include <emb/frame.h>
#include <emb/irq.h>
#include <emb/kernel.h>
#include <emb/sched.h>
#include <emb/sem.h>
#include <emb/storage.h>
#include <emb/thread.h>
#include <emb/time.h>

#include <emb_board.h>
#include <embs/log.h>

#if CONFIG_EMB_LOG_DEFERRED
#include <embk/crash.h> /* emb_build_id */
#endif

#include <stdarg.h>
#include <stdbool.h>

/* ---- sizes ------------------------------------------------------------------------- */

#define RING_SIZE     ((uint32_t)CONFIG_EMB_LOG_RING_SIZE)
#define RING_MASK     (RING_SIZE - 1u)
#define HDR_BYTES     2u
#define HDR_COMMITTED 0x80u
#define LEN_HI_MASK   0x7Fu

#if CONFIG_EMB_LOG_DEFERRED
#define ID_MAX 4u
#else
#define ID_MAX                                                                         \
    ((uint32_t)sizeof(void *)) /* the plain-text record carries its metadata's address \
                                */
#endif
#if CONFIG_EMB_LOG_TIMESTAMP
#define TS_BYTES 4u
#else
#define TS_BYTES 0u
#endif
#define STR_MAX       ((uint32_t)CONFIG_EMB_LOG_STR_MAX)
#define HEX_MAX       ((uint32_t)CONFIG_EMB_LOG_HEXDUMP_MAX)
#define ARG_BYTES_MAX (((STR_MAX + 1u) > 8u) ? (STR_MAX + 1u) : 8u)
#define ARGS_BYTES_MAX                                                                          \
    (((EMB_LOG_ARGS_MAX * ARG_BYTES_MAX) > (HEX_MAX + 2u)) ? (EMB_LOG_ARGS_MAX * ARG_BYTES_MAX) \
                                                           : (HEX_MAX + 2u))
#define RECORD_MAX  (ID_MAX + TS_BYTES + ARGS_BYTES_MAX)
#define DROPPED_MAX (ID_MAX + TS_BYTES + 2u) /* the dropped record, at its widest */

/* A consumer's batch: whole records, at least the largest. */
#define BATCH_MAX ((RECORD_MAX > 128u) ? RECORD_MAX : 128u)

EMB_STATIC_ASSERT((RING_SIZE & RING_MASK) == 0u, "CONFIG_EMB_LOG_RING_SIZE is a power of two");
EMB_STATIC_ASSERT(RECORD_MAX <= 0x7FFFu, "a record's length fits its header");
EMB_STATIC_ASSERT(((2u * HDR_BYTES) + RECORD_MAX + DROPPED_MAX) <= RING_SIZE,
                  "the log ring holds the largest record and a dropped record");

#if CONFIG_EMB_LOG_THREAD
#if CONFIG_EMB_LOG_THREAD_PRIORITY == 0
#define LOG_PRIORITY EMB_PRIORITY_MIN
#else
#define LOG_PRIORITY ((uint8_t)CONFIG_EMB_LOG_THREAD_PRIORITY)
EMB_STATIC_ASSERT(CONFIG_EMB_LOG_THREAD_PRIORITY < CONFIG_EMB_PRIORITY_COUNT,
                  "CONFIG_EMB_LOG_THREAD_PRIORITY is not an application priority");
#endif
#endif

/* ---- the ring ---------------------------------------------------------------------- */

static uint8_t ring[RING_SIZE];
/* Lock domain: the critical section (emb_irq_lock), for every variable below. */
static uint32_t ring_head;   /* the next byte to reserve */
static uint32_t ring_tail;   /* the oldest byte not taken: the free space ends here */
static uint32_t ring_claims; /* moved by each consumer's claim, so a preempted one can tell */
static uint32_t ring_lost;   /* records refused since the last one that fitted */
static emb_log_stats_t ring_stats;
static bool flushing; /* emb_log_flush() is sending: a nested one leaves it to it */
#if CONFIG_EMB_LOG_THREAD
static bool log_ready; /* the log thread's semaphore exists */
static emb_sem_t log_sem;
#endif

static void ring_set(uint32_t pos, uint8_t b)
{
    ring[pos & RING_MASK] = b;
}

static uint8_t ring_at(uint32_t pos)
{
    return ring[pos & RING_MASK];
}

/* A record's header, its committed bit clear. */
static void hdr_write(uint32_t pos, uint32_t len)
{
    ring_set(pos, (uint8_t)(len & 0xFFu));
    ring_set(pos + 1u, (uint8_t)((len >> 8) & LEN_HI_MASK));
}

/* Writes a record's bytes in place, wrapping at the ring's end. */
typedef struct rec_writer {
    uint32_t pos;
} rec_writer_t;

static void put8(rec_writer_t *w, uint8_t b)
{
    ring_set(w->pos, b);
    w->pos++;
}

static void put_le(rec_writer_t *w, uint64_t v, uint32_t bytes)
{
    uint32_t i;
    for (i = 0u; i < bytes; i++) { /* bounded: at most eight bytes */
        put8(w, (uint8_t)(v >> (8u * i)));
    }
}

static void log_wake(void)
{
#if CONFIG_EMB_LOG_THREAD
    if (log_ready && (emb_context() != EMB_CONTEXT_PREKERNEL)) {
        (void)emb_sem_give(log_sem); /* binary and saturating: a wake already pending stays one */
    }
#endif
}

static uint32_t now_ticks(void)
{
    return (uint32_t)emb_time_now().ticks;
}

/*
 * Reserves room for a record of len bytes, and before it for a dropped record when records were
 * lost. In the critical section: the free space, the headers, the head, and the timestamp, so
 * timestamps grow in the ring's order.
 */
static bool ring_reserve(uint32_t len, uint32_t dropped_len, uint32_t *pos, uint32_t *lost,
                         uint32_t *ts)
{
    emb_irq_key_t key = emb_irq_lock();
    uint32_t used = ring_head - ring_tail;
    uint32_t extra = (ring_lost != 0u) ? (HDR_BYTES + dropped_len) : 0u;
    uint32_t need = extra + HDR_BYTES + len;
    bool ok = need <= (RING_SIZE - used);
    if (ok) {
        *pos = ring_head;
        *lost = ring_lost;
        ring_lost = 0u;
        if (extra != 0u) {
            hdr_write(ring_head, dropped_len);
        }
        hdr_write(ring_head + extra, len);
        ring_head += need;
        if ((used + need) > ring_stats.high_water) {
            ring_stats.high_water = used + need;
        }
        *ts = now_ticks();
    } else {
        if (ring_lost != UINT32_MAX) {
            ring_lost++;
        }
        ring_stats.dropped++;
    }
    emb_irq_unlock(key);
    return ok;
}

/* Sets the record's committed bit, its bytes written before it; wakes the log thread when the
 * record is the oldest, which the thread may be waiting on. */
static void ring_commit(uint32_t pos, uint32_t len)
{
    emb_irq_key_t key;
    bool at_tail;
    EMB_COMPILER_BARRIER(); /* the record's bytes before the bit that publishes them */
    ring_set(pos + 1u, (uint8_t)(((len >> 8) & LEN_HI_MASK) | HDR_COMMITTED));
    key = emb_irq_lock();
    ring_stats.records++;
    at_tail = pos == ring_tail;
    emb_irq_unlock(key);
    if (at_tail) {
        log_wake();
    }
}

/* ---- records ----------------------------------------------------------------------- */

#if CONFIG_EMB_LOG_DEFERRED
EMB_TABLE_BOUNDS(const uint8_t, emb_log);

/* What tools/logdec reads first: in the ELF only, as the call sites' metadata. */
EMB_LOG_SECTION_ const emb_log_header_t emb_log_header
    EMB_USED = {{'E', 'M', 'B', 'L'},
                1u,
                (uint8_t)(CONFIG_EMB_LOG_TIMESTAMP ? 1u : 0u),
                (uint8_t)sizeof(long),
                (uint8_t)sizeof(void *),
                (uint8_t)sizeof(double),
                (uint8_t)CONFIG_EMB_LOG_STR_MAX,
                {0u, 0u}};

/* 16-bit identifiers while the section is under 64 KiB minus EMB_LOG_ID_FIRST, else 32: fixed
 * when the image is linked, and the decoder computes the same from the ELF. */
static uint32_t id_bytes(void)
{
    uintptr_t size = (uintptr_t)EMB_TABLE_STOP(emb_log) - (uintptr_t)EMB_TABLE_START(emb_log);
    return (size <= (uintptr_t)(0xFFFFu - EMB_LOG_ID_FIRST)) ? 2u : 4u;
}

static uint64_t record_id(const emb_log_meta_t *meta)
{
    return (uint64_t)((uintptr_t)meta - (uintptr_t)EMB_TABLE_START(emb_log)) + EMB_LOG_ID_FIRST;
}
#else
static uint32_t id_bytes(void)
{
    return ID_MAX;
}

static uint64_t record_id(const emb_log_meta_t *meta)
{
    return (uint64_t)(uintptr_t)meta;
}
#endif

/* The dropped record: an identifier, the timestamp, a 16-bit count. */
static uint32_t dropped_bytes(void)
{
    return id_bytes() + TS_BYTES + 2u;
}

static void put_head(rec_writer_t *w, uint64_t id, uint32_t ts)
{
    put_le(w, id, id_bytes());
#if CONFIG_EMB_LOG_TIMESTAMP
    put_le(w, ts, 4u);
#else
    (void)ts;
#endif
}

static uint32_t str_len(const char *s)
{
    uint32_t n = 0u;
    if (s != NULL) {
        while ((n < STR_MAX) && (s[n] != '\0')) { /* bounded by CONFIG_EMB_LOG_STR_MAX */
            n++;
        }
    }
    return n;
}

static uint32_t sig_at(uint32_t sig, uint32_t k)
{
    return (sig >> (4u * k)) & 0xFu;
}

/* The arguments' bytes, read from a copy of the argument list. A type the record cannot carry
 * ends the arguments: the decoder stops at it too. */
static uint32_t args_size(uint32_t sig, va_list *ap)
{
    uint32_t size = 0u;
    uint32_t k;
    for (k = 0u; k < EMB_LOG_ARGS_MAX; k++) { /* bounded: eight arguments */
        uint32_t t = sig_at(sig, k);
        if ((t == EMB_LOG_T_END) || (t > EMB_LOG_T_HEX)) {
            break;
        }
        switch (t) {
        case EMB_LOG_T_INT:
            (void)va_arg(*ap, int);
            size += 4u;
            break;
        case EMB_LOG_T_UINT:
            (void)va_arg(*ap, unsigned int);
            size += 4u;
            break;
        case EMB_LOG_T_LONG:
            (void)va_arg(*ap, long);
            size += (uint32_t)sizeof(long);
            break;
        case EMB_LOG_T_ULONG:
            (void)va_arg(*ap, unsigned long);
            size += (uint32_t)sizeof(long);
            break;
        case EMB_LOG_T_LLONG:
            (void)va_arg(*ap, long long);
            size += 8u;
            break;
        case EMB_LOG_T_ULLONG:
            (void)va_arg(*ap, unsigned long long);
            size += 8u;
            break;
        case EMB_LOG_T_DOUBLE:
            (void)va_arg(*ap, double);
            size += (uint32_t)sizeof(double);
            break;
        case EMB_LOG_T_STR:
            size += 1u + str_len(va_arg(*ap, const char *));
            break;
        case EMB_LOG_T_PTR:
            (void)va_arg(*ap, void *);
            size += (uint32_t)sizeof(void *);
            break;
        default: { /* EMB_LOG_T_HEX: a pointer and a length */
            size_t n;
            (void)va_arg(*ap, const void *);
            n = va_arg(*ap, size_t);
            size += 2u + ((n < HEX_MAX) ? (uint32_t)n : HEX_MAX);
            break;
        }
        }
    }
    return size;
}

static void put_bytes(rec_writer_t *w, const void *src, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)src;
    uint32_t i;
    for (i = 0u; i < n; i++) { /* bounded by n */
        put8(w, p[i]);
    }
}

static void args_write(rec_writer_t *w, uint32_t sig, va_list *ap)
{
    uint32_t k;
    for (k = 0u; k < EMB_LOG_ARGS_MAX; k++) { /* bounded: eight arguments */
        uint32_t t = sig_at(sig, k);
        if ((t == EMB_LOG_T_END) || (t > EMB_LOG_T_HEX)) {
            break;
        }
        switch (t) {
        case EMB_LOG_T_INT:
            put_le(w, (uint64_t)(int64_t)va_arg(*ap, int), 4u);
            break;
        case EMB_LOG_T_UINT:
            put_le(w, (uint64_t)va_arg(*ap, unsigned int), 4u);
            break;
        case EMB_LOG_T_LONG:
            put_le(w, (uint64_t)(int64_t)va_arg(*ap, long), (uint32_t)sizeof(long));
            break;
        case EMB_LOG_T_ULONG:
            put_le(w, (uint64_t)va_arg(*ap, unsigned long), (uint32_t)sizeof(long));
            break;
        case EMB_LOG_T_LLONG:
            put_le(w, (uint64_t)va_arg(*ap, long long), 8u);
            break;
        case EMB_LOG_T_ULLONG:
            put_le(w, (uint64_t)va_arg(*ap, unsigned long long), 8u);
            break;
        case EMB_LOG_T_DOUBLE: {
            double d = va_arg(*ap, double);
            put_bytes(w, &d, (uint32_t)sizeof(double)); /* little-endian on every port */
            break;
        }
        case EMB_LOG_T_STR: {
            const char *s = va_arg(*ap, const char *);
            uint32_t n = str_len(s);
            put8(w, (uint8_t)n);
            put_bytes(w, s, n);
            break;
        }
        case EMB_LOG_T_PTR:
            put_le(w, (uint64_t)(uintptr_t)va_arg(*ap, void *), (uint32_t)sizeof(void *));
            break;
        default: { /* EMB_LOG_T_HEX */
            const void *p = va_arg(*ap, const void *);
            size_t len = va_arg(*ap, size_t);
            uint32_t n = (len < HEX_MAX) ? (uint32_t)len : HEX_MAX;
            put_le(w, n, 2u);
            if (p != NULL) {
                put_bytes(w, p, n);
            } else {
                uint32_t i;
                for (i = 0u; i < n; i++) { /* bounded by HEX_MAX: nothing to dump, zeros */
                    put8(w, 0u);
                }
            }
            break;
        }
        }
    }
}

/* A dropped record: its count saturates at 65535. */
static void put_dropped(uint32_t pos, uint32_t lost, uint32_t ts)
{
    rec_writer_t w;
    w.pos = pos + HDR_BYTES;
    put_head(&w, EMB_LOG_ID_DROPPED, ts);
    put_le(&w, (lost < 0xFFFFu) ? lost : 0xFFFFu, 2u);
    ring_commit(pos, dropped_bytes());
}

void emb_log_write(const emb_log_meta_t *meta, uint32_t sig, ...)
{
    va_list ap;
    va_list sizing;
    uint32_t len;
    uint32_t pos = 0u;
    uint32_t lost = 0u;
    uint32_t ts = 0u;
    if (meta == NULL) {
        return;
    }
    va_start(ap, sig);
    va_copy(sizing, ap);
    len = id_bytes() + TS_BYTES + args_size(sig, &sizing);
    va_end(sizing);
    if (ring_reserve(len, dropped_bytes(), &pos, &lost, &ts)) {
        rec_writer_t w;
        if (lost != 0u) {
            put_dropped(pos, lost, ts);
            pos += HDR_BYTES + dropped_bytes();
        }
        w.pos = pos + HDR_BYTES;
        put_head(&w, record_id(meta), ts);
        args_write(&w, sig, &ap);
        ring_commit(pos, len);
    }
    va_end(ap);
}

emb_status_t emb_log_level_set(emb_log_module_t *module, uint8_t level)
{
    if ((module == NULL) || (level > EMB_LOG_LEVEL_DBG)) {
        return EMB_EINVAL;
    }
    module->level = level; /* one byte: a call reads the old level or the new */
    return EMB_OK;
}

emb_status_t emb_log_stats_get(emb_log_stats_t *out)
{
    emb_irq_key_t key;
    if (out == NULL) {
        return EMB_EINVAL;
    }
    key = emb_irq_lock();
    *out = ring_stats;
    emb_irq_unlock(key);
    return EMB_OK;
}

/* ---- consumers --------------------------------------------------------------------- */

typedef enum take_result {
    TAKE_NONE,    /* the ring is empty, or its oldest record is not committed */
    TAKE_TOO_BIG, /* the oldest record does not fit in what is left of the buffer */
    TAKE_DONE
} take_result_t;

/* Takes the oldest record, if committed, into buf (cap bytes); its length in *len. */
static take_result_t ring_take(uint8_t *buf, uint32_t cap, uint32_t *len)
{
    for (;;) { /* bounded: a retry means another consumer took the record meanwhile */
        emb_irq_key_t key = emb_irq_lock();
        uint32_t at = ring_tail;
        uint32_t claim;
        uint32_t n;
        uint32_t i;
        uint8_t hi;
        bool taken;
        if (ring_head == at) {
            emb_irq_unlock(key);
            return TAKE_NONE;
        }
        hi = ring_at(at + 1u);
        if ((hi & HDR_COMMITTED) == 0u) {
            emb_irq_unlock(key);
            return TAKE_NONE; /* in order: the records after it wait for it */
        }
        n = (uint32_t)ring_at(at) | ((uint32_t)(hi & LEN_HI_MASK) << 8);
        if (n > cap) {
            emb_irq_unlock(key);
            return TAKE_TOO_BIG;
        }
        ring_claims++;
        claim = ring_claims;
        emb_irq_unlock(key);
        for (i = 0u; i < n; i++) { /* bounded by the record; producers stay out of it */
            buf[i] = ring_at(at + HDR_BYTES + i);
        }
        key = emb_irq_lock();
        taken = (ring_claims == claim) && (ring_tail == at);
        if (taken) {
            ring_tail = at + HDR_BYTES + n;
            ring_stats.sent++;
        }
        emb_irq_unlock(key);
        if (taken) {
            *len = n;
            return TAKE_DONE;
        }
    }
}

#if CONFIG_EMB_LOG_DEFERRED

typedef struct consumer {
    uint8_t batch[BATCH_MAX];
} consumer_t;

/* Sends the committed records as frames on the log channel, each holding whole records. */
static void drain(consumer_t *c)
{
    uint32_t n = 0u;
    for (;;) { /* bounded: each pass takes a record or sends the batch, until none is left */
        uint32_t len = 0u;
        take_result_t r = ring_take(&c->batch[n], BATCH_MAX - n, &len);
        if (r == TAKE_DONE) {
            n += len;
        } else if ((r == TAKE_TOO_BIG) && (n != 0u)) {
            (void)emb_console_frame(EMB_FRAME_CHANNEL_LOG, c->batch, n);
            n = 0u;
        } else {
            break;
        }
    }
    if (n != 0u) {
        (void)emb_console_frame(EMB_FRAME_CHANNEL_LOG, c->batch, n);
    }
}

/* The stream's first record: the image it comes from, and the clock's tick. */
static void put_start(void)
{
    uint32_t len = id_bytes() + TS_BYTES + EMB_CRASH_BUILD_ID_SIZE + 4u;
    uint32_t pos = 0u;
    uint32_t lost = 0u;
    uint32_t ts = 0u;
    if (ring_reserve(len, dropped_bytes(), &pos, &lost, &ts)) {
        rec_writer_t w;
        uint32_t i;
        if (lost != 0u) {
            put_dropped(pos, lost, ts);
            pos += HDR_BYTES + dropped_bytes();
        }
        w.pos = pos + HDR_BYTES;
        put_head(&w, EMB_LOG_ID_START, ts);
        for (i = 0u; i < EMB_CRASH_BUILD_ID_SIZE; i++) { /* bounded: 20 bytes */
            put8(&w, EMB_FLASH_READ_U8(&emb_build_id[i]));
        }
        put_le(&w, (uint32_t)CONFIG_EMB_TICK_NS, 4u);
        ring_commit(pos, len);
    }
}

#else /* CONFIG_EMB_LOG_TEXT */

typedef struct consumer {
    uint8_t rec[RECORD_MAX];
    char line[CONFIG_EMB_LOG_LINE_MAX];
} consumer_t;

/* A line being formatted: room is its size less one, kept for the newline. */
typedef struct out {
    char *buf;
    uint32_t len;
    uint32_t room;
} out_t;

static void out_c(out_t *o, char c)
{
    if (o->len < o->room) {
        o->buf[o->len] = c;
        o->len++;
    }
}

static void out_n(out_t *o, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0u; i < n; i++) { /* bounded by n */
        out_c(o, s[i]);
    }
}

static void out_z(out_t *o, const char *s)
{
    uint32_t i = 0u;
    while ((s[i] != '\0') && (o->len < o->room)) { /* bounded by the line */
        out_c(o, s[i]);
        i++;
    }
}

static void out_pad(out_t *o, char c, uint32_t n)
{
    uint32_t i;
    for (i = 0u; (i < n) && (o->len < o->room); i++) { /* bounded by the line */
        out_c(o, c);
    }
}

/* A character of a format or a module's name: in program memory on a Harvard core. */
static char fch(const char *s, uint32_t i)
{
    return (char)EMB_FLASH_READ_U8((const uint8_t *)&s[i]);
}

/* A name from program memory, as out_z(). */
static void out_zf(out_t *o, const char *s)
{
    uint32_t i = 0u;
    while ((fch(s, i) != '\0') && (o->len < o->room)) { /* bounded by the line */
        out_c(o, fch(s, i));
        i++;
    }
}

/* A record's arguments, read in order. */
typedef struct rd {
    const uint8_t *p;
    uint32_t n;
    uint32_t at;
} rd_t;

static uint64_t rd_le(rd_t *r, uint32_t bytes)
{
    uint64_t v = 0u;
    uint32_t i;
    for (i = 0u; (i < bytes) && (r->at < r->n); i++) { /* bounded: at most eight bytes */
        v |= (uint64_t)r->p[r->at] << (8u * i);
        r->at++;
    }
    return v;
}

static int64_t sign_extend(uint64_t v, uint32_t bytes)
{
    uint64_t sign = (uint64_t)1u << ((8u * bytes) - 1u);
    return (bytes >= 8u) ? (int64_t)v : (int64_t)((v ^ sign) - sign);
}

/* A conversion's flags, width and precision. */
typedef struct spec {
    uint32_t width;
    int32_t prec; /* -1: none */
    bool left;
    bool zero;
    bool plus;
    bool space;
    bool alt;
} spec_t;

/* Writes digits, a sign and a prefix within the conversion's width. */
static void out_field(out_t *o, const spec_t *sp, const char *sign, const char *digits, uint32_t nd)
{
    uint32_t ns = 0u;
    uint32_t zeros = 0u;
    uint32_t total;
    while (sign[ns] != '\0') { /* bounded: at most "-0x" */
        ns++;
    }
    if ((sp->prec >= 0) && ((uint32_t)sp->prec > nd)) {
        zeros = (uint32_t)sp->prec - nd;
    }
    total = ns + zeros + nd;
    if ((sp->zero) && (!sp->left) && (sp->prec < 0) && (sp->width > total)) {
        zeros += sp->width - total;
        total = sp->width;
    }
    if ((!sp->left) && (sp->width > total)) {
        out_pad(o, ' ', sp->width - total);
    }
    out_z(o, sign);
    out_pad(o, '0', zeros);
    out_n(o, digits, nd);
    if (sp->left && (sp->width > total)) {
        out_pad(o, ' ', sp->width - total);
    }
}

static uint32_t to_digits(uint64_t v, uint32_t base, bool upper, char *buf)
{
    char tmp[24];
    uint32_t n = 0u;
    uint32_t i;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { /* bounded: at most 22 digits of a 64-bit value */
        tmp[n] = set[v % base];
        n++;
        v /= base;
    } while (v != 0u);
    for (i = 0u; i < n; i++) { /* bounded by n */
        buf[i] = tmp[n - 1u - i];
    }
    return n;
}

static void out_int(out_t *o, const spec_t *sp, int64_t sv, uint64_t uv, char conv)
{
    char digits[24];
    uint32_t nd;
    const char *sign = "";
    if ((conv == 'd') || (conv == 'i')) {
        uint64_t mag = (sv < 0) ? (0u - (uint64_t)sv) : (uint64_t)sv;
        sign = (sv < 0) ? "-" : (sp->plus ? "+" : (sp->space ? " " : ""));
        nd = to_digits(mag, 10u, false, digits);
    } else if ((conv == 'x') || (conv == 'X')) {
        nd = to_digits(uv, 16u, conv == 'X', digits);
        sign = (sp->alt && (uv != 0u)) ? ((conv == 'X') ? "0X" : "0x") : "";
    } else if (conv == 'o') {
        nd = to_digits(uv, 8u, false, digits);
        sign = (sp->alt && (uv != 0u)) ? "0" : "";
    } else if (conv == 'p') {
        nd = to_digits(uv, 16u, false, digits);
        sign = "0x";
    } else {
        nd = to_digits(uv, 10u, false, digits);
    }
    if ((sp->prec == 0) && (uv == 0u) && (sv == 0) && (conv != 'p')) {
        nd = 0u; /* printf: a zero with precision 0 has no digits */
    }
    out_field(o, sp, sign, digits, nd);
}

#if CONFIG_EMB_LOG_FLOAT
/* Fixed point for %f (and %e and %g, which print as %f here): up to nine decimals. */
static void out_double(out_t *o, const spec_t *sp, double d)
{
    static const uint32_t pow10[10] = {1u,      10u,      100u,      1000u,      10000u,
                                       100000u, 1000000u, 10000000u, 100000000u, 1000000000u};
    char digits[40];
    uint32_t nd;
    uint32_t prec = (sp->prec < 0) ? 6u : (((uint32_t)sp->prec > 9u) ? 9u : (uint32_t)sp->prec);
    const char *sign = "";
    uint64_t ip;
    uint64_t fp;
    bool neg = d < 0.0;
    if (d != d) { /* NaN */
        out_field(o, sp, "", "nan", 3u);
        return;
    }
    if (neg) {
        d = -d;
    }
    sign = neg ? "-" : (sp->plus ? "+" : (sp->space ? " " : ""));
    if (d >= 1.8e19) { /* beyond a 64-bit integer part, infinity included */
        out_field(o, sp, sign, "inf", 3u);
        return;
    }
    ip = (uint64_t)d;
    {
        /* to the nearest, an exact half to the even neighbour, as printf rounds */
        double x = (d - (double)ip) * (double)pow10[prec];
        double rest;
        fp = (uint64_t)x;
        rest = x - (double)fp;
        if ((rest > 0.5) || ((rest == 0.5) && (((prec == 0u) ? ip : fp) & 1u) != 0u)) {
            if (prec == 0u) {
                ip++;
            } else {
                fp++;
            }
        }
    }
    if (fp >= pow10[prec]) {
        ip++;
        fp -= pow10[prec];
    }
    nd = to_digits(ip, 10u, false, digits);
    if ((prec != 0u) || sp->alt) {
        digits[nd] = '.';
        nd++;
    }
    if (prec != 0u) {
        char frac[12];
        uint32_t nf = to_digits(fp, 10u, false, frac);
        uint32_t i;
        for (i = nf; i < prec; i++) { /* bounded: nine decimals */
            digits[nd] = '0';
            nd++;
        }
        for (i = 0u; i < nf; i++) { /* bounded by nf */
            digits[nd] = frac[i];
            nd++;
        }
    }
    {
        spec_t fs = *sp;
        fs.prec = -1; /* the precision is spent on the decimals */
        out_field(o, &fs, sign, digits, nd);
    }
}
#endif

static bool is_digit(char c)
{
    return (c >= '0') && (c <= '9');
}

/* One argument's value, as its signature types it. */
typedef struct arg {
    uint64_t u;
    int64_t s;
    double d;
    const uint8_t *str;
    uint32_t str_len;
    uint32_t type;
} arg_t;

static void arg_read(rd_t *r, uint32_t type, arg_t *a)
{
    uint32_t w = 4u;
    a->type = type;
    a->u = 0u;
    a->s = 0;
    a->d = 0.0;
    a->str = NULL;
    a->str_len = 0u;
    switch (type) {
    case EMB_LOG_T_INT:
    case EMB_LOG_T_UINT:
        w = 4u;
        break;
    case EMB_LOG_T_LONG:
    case EMB_LOG_T_ULONG:
        w = (uint32_t)sizeof(long);
        break;
    case EMB_LOG_T_LLONG:
    case EMB_LOG_T_ULLONG:
        w = 8u;
        break;
    case EMB_LOG_T_PTR:
        w = (uint32_t)sizeof(void *);
        break;
    case EMB_LOG_T_DOUBLE: {
        double d = 0.0;
        uint8_t *dp = (uint8_t *)&d;
        uint32_t i;
        for (i = 0u; (i < (uint32_t)sizeof(double)) && (r->at < r->n); i++) { /* bounded */
            dp[i] = r->p[r->at];
            r->at++;
        }
        a->d = d;
        return;
    }
    case EMB_LOG_T_STR:
        a->str_len = (uint32_t)rd_le(r, 1u);
        a->str = &r->p[r->at];
        if ((r->at + a->str_len) > r->n) {
            a->str_len = r->n - r->at;
        }
        r->at += a->str_len;
        return;
    default:
        return; /* the end, or a type the record does not carry */
    }
    a->u = rd_le(r, w);
    a->s = ((type == EMB_LOG_T_INT) || (type == EMB_LOG_T_LONG) || (type == EMB_LOG_T_LLONG))
               ? sign_extend(a->u, w)
               : (int64_t)a->u;
}

/* The length modifiers that narrow a value: hh and h. */
static void narrow(arg_t *a, uint32_t h, char conv)
{
    if (h == 0u) {
        return;
    }
    a->u &= (h == 2u) ? 0xFFu : 0xFFFFu;
    if ((conv == 'd') || (conv == 'i')) {
        a->s = sign_extend(a->u, h == 2u ? 1u : 2u);
    } else {
        a->s = (int64_t)a->u;
    }
}

static void format(out_t *o, const char *fmt, uint32_t sig, rd_t *r)
{
    uint32_t k = 0u;
    uint32_t i = 0u;
    while (fch(fmt, i) != '\0') { /* bounded by the format */
        spec_t sp = {0u, -1, false, false, false, false, false};
        uint32_t h = 0u;
        arg_t a;
        char conv;
        if (fch(fmt, i) != '%') {
            out_c(o, fch(fmt, i));
            i++;
            continue;
        }
        i++;
        if (fch(fmt, i) == '%') {
            out_c(o, '%');
            i++;
            continue;
        }
        for (;;) { /* bounded by the format: its flags */
            char f = fch(fmt, i);
            if (f == '-') {
                sp.left = true;
            } else if (f == '0') {
                sp.zero = true;
            } else if (f == '+') {
                sp.plus = true;
            } else if (f == ' ') {
                sp.space = true;
            } else if (f == '#') {
                sp.alt = true;
            } else {
                break;
            }
            i++;
        }
        while (is_digit(fch(fmt, i))) { /* bounded by the format */
            sp.width = (sp.width * 10u) + (uint32_t)(fch(fmt, i) - '0');
            i++;
        }
        if (fch(fmt, i) == '.') {
            i++;
            sp.prec = 0;
            while (is_digit(fch(fmt, i))) { /* bounded by the format */
                sp.prec = (sp.prec * 10) + (int32_t)(fch(fmt, i) - '0');
                i++;
            }
        }
        while ((fch(fmt, i) == 'h') || (fch(fmt, i) == 'l') || (fch(fmt, i) == 'z') ||
               (fch(fmt, i) == 'j') || (fch(fmt, i) == 't') ||
               (fch(fmt, i) == 'L')) { /* bounded by the format */
            if (fch(fmt, i) == 'h') {
                h++;
            }
            i++;
        }
        conv = fch(fmt, i);
        if (conv == '\0') {
            break;
        }
        i++;
        if (sp.width > o->room) {
            sp.width = o->room;
        }
        arg_read(r, (k < EMB_LOG_ARGS_MAX) ? sig_at(sig, k) : EMB_LOG_T_END, &a);
        k++;
        if ((conv == 's') && (a.type == EMB_LOG_T_STR)) {
            uint32_t n = a.str_len;
            spec_t ss = sp;
            if ((sp.prec >= 0) && ((uint32_t)sp.prec < n)) {
                n = (uint32_t)sp.prec;
            }
            ss.prec = -1;
            ss.zero = false;
            out_field(o, &ss, "", (const char *)a.str, n);
        } else if (conv == 'c') {
            char ch = (char)a.u;
            spec_t cs = sp;
            cs.prec = -1;
            out_field(o, &cs, "", &ch, 1u);
        } else if ((conv == 'f') || (conv == 'F') || (conv == 'e') || (conv == 'E') ||
                   (conv == 'g') || (conv == 'G')) {
#if CONFIG_EMB_LOG_FLOAT
            out_double(o, &sp, (a.type == EMB_LOG_T_DOUBLE) ? a.d : (double)a.s);
#else
            out_z(o, "(float)"); /* CONFIG_EMB_LOG_FLOAT is off */
#endif
        } else {
            narrow(&a, h, conv);
            out_int(o, &sp, a.s, a.u, conv);
        }
    }
}

static void line_send(const char *line, uint32_t n)
{
#if CONFIG_EMB_CONSOLE_FRAMED
    (void)emb_console_frame(EMB_FRAME_CHANNEL_SHELL, line, n);
#else
    emb_board_write(line, (size_t)n);
#endif
}

/* "[seconds.milliseconds] E module: message" */
static void line_head(out_t *o, uint32_t ts, char level, const char *module, bool module_in_flash)
{
#if CONFIG_EMB_LOG_TIMESTAMP
    uint64_t ms = ((uint64_t)ts * (uint64_t)CONFIG_EMB_TICK_NS) / 1000000u;
    spec_t sp = {3u, -1, false, true, false, false, false};
    spec_t plain = {0u, -1, false, false, false, false, false};
    out_c(o, '[');
    out_int(o, &plain, (int64_t)(ms / 1000u), ms / 1000u, 'u');
    out_c(o, '.');
    out_int(o, &sp, (int64_t)(ms % 1000u), ms % 1000u, 'u');
    out_z(o, "] ");
#else
    (void)ts;
#endif
    out_c(o, level);
    out_c(o, ' ');
    if (module_in_flash) {
        out_zf(o, module);
    } else {
        out_z(o, module);
    }
    out_z(o, ": ");
}

size_t embs_log_line(const uint8_t *rec, size_t n, char *line, size_t cap)
{
    static const char levels[] = "?EWID";
    out_t o;
    rd_t r;
    uintptr_t id;
    uint32_t ts;
    if ((rec == NULL) || (line == NULL) || (cap < 2u)) {
        return 0u;
    }
    o.buf = line;
    o.len = 0u;
    o.room = (cap > 0xFFFFu) ? 0xFFFEu : (uint32_t)(cap - 1u);
    r.p = rec;
    r.n = (n > 0xFFFFu) ? 0xFFFFu : (uint32_t)n;
    r.at = 0u;
    id = (uintptr_t)rd_le(&r, ID_MAX);
    ts = (uint32_t)rd_le(&r, TS_BYTES);
    if (id == EMB_LOG_ID_DROPPED) {
        spec_t plain = {0u, -1, false, false, false, false, false};
        uint64_t lost = rd_le(&r, 2u);
        line_head(&o, ts, 'W', "emb_log", false);
        out_int(&o, &plain, (int64_t)lost, lost, 'u');
        out_z(&o, " records dropped");
    } else if (id >= EMB_LOG_ID_FIRST) {
        /* the call site's metadata: in program memory on a Harvard core */
        const emb_log_meta_t *m = (const emb_log_meta_t *)id;
        uint8_t level = EMB_FLASH_READ_U8(&m->level);
        uint32_t sig = EMB_FLASH_READ_U32(&m->sig);
        const char *fmt = (const char *)EMB_FLASH_READ_PTR(&m->format);
        const char *module = (const char *)EMB_FLASH_READ_PTR(&m->module);
        line_head(&o, ts, levels[(level <= EMB_LOG_LEVEL_DBG) ? level : 0u], module, true);
        if (sig == EMB_LOG_T_HEX) {
            uint32_t len = (uint32_t)rd_le(&r, 2u);
            uint32_t i;
            spec_t sp = {2u, -1, false, true, false, false, false};
            out_zf(&o, fmt);
            out_c(&o, ':');
            for (i = 0u; (i < len) && (r.at < r.n); i++) { /* bounded by the record */
                out_c(&o, ' ');
                out_int(&o, &sp, 0, r.p[r.at], 'x');
                r.at++;
            }
        } else {
            format(&o, fmt, sig, &r);
        }
    } else {
        return 0u; /* the stream's own records that only the deferred backend sends */
    }
    o.buf[o.len] = '\n';
    o.len++;
    return (size_t)o.len;
}

/* Formats and sends the committed records, one line each. */
static void drain(consumer_t *c)
{
    uint32_t n = 0u;
    while (ring_take(c->rec, RECORD_MAX, &n) == TAKE_DONE) { /* bounded by the ring */
        size_t len = embs_log_line(c->rec, (size_t)n, c->line, sizeof(c->line));
        if (len != 0u) {
            line_send(c->line, (uint32_t)len);
        }
    }
}

#endif /* CONFIG_EMB_LOG_DEFERRED */

/* ---- the steps, for tests (embs/log.h) ------------------------------------------- */

bool embs_log_reserve(uint32_t len, uint32_t *pos)
{
    uint32_t p = 0u;
    uint32_t lost = 0u;
    uint32_t ts = 0u;
    if ((pos == NULL) || (len > RECORD_MAX) ||
        !ring_reserve(len, dropped_bytes(), &p, &lost, &ts)) {
        return false;
    }
    if (lost != 0u) {
        put_dropped(p, lost, ts);
        p += HDR_BYTES + dropped_bytes();
    }
    *pos = p;
    return true;
}

void embs_log_put(uint32_t pos, const void *bytes, uint32_t len)
{
    rec_writer_t w;
    w.pos = pos + HDR_BYTES;
    put_bytes(&w, bytes, len);
}

void embs_log_commit(uint32_t pos, uint32_t len)
{
    ring_commit(pos, len);
}

size_t embs_log_take(uint8_t *buf, size_t cap)
{
    uint32_t n = 0u;
    uint32_t c = (cap > 0xFFFFu) ? 0xFFFFu : (uint32_t)cap;
    return ((buf != NULL) && (ring_take(buf, c, &n) == TAKE_DONE)) ? (size_t)n : 0u;
}

uint32_t embs_log_id_bytes(void)
{
    return id_bytes();
}

/* ---- the log thread and the flush ------------------------------------------------- */

static consumer_t flush_consumer;

#if CONFIG_EMB_LOG_THREAD
static consumer_t thread_consumer;
static emb_sem_storage_t log_sem_storage;
static emb_thread_storage_t log_thread_storage;
EMB_THREAD_STACK(log_stack, CONFIG_EMB_LOG_THREAD_STACK);

static void log_thread(void *arg)
{
    (void)arg;
    for (;;) { /* bounded: the thread serves the ring for as long as the system runs */
        (void)emb_sem_take(log_sem, EMB_WAIT_FOREVER);
        drain(&thread_consumer);
    }
}
#endif

void emb_log_flush(void)
{
    emb_irq_key_t key = emb_irq_lock();
    bool busy = flushing;
    flushing = true;
    emb_irq_unlock(key);
    if (busy) {
        return; /* a flush this one interrupted sends everything */
    }
    drain(&flush_consumer);
    key = emb_irq_lock();
    flushing = false;
    emb_irq_unlock(key);
}

static void log_init(void)
{
#if CONFIG_EMB_LOG_THREAD
    emb_sem_attr_t sa;
    emb_thread_attr_t ta;
    emb_thread_t th;
    emb_sem_attr_binary(&sa, true); /* available: the thread first sends what came before it */
    sa.name = "emb_log";
    (void)emb_check_status(emb_sem_init(&log_sem_storage, &sa, &log_sem), "emb_log semaphore");
    log_ready = true;
#endif
#if CONFIG_EMB_LOG_DEFERRED
    put_start();
#endif
#if CONFIG_EMB_LOG_THREAD
    emb_thread_attr_default(&ta);
    ta.name = "emb_log";
    ta.stack = log_stack;
    ta.stack_size = sizeof(log_stack);
    ta.priority = LOG_PRIORITY;
    (void)emb_check_status(emb_thread_init(&log_thread_storage, &ta, log_thread, NULL, &th),
                           "emb_log thread");
    (void)emb_check_status(emb_thread_start(th), "emb_log thread");
#endif
}
EMB_INIT_TABLE_ENTRY(log_init);

#endif /* CONFIG_EMB_LOG */
