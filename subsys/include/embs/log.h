/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    log.h
 * @brief   The log ring's steps, for the subsystem's tests (A127).
 *
 * @details
 * emb_log_write() is a reservation, the record's bytes, and a commit; the log thread takes
 * records and, with the plain-text backend, formats each as a line. These are those steps on
 * their own, so a test can hold a record uncommitted while an interrupt writes after it, or read
 * the ring itself. Not an application interface.
 *
 * @see     subsys/logging/log.c
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#ifndef EMBS_LOG_H
#define EMBS_LOG_H

#include <emb/log.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reserves a record of len bytes (a dropped record first, written and committed, when records
 * were lost); *pos is the record's. False when it does not fit: dropped and counted. */
bool embs_log_reserve(uint32_t len, uint32_t *pos);

/* Writes len bytes of the reserved record at pos, from its first byte. */
void embs_log_put(uint32_t pos, const void *bytes, uint32_t len);

/* Commits the record of len bytes at pos. */
void embs_log_commit(uint32_t pos, uint32_t len);

/* Takes the oldest record, if committed and no longer than cap; its length, else 0. */
size_t embs_log_take(uint8_t *buf, size_t cap);

/* The identifier's width in a record: 2 or 4 with the deferred backend, a pointer's with the
 * plain-text one. */
uint32_t embs_log_id_bytes(void);

#if CONFIG_EMB_LOG_TEXT
/* Formats a record as the plain-text backend sends it, the newline included; its length. */
size_t embs_log_line(const uint8_t *rec, size_t n, char *line, size_t cap);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMBS_LOG_H */
