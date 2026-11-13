/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    console.h
 * @brief   The console: plain text, or every stream in frames (SPEC-015 §7, A127).
 *
 * @details
 * A board writes the console's bytes as they are (emb_board_write()); the board's text goes
 * through emb_board_puts(), which is common code. By default the console is plain text. With
 * CONFIG_EMB_CONSOLE_FRAMED everything on it travels in A113's frames, each preceded by a zero:
 * the board's text on channel 3, log records on 0, trace on 1, crash records on 2. A thread's
 * frame is written whole under the console's mutex; one from an interrupt, a fault, a critical
 * section or before the kernel starts is written at once, and may cut a thread's frame, which
 * the decoder then drops and counts.
 *
 * @see     subsys/console/console.c
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#ifndef EMB_CONSOLE_H
#define EMB_CONSOLE_H

#include <emb/status.h>
#include <emb/types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * emb_console_frame() - Send one frame on the framed console.
 * @channel: 0 to EMB_FRAME_CHANNEL_MAX (SPEC-015 §7, A113).
 * @payload: The payload; may be NULL when @len is 0.
 * @len:     Its bytes.
 *
 * @ctx      thread isr prekernel
 * @blocks   yes, in a thread outside a critical section: for the console, held by another
 *           thread's frame
 * @time     O(len) plus the console's line time
 * @owns     none
 * @config   CONFIG_EMB_CONSOLE_FRAMED
 * @req      OBS-019
 * @since    0.3
 * @stable   no
 *
 * The frame is preceded by a zero, so a decoder that saw bytes which were not a frame starts
 * this one clean. Polled: the caller sends the bytes.
 *
 * Return: EMB_OK; EMB_EINVAL (a channel above 15, or a payload missing); EMB_ENOTSUP when
 * the console is plain text.
 */
emb_status_t emb_console_frame(uint8_t channel, const void *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EMB_CONSOLE_H */
