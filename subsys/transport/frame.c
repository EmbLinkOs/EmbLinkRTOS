/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Framed byte streams (SPEC-015 §7, OBS-019): COBS over the channel byte, the payload and a
 * CRC-16, ended by a zero. Consistent Overhead Byte Stuffing (Cheshire and Baker, 1999) writes
 * each run of up to 254 nonzero bytes after a code byte holding the run's length plus one;
 * a code below 0xFF stands for a zero after its run, except at the frame's end.
 */
#include <emb/frame.h>

#define COBS_RUN_MAX 0xFFu /* a code byte: 254 data bytes and no zero after them */
#define CRC_INIT     0xFFFFu
#define CRC_POLY     0x1021u

/* CRC-16/IBM-3740, bit by bit: small rather than fast; a frame's CRC is O(len) either way. */
static uint16_t crc16(uint16_t crc, uint8_t byte)
{
    unsigned bit;
    crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)byte << 8));
    for (bit = 0u; bit < 8u; bit++) { /* bounded: eight bits */
        if ((crc & 0x8000u) != 0u) {
            crc = (uint16_t)((uint16_t)(crc << 1) ^ CRC_POLY);
        } else {
            crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
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
    if (channel > EMB_FRAME_CHANNEL_MAX || out == NULL || out_len == NULL ||
        (payload == NULL && len != 0u)) {
        return EMB_EINVAL;
    }
    /* the bound is computed so that it cannot wrap: a payload this long is refused first */
    if (len > (SIZE_MAX / 2u) || cap < EMB_FRAME_ENCODED_MAX(len)) {
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
    if (dec->left != 0u || n < EMB_FRAME_OVERHEAD) {
        dec->errors++; /* a block cut short, or too short to hold a channel and a CRC */
        return false;
    }
    for (i = 0u; i < n - 2u; i++) { /* bounded by the frame's length */
        crc = crc16(crc, dec->buf[i]);
    }
    if ((uint8_t)(crc & 0xFFu) != dec->buf[n - 2u] || (uint8_t)(crc >> 8) != dec->buf[n - 1u] ||
        dec->buf[0] > EMB_FRAME_CHANNEL_MAX) {
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
    bool good;
    if (dec == NULL || frame == NULL) {
        return false;
    }
    if (byte == 0u) {
        good = frame_end(dec, frame);
        dec->len = 0u;
        dec->code = 0u;
        dec->left = 0u;
        dec->dropping = 0u;
        return good;
    }
    if (dec->left == 0u) { /* a code byte: the zero the last run stood for comes first */
        if (dec->code != 0u && dec->code != COBS_RUN_MAX) {
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
