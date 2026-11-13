/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_frame.c
 * @brief   Framed byte streams (SPEC-015 §7; OBS-019).
 *
 * @details
 * The encoder against vectors the host tool (tools/transport/frame.py) produces from the
 * specification, and the stream decoder's resynchronization, CRC and overrun handling.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#include <emb/frame.h>

#include <emb_test.h>
#include <string.h>

/* tools/transport/frame.py's encode(): the same bytes on both sides of the line */
static const uint8_t v_empty[] = {0x01u, 0x03u, 0xF0u, 0xE1u, 0x00u};
static const uint8_t v_zero[] = {0x02u, 0x01u, 0x03u, 0x3Eu, 0x2Eu, 0x00u};
static const uint8_t v_hello[] = {0x09u, 0x02u, 0x68u, 0x65u, 0x6Cu,
                                  0x6Cu, 0x6Fu, 0x32u, 0x46u, 0x00u};

static uint8_t payload[300];
static uint8_t wire[EMB_FRAME_ENCODED_MAX(300u) + 16u];
static uint8_t dbuf[300u + EMB_FRAME_OVERHEAD];
static emb_frame_decoder_t dec;

static size_t encode(uint8_t ch, const void *p, size_t len)
{
    size_t n = 0u;
    EMB_ASSERT_OK(emb_frame_encode(ch, p, len, wire, sizeof(wire), &n));
    return n;
}

/* Feeds n bytes; returns how many good frames came out, the last in *last. */
static unsigned feed(const uint8_t *bytes, size_t n, emb_frame_t *last)
{
    unsigned good = 0u;
    size_t i;
    emb_frame_t f;
    for (i = 0u; i < n; i++) { /* bounded by n */
        if (emb_frame_decode_byte(&dec, bytes[i], &f)) {
            good++;
            *last = f;
        }
    }
    return good;
}

EMB_TEST(frame_encoding_matches_the_host_tool)
{
    size_t n;
    EMB_TEST_REQ("OBS-019");
    n = encode(EMB_FRAME_CHANNEL_LOG, NULL, 0u);
    EMB_ASSERT_EQ(n, sizeof(v_empty));
    EMB_ASSERT_TRUE(memcmp(wire, v_empty, n) == 0);
    n = encode(EMB_FRAME_CHANNEL_TRACE, "\0", 1u);
    EMB_ASSERT_EQ(n, sizeof(v_zero));
    EMB_ASSERT_TRUE(memcmp(wire, v_zero, n) == 0);
    n = encode(EMB_FRAME_CHANNEL_CRASH, "hello", 5u);
    EMB_ASSERT_EQ(n, sizeof(v_hello));
    EMB_ASSERT_TRUE(memcmp(wire, v_hello, n) == 0);
}

EMB_TEST(frame_runs_at_the_cobs_boundary)
{
    size_t n;
    size_t i;
    EMB_TEST_REQ("OBS-019");
    (void)memset(payload, 0x11, sizeof(payload));
    n = encode(7u, payload, 300u); /* a full run of 254, then the rest */
    EMB_ASSERT_EQ(n, 306u);
    EMB_ASSERT_EQ(wire[0], 0xFFu);
    EMB_ASSERT_EQ(wire[255], 50u);
    EMB_ASSERT_EQ(wire[n - 3u], 0x0Bu); /* the CRC, low byte first */
    EMB_ASSERT_EQ(wire[n - 2u], 0xC9u);
    (void)memset(payload, 0x22, 253u);
    n = encode(5u, payload, 253u); /* the channel and 253 bytes fill a run exactly */
    EMB_ASSERT_EQ(n, 259u);
    EMB_ASSERT_EQ(wire[0], 0xFFu);
    EMB_ASSERT_EQ(wire[255], 0x03u);
    for (i = 0u; i + 1u < n; i++) { /* bounded by the frame: only its last byte is zero */
        if (wire[i] == 0u) {
            break;
        }
    }
    EMB_ASSERT_EQ(i, n - 1u);
    for (i = 0u; i <= 300u; i += 23u) { /* the bound holds for each length */
        EMB_ASSERT_TRUE(encode(9u, payload, i) <= EMB_FRAME_ENCODED_MAX(i));
    }
}

EMB_TEST(frame_round_trip_through_the_decoder)
{
    emb_frame_t f = {NULL, 0u, 0u};
    size_t n;
    size_t i;
    EMB_TEST_REQ("OBS-019");
    for (i = 0u; i < sizeof(payload); i++) { /* every byte value, zeros included */
        payload[i] = (uint8_t)(i * 7u);
    }
    emb_frame_decoder_init(&dec, dbuf, sizeof(dbuf));
    n = encode(EMB_FRAME_CHANNEL_MAX, payload, sizeof(payload));
    EMB_ASSERT_EQ(feed(wire, n, &f), 1u);
    EMB_ASSERT_EQ(f.channel, EMB_FRAME_CHANNEL_MAX);
    EMB_ASSERT_EQ(f.len, sizeof(payload));
    EMB_ASSERT_TRUE(memcmp(f.payload, payload, f.len) == 0);
    EMB_ASSERT_EQ(feed(v_empty, sizeof(v_empty), &f), 1u); /* an empty payload */
    EMB_ASSERT_EQ(f.len, 0u);
    EMB_ASSERT_EQ(feed(v_hello, sizeof(v_hello), &f), 1u);
    EMB_ASSERT_EQ(f.channel, EMB_FRAME_CHANNEL_CRASH);
    EMB_ASSERT_TRUE(f.len == 5u && memcmp(f.payload, "hello", 5u) == 0);
    EMB_ASSERT_EQ(dec.frames, 3u);
    EMB_ASSERT_EQ(dec.errors + dec.overruns, 0u);
}

