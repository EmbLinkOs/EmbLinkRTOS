/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    console.c
 * @brief   The console: the board's text, plain or framed (SPEC-015 §7, A127).
 *
 * @details
 * emb_board_puts() is the same on every board: plain, it writes the text as it is; framed, it
 * sends the text as one frame on the shell's channel. A frame is written through
 * emb_frame_write(), which reads its payload in place, so the console keeps no frame buffer.
 *
 * @see     include/emb/console.h
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/config.h>
#include <emb/console.h>
#include <emb/frame.h>

#include <emb_board.h>

#if CONFIG_EMB_CONSOLE_FRAMED
#include <emb/context.h>
#include <emb/irq.h>
#include <emb/kernel.h>
#include <emb/mutex.h>
#include <emb/sched.h>
#include <emb/storage.h>
#include <emb/time.h>

#include <stdbool.h>
#endif

#if CONFIG_EMB_CONSOLE_FRAMED

#if CONFIG_EMB_MUTEX_INHERIT
#define CONSOLE_PROTOCOL EMB_MUTEX_INHERIT
#else
#define CONSOLE_PROTOCOL EMB_MUTEX_NONE
#endif

/* One thread's frame at a time on the line. */
EMB_MUTEX_DEFINE(console_mutex, CONSOLE_PROTOCOL, 0u, 0u);

static void console_sink(void *ctx, const uint8_t *bytes, size_t len)
{
    (void)ctx;
    emb_board_write(bytes, len);
}

/* A thread outside any critical section waits for the console; anywhere else the frame goes at
 * once, since nothing there may wait. */
static bool console_may_wait(void)
{
    return (emb_context() == EMB_CONTEXT_THREAD) && !emb_irq_is_locked() &&
           (emb_sched_lock_depth() == 0u);
}

emb_status_t emb_console_frame(uint8_t channel, const void *payload, size_t len)
{
    static const uint8_t lead = 0u;
    bool locked = false;
    emb_status_t st;
    if ((channel > EMB_FRAME_CHANNEL_MAX) || ((payload == NULL) && (len != 0u))) {
        return EMB_EINVAL;
    }
    if (console_may_wait()) {
        locked = (emb_mutex_lock(console_mutex, EMB_WAIT_FOREVER) == EMB_OK);
    }
    emb_board_write(&lead, 1u); /* ends whatever came before that was not a frame */
    st = emb_frame_write(channel, payload, len, console_sink, NULL);
    if (locked) {
        (void)emb_mutex_unlock(console_mutex);
    }
    return st;
}

#else

emb_status_t emb_console_frame(uint8_t channel, const void *payload, size_t len)
{
    (void)channel;
    (void)payload;
    (void)len;
    return EMB_ENOTSUP;
}

#endif /* CONFIG_EMB_CONSOLE_FRAMED */

void emb_board_puts(const char *s)
{
    size_t n = 0u;
    if (s == NULL) {
        return;
    }
    while (s[n] != '\0') { /* bounded by the string */
        n++;
    }
#if CONFIG_EMB_CONSOLE_FRAMED
    (void)emb_console_frame(EMB_FRAME_CHANNEL_SHELL, s, n);
#else
    emb_board_write(s, n);
#endif
}
