/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    trace.h
 * @brief   The trace ring's events by number, for the dump, the stream and the tests (A128).
 *
 * @see     subsys/trace/ring.c
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#ifndef EMBS_TRACE_H
#define EMBS_TRACE_H

#include <emb/trace.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The number the next event will have: events are numbered from emb_trace_start(). */
uint32_t embs_trace_next(void);

/* Copies event @seq as it is sent (20 bytes, little-endian: time, id, cpu, context, a0 to a2);
 * false when the ring no longer holds it, or never did. */
bool embs_trace_read(uint32_t seq, uint8_t out[EMB_TRACE_RECORD_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* EMBS_TRACE_H */
