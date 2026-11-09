/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Framed byte streams (SPEC-015 §7, OBS-019): one frame is COBS(channel, payload, CRC-16)
 * and a zero delimiter, so a byte-stream transport (a UART, USB CDC, a network socket)
 * carries the log, the trace, the crash record, the shell and the update protocol over one
 * line, and a decoder that joins mid-stream finds the next frame at the next zero. */
#ifndef EMB_FRAME_H
#define EMB_FRAME_H

#include <emb/status.h>
#include <emb/types.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channels (SPEC-015 §7, AMENDMENTS-M4 A113): 0 to 4 are the system's, 5 to 15 the
 * application's. */
#define EMB_FRAME_CHANNEL_LOG       0u
#define EMB_FRAME_CHANNEL_TRACE     1u
#define EMB_FRAME_CHANNEL_CRASH     2u
#define EMB_FRAME_CHANNEL_SHELL     3u
#define EMB_FRAME_CHANNEL_UPDATE    4u /* SPEC-019 §6 */
#define EMB_FRAME_CHANNEL_APP_FIRST 5u
#define EMB_FRAME_CHANNEL_MAX       15u

/* The channel byte and the CRC-16 around a frame's payload, before COBS. */
#define EMB_FRAME_OVERHEAD 3u
/* The most bytes emb_frame_encode() writes for a payload of n bytes, the delimiter included:
 * COBS adds one code byte per 254 bytes, and one more. */
#define EMB_FRAME_ENCODED_MAX(n) \
    ((n) + EMB_FRAME_OVERHEAD + (((n) + EMB_FRAME_OVERHEAD) / 254u) + 2u)

/* A decoded frame: its payload points into the decoder's buffer, valid until the decoder is
 * given its next byte. */
typedef struct emb_frame {
    const uint8_t *payload;
    size_t len;
    uint8_t channel;
} emb_frame_t;

/* A stream decoder. Its fields are the decoder's; the counters may be read at any time. */
typedef struct emb_frame_decoder {
    uint8_t *buf;     /* the caller's: holds one decoded frame, channel and CRC included */
    size_t cap;       /* its size in bytes */
    size_t len;       /* bytes of the frame so far */
    uint8_t code;     /* the current COBS block's code, 0 before the first */
    uint8_t left;     /* bytes the block still holds */
    uint8_t dropping; /* the frame overran the buffer: skip to the delimiter */
    uint8_t reserved_;
    uint32_t frames;   /* good frames delivered */
    uint32_t errors;   /* frames dropped for a bad CRC, a channel above 15 or a cut block */
    uint32_t overruns; /* frames dropped for being longer than the buffer */
} emb_frame_decoder_t;

/**
 * emb_frame_encode() - Encode one frame: COBS(channel, payload, CRC-16) and a delimiter.
 * @channel: 0 to EMB_FRAME_CHANNEL_MAX.
 * @payload: The payload; may be NULL when @len is 0.
 * @len:     Its bytes.
 * @out:     Where the frame goes.
 * @cap:     @out's size; EMB_FRAME_ENCODED_MAX(@len) is always enough.
 * @out_len: Receives the frame's bytes, the delimiter included.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(len)
 * @owns     none
 * @config   CONFIG_EMB_FRAME
 * @req      OBS-019
 * @since    0.3
 * @stable   no
 *
 * The CRC is CRC-16/IBM-3740 (polynomial 0x1021, initial value 0xFFFF, not reflected) over
 * the channel byte and the payload, sent least significant byte first. No byte of the frame
 * but the last is zero.
 *
 * Return: EMB_OK; EMB_EINVAL (a channel above 15, or a pointer missing);
 * EMB_EOVERFLOW (@cap is too small for the frame).
 */
emb_status_t emb_frame_encode(uint8_t channel, const void *payload, size_t len, uint8_t *out,
                              size_t cap, size_t *out_len);

/**
 * emb_frame_decoder_init() - Start a stream decoder on a buffer.
 * @dec: The decoder.
 * @buf: The buffer a decoded frame is kept in: its payload plus EMB_FRAME_OVERHEAD bytes.
 * @cap: Its size in bytes.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_FRAME
 * @req      OBS-019
 * @since    0.3
 * @stable   no
 *
 * The decoder starts between frames, with its counters at zero. A NULL @dec is ignored.
 */
void emb_frame_decoder_init(emb_frame_decoder_t *dec, uint8_t *buf, size_t cap);

/**
 * emb_frame_decode_byte() - Give a stream decoder its next byte.
 * @dec:   The decoder.
 * @byte:  The byte, as the transport received it.
 * @frame: Receives the frame when this byte completes a good one.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(len) at a delimiter, for the CRC; O(1) otherwise
 * @owns     none
 * @config   CONFIG_EMB_FRAME
 * @req      OBS-019
 * @since    0.3
 * @stable   no
 *
 * A zero byte ends a frame. A frame with a bad CRC, a channel above 15 or a COBS block cut
 * short is dropped and counted in errors; one longer than the buffer is dropped and counted
 * in overruns; either way the decoder carries on at the next frame. Zeros between frames are
 * ignored, so a sender may start with one to resynchronize a decoder that joined mid-frame.
 *
 * Return: true when @byte completed a good frame, now in @frame; false otherwise, and false
 * for a NULL @dec or @frame.
 */
bool emb_frame_decode_byte(emb_frame_decoder_t *dec, uint8_t byte, emb_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* EMB_FRAME_H */