EMB_TEST(frame_decoder_resynchronizes)
{
    static const uint8_t noise[] = {0x47u, 0x13u, 0x99u, 0x05u}; /* the tail of a lost frame */
    emb_frame_t f = {NULL, 0u, 0u};
    size_t n;
    EMB_TEST_REQ("OBS-019");
    emb_frame_decoder_init(&dec, dbuf, sizeof(dbuf));
    EMB_ASSERT_EQ(feed(noise, sizeof(noise), &f), 0u);     /* joined mid-frame */
    EMB_ASSERT_EQ(feed(v_hello, sizeof(v_hello), &f), 0u); /* the noise spoils this one */
    EMB_ASSERT_EQ(dec.errors, 1u);
    EMB_ASSERT_EQ(feed(v_hello, sizeof(v_hello), &f), 1u);      /* and the next is good */
    EMB_ASSERT_EQ(feed((const uint8_t *)"\0\0\0", 3u, &f), 0u); /* zeros between frames */
    EMB_ASSERT_EQ(dec.errors, 1u);

    n = encode(EMB_FRAME_CHANNEL_LOG, "abc", 3u);
    wire[2] ^= 0x01u; /* one bit flipped on the line */
    EMB_ASSERT_EQ(feed(wire, n, &f), 0u);
    EMB_ASSERT_EQ(dec.errors, 2u);
    n = encode(EMB_FRAME_CHANNEL_LOG, "abc", 3u);
    EMB_ASSERT_EQ(feed(wire, n - 3u, &f), 0u); /* cut before its CRC and delimiter */
    EMB_ASSERT_EQ(feed((const uint8_t *)"\0", 1u, &f), 0u);
    EMB_ASSERT_EQ(dec.errors, 3u);
    EMB_ASSERT_EQ(feed(v_zero, sizeof(v_zero), &f), 1u);
    EMB_ASSERT_TRUE(f.len == 1u && f.payload[0] == 0u);

    /* a frame whose CRC is right but whose channel byte is above 15 */
    wire[0] = 0x04u;
    wire[1] = 0x10u;
    wire[2] = 0xC1u; /* CRC-16/IBM-3740 of 0x10 is 0xF3C1: sent C1 F3 */
    wire[3] = 0xF3u;
    wire[4] = 0x00u;
    EMB_ASSERT_EQ(feed(wire, 5u, &f), 0u);
    EMB_ASSERT_EQ(dec.errors, 4u);
}

EMB_TEST(frame_decoder_drops_an_overrun)
{
    static uint8_t small[8];
    emb_frame_t f = {NULL, 0u, 0u};
    size_t n;
    EMB_TEST_REQ("OBS-019");
    emb_frame_decoder_init(&dec, small, sizeof(small));
    (void)memset(payload, 0x33, 20u);
    n = encode(6u, payload, 20u); /* 23 bytes decoded: more than the buffer holds */
    EMB_ASSERT_EQ(feed(wire, n, &f), 0u);
    EMB_ASSERT_EQ(dec.overruns, 1u);
    EMB_ASSERT_EQ(dec.errors, 0u);
    EMB_ASSERT_EQ(feed(v_hello, sizeof(v_hello), &f), 1u); /* 8 bytes: it fits */
    EMB_ASSERT_EQ(dec.frames, 1u);
}

EMB_TEST(frame_misuse_is_refused)
{
    size_t n = 0u;
    emb_frame_t f;
    EMB_TEST_REQ("OBS-019");
    EMB_ASSERT_STATUS(emb_frame_encode(16u, NULL, 0u, wire, sizeof(wire), &n), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_frame_encode(0u, NULL, 1u, wire, sizeof(wire), &n), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_frame_encode(0u, NULL, 0u, NULL, sizeof(wire), &n), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_frame_encode(0u, NULL, 0u, wire, sizeof(wire), NULL), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_frame_encode(0u, "hello", 5u, wire, 9u, &n), EMB_EOVERFLOW);
    EMB_ASSERT_OK(emb_frame_encode(0u, "hello", 5u, wire, EMB_FRAME_ENCODED_MAX(5u), &n));
    emb_frame_decoder_init(NULL, dbuf, sizeof(dbuf)); /* ignored */
    EMB_ASSERT_TRUE(!emb_frame_decode_byte(NULL, 0u, &f));
    EMB_ASSERT_TRUE(!emb_frame_decode_byte(&dec, 0u, NULL));
}

