/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    frame.c
 * @brief   Framed byte streams (SPEC-015 §7, OBS-019).
 *
 * @details
 * COBS over the channel byte, the payload and a CRC-16, ended by a zero. Consistent Overhead Byte
 * Stuffing (Cheshire and Baker, 1999) writes each run of up to 254 nonzero bytes after a code byte
 * holding the run's length plus one; a code below 0xFF stands for a zero after its run, except at
 * the frame's end.
 *
 * @see     include/emb/frame.h
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#include <emb/frame.h>

#define COBS_RUN_MAX 0xFFu /* a code byte: 254 data bytes and no zero after them */
#define CRC_INIT     0xFFFFu
#define CRC_POLY     0x1021u

/* CRC-16/IBM-3740, bit by bit: small rather than fast; a frame's CRC is O(len) either way. */
static uint16_t crc16(uint16_t crc, uint8_t byte)
{
    unsigned bit;
    uint16_t c = (uint16_t)(crc ^ (uint16_t)((uint16_t)byte << 8));
    for (bit = 0u; bit < 8u; bit++) { /* bounded: eight bits */
        if ((c & 0x8000u) != 0u) {
            c = (uint16_t)((uint16_t)(c << 1) ^ CRC_POLY);
        } else {
            c = (uint16_t)(c << 1);
        }
    }
    return c;
}

/* The encoder's state: where the open run's code byte goes, and the run's code so far. */
typedef struct cobs {
    uint8_t *out;
    size_t pos;  /* the next byte to write */
    size_t code; /* the open run's code byte */
    uint8_t run; /* the open run's code: its length plus one */
} cobs_t;

static void cobs_put(cobs_t *c, uint8_t byte)
{
    if (byte == 0u) {
        c->out[c->code] = c->run; /* the run ends at the zero */
        c->code = c->pos;
        c->pos++;
        c->run = 1u;
        return;
    }
    c->out[c->pos] = byte;
    c->pos++;
    c->run++;
    if (c->run == COBS_RUN_MAX) { /* a full run: it closes with no zero after it */
        c->out[c->code] = c->run;
        c->code = c->pos;
        c->pos++;
        c->run = 1u;
    }
}

emb_status_t emb_frame_encode(uint8_t channel, const void *payload, size_t len, uint8_t *out,
                              size_t cap, size_t *out_len)
{
    const uint8_t *p = (const uint8_t *)payload;
    cobs_t c;
    uint16_t crc = CRC_INIT;
    size_t i;
    if ((channel > EMB_FRAME_CHANNEL_MAX) || (out == NULL) || (out_len == NULL) ||
        ((payload == NULL) && (len != 0u))) {
        return EMB_EINVAL;
    }
    /* the bound is computed so that it cannot wrap: a payload this long is refused first */
    if ((len > (SIZE_MAX / 2u)) || (cap < EMB_FRAME_ENCODED_MAX(len))) {
        return EMB_EOVERFLOW;
    }
    c.out = out;
    c.code = 0u;
    c.pos = 1u;
    c.run = 1u;
    crc = crc16(crc, channel);
    cobs_put(&c, channel);
    for (i = 0u; i < len; i++) { /* bounded by len */
        crc = crc16(crc, p[i]);
        cobs_put(&c, p[i]);
    }
    cobs_put(&c, (uint8_t)(crc & 0xFFu));
    cobs_put(&c, (uint8_t)(crc >> 8));
    out[c.code] = c.run;
    out[c.pos] = 0u; /* the delimiter */
    *out_len = c.pos + 1u;
    return EMB_OK;
}

/* The frame's bytes before COBS, read in place: the channel, the payload, the CRC (LSB first). */
typedef struct frame_src {
    const uint8_t *payload;
    size_t len;   /* the payload's */
    uint8_t head; /* the channel */
    uint8_t crc[2];
} frame_src_t;

static uint8_t src_byte(const frame_src_t *s, size_t i)
{
    uint8_t b;
    if (i == 0u) {
        b = s->head;
    } else if (i <= s->len) {
        b = s->payload[i - 1u];
    } else {
        b = s->crc[i - 1u - s->len];
    }
    return b;
}

/* Sends the source's bytes [from, to): the payload's part as one piece, read in place. */
static void src_send(const frame_src_t *s, size_t from, size_t to, emb_frame_sink_t sink, void *ctx)
{
    size_t lo = from;
    size_t hi;
    if ((lo == 0u) && (to > 0u)) {
        sink(ctx, &s->head, 1u);
        lo = 1u;
    }
    hi = (to < (s->len + 1u)) ? to : (s->len + 1u);
    if (lo < hi) {
        sink(ctx, &s->payload[lo - 1u], hi - lo);
        lo = hi;
    }
    if (lo < to) {
        sink(ctx, &s->crc[lo - 1u - s->len], to - lo);
    }
}