/* A fixed-seed generator: the same stream on every target (Numerical Recipes' LCG). */
static uint32_t rng_state;

static uint32_t rng(void)
{
    rng_state = (rng_state * 1664525u) + 1013904223u;
    return rng_state >> 8;
}

EMB_TEST(frame_random_stream_with_damage)
{
    emb_frame_t f = {NULL, 0u, 0u};
    unsigned sent_good = 0u;
    unsigned got = 0u;
    unsigned k;
    EMB_TEST_REQ("OBS-019");
    rng_state = 0x0E3B1u;
    emb_frame_decoder_init(&dec, dbuf, sizeof(dbuf));
    for (k = 0u; k < 200u; k++) { /* bounded: 200 frames */
        size_t len = (size_t)(rng() % 65u);
        size_t i;
        size_t n;
        uint8_t ch = (uint8_t)(rng() % 16u);
        for (i = 0u; i < len; i++) { /* bounded by len */
            payload[i] = (uint8_t)rng();
        }
        n = encode(ch, payload, len);
        if ((rng() % 8u) == 0u) { /* one frame in eight damaged on the line */
            size_t at = (size_t)(rng() % (uint32_t)(n - 1u));
            wire[at] = (uint8_t)(wire[at] ^ (uint8_t)(1u + (rng() % 255u)));
        } else {
            sent_good++;
        }
        got += feed(wire, n, &f);
        if (got > 0u && f.len == len && f.channel == ch) {
            EMB_ASSERT_TRUE(memcmp(f.payload, payload, len) == 0);
        }
    }
    /* every undamaged frame is recovered, and no damaged one is taken for good */
    EMB_ASSERT_EQ(got, sent_good);
    EMB_ASSERT_EQ(dec.frames, sent_good);
    EMB_ASSERT_TRUE(dec.errors > 0u);
}

/* emb_frame_write()'s sink: collects the frame, and counts the calls. */
static uint8_t streamed[EMB_FRAME_ENCODED_MAX(300u) + 16u];
static size_t streamed_len;
static unsigned sink_calls;

static void collect(void *ctx, const uint8_t *bytes, size_t len)
{
    (void)ctx;
    if ((streamed_len + len) <= sizeof(streamed)) {
        (void)memcpy(&streamed[streamed_len], bytes, len);
    }
    streamed_len += len;
    sink_calls++;
}

EMB_TEST(frame_write_streams_the_encoders_bytes)
{
    /* lengths around COBS's 254-byte run, with no zero, zeros inside, and a zero last */
    static const size_t lens[] = {0u, 1u, 2u, 250u, 251u, 252u, 253u, 254u, 255u, 300u};
    size_t li;
    unsigned pattern;
    EMB_TEST_REQ("OBS-019");
    for (pattern = 0u; pattern < 3u; pattern++) {                    /* bounded: three patterns */
        for (li = 0u; li < (sizeof(lens) / sizeof(lens[0])); li++) { /* bounded by the table */
            size_t len = lens[li];
            size_t i;
            size_t n;
            for (i = 0u; i < len; i++) { /* bounded by len */
                payload[i] = (uint8_t)(1u + (i % 255u));
                if ((pattern == 1u) && ((i % 97u) == 40u)) {
                    payload[i] = 0u;
                }
            }
            if ((pattern == 2u) && (len > 0u)) {
                payload[len - 1u] = 0u;
            }
            n = encode(EMB_FRAME_CHANNEL_SHELL, payload, len);
            streamed_len = 0u;
            sink_calls = 0u;
            EMB_ASSERT_OK(emb_frame_write(EMB_FRAME_CHANNEL_SHELL, payload, len, collect, NULL));
            EMB_ASSERT_EQ(streamed_len, n);
            EMB_ASSERT_TRUE(memcmp(streamed, wire, n) == 0);
        }
    }
    /* a run inside the payload goes to the sink in one piece, not byte by byte */
    for (li = 0u; li < 200u; li++) { /* bounded: 200 bytes */
        payload[li] = 0x55u;
    }
    sink_calls = 0u;
    streamed_len = 0u;
    EMB_ASSERT_OK(emb_frame_write(EMB_FRAME_CHANNEL_LOG, payload, 200u, collect, NULL));
    EMB_ASSERT_TRUE(sink_calls <= 6u);
}

EMB_TEST(frame_write_misuse_is_refused)
{
    EMB_TEST_REQ("OBS-019");
    EMB_ASSERT_STATUS(emb_frame_write(16u, payload, 1u, collect, NULL), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_frame_write(0u, NULL, 1u, collect, NULL), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_frame_write(0u, payload, 1u, NULL, NULL), EMB_EINVAL);
    streamed_len = 0u;
    EMB_ASSERT_OK(emb_frame_write(0u, NULL, 0u, collect, NULL));
    EMB_ASSERT_EQ(streamed_len, sizeof(v_empty));
}