emb_status_t emb_frame_write(uint8_t channel, const void *payload, size_t len,
                             emb_frame_sink_t sink, void *ctx)
{
    frame_src_t s;
    uint16_t crc = CRC_INIT;
    size_t total;
    size_t i = 0u;
    size_t k;
    bool more = true;
    static const uint8_t delimiter = 0u;
    if ((channel > EMB_FRAME_CHANNEL_MAX) || (sink == NULL) || ((payload == NULL) && (len != 0u)) ||
        (len > (SIZE_MAX / 2u))) {
        return EMB_EINVAL;
    }
    s.payload = (const uint8_t *)payload;
    s.len = len;
    s.head = channel;
    crc = crc16(crc, channel);
    for (k = 0u; k < len; k++) { /* bounded by len */
        crc = crc16(crc, s.payload[k]);
    }
    s.crc[0] = (uint8_t)(crc & 0xFFu);
    s.crc[1] = (uint8_t)(crc >> 8);
    total = len + EMB_FRAME_OVERHEAD;
    /* One run per pass, as cobs_put() makes them: its code byte, then its nonzero bytes. A run
     * that a zero ended skips the zero; a full run skips nothing, and when it is the last, an
     * empty run follows it, as the encoder writes. */
    while (more) { /* bounded: each pass moves i forward, and the last pass ends at total */
        size_t end = i;
        uint8_t code;
        while ((end < total) && ((end - i) < (size_t)(COBS_RUN_MAX - 1u)) &&
               (src_byte(&s, end) != 0u)) {
            end++; /* bounded by the run's 254 bytes */
        }
        code = (uint8_t)((end - i) + 1u);
        sink(ctx, &code, 1u);
        src_send(&s, i, end, sink, ctx);
        if (code == COBS_RUN_MAX) {
            i = end;
        } else if (end == total) {
            more = false;
        } else {
            i = end + 1u;
        }
    }
    sink(ctx, &delimiter, 1u);
    return EMB_OK;
}

void emb_frame_decoder_init(emb_frame_decoder_t *dec, uint8_t *buf, size_t cap)
{
    if (dec == NULL) {
        return;
    }
    dec->buf = buf;
    dec->cap = (buf != NULL) ? cap : 0u;
    dec->len = 0u;
    dec->code = 0u;
    dec->left = 0u;
    dec->dropping = 0u;
    dec->reserved_ = 0u;
    dec->frames = 0u;
    dec->errors = 0u;
    dec->overruns = 0u;
}

static void keep(emb_frame_decoder_t *dec, uint8_t byte)
{
    if (dec->dropping != 0u) {
        return;
    }
    if (dec->len >= dec->cap) {
        dec->dropping = 1u;
        dec->overruns++;
        return;
    }
    dec->buf[dec->len] = byte;
    dec->len++;
}

/* A frame ended at a delimiter: whether it is good, and if so, the frame. */
static bool frame_end(emb_frame_decoder_t *dec, emb_frame_t *frame)
{
    uint16_t crc = CRC_INIT;
    size_t i;
    size_t n = dec->len;
    if (dec->dropping != 0u) {
        return false; /* counted when it overran */
    }
    if (dec->code == 0u) {
        return false; /* no frame: a zero between frames */
    }
    if ((dec->left != 0u) || (n < EMB_FRAME_OVERHEAD)) {
        dec->errors++; /* a block cut short, or too short to hold a channel and a CRC */
        return false;
    }
    for (i = 0u; i < (n - 2u); i++) { /* bounded by the frame's length */
        crc = crc16(crc, dec->buf[i]);
    }
    if (((uint8_t)(crc & 0xFFu) != dec->buf[n - 2u]) || ((uint8_t)(crc >> 8) != dec->buf[n - 1u]) ||
        (dec->buf[0] > EMB_FRAME_CHANNEL_MAX)) {
        dec->errors++;
        return false;
    }
    frame->channel = dec->buf[0];
    frame->payload = &dec->buf[1];
    frame->len = n - EMB_FRAME_OVERHEAD;
    dec->frames++;
    return true;
}

bool emb_frame_decode_byte(emb_frame_decoder_t *dec, uint8_t byte, emb_frame_t *frame)
{
    if ((dec == NULL) || (frame == NULL)) {
        return false;
    }
    if (byte == 0u) {
        bool good = frame_end(dec, frame);
        dec->len = 0u;
        dec->code = 0u;
        dec->left = 0u;
        dec->dropping = 0u;
        return good;
    }
    if (dec->left == 0u) { /* a code byte: the zero the last run stood for comes first */
        if ((dec->code != 0u) && (dec->code != COBS_RUN_MAX)) {
            keep(dec, 0u);
        }
        dec->code = byte;
        dec->left = (uint8_t)(byte - 1u);
        return false;
    }
    keep(dec, byte);
    dec->left--;
    return false;
}
