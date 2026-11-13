/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    wdog.h
 * @brief   The software watchdog (FLT-004, SPEC-017 §8, A121).
 *
 * @details
 * It feeds the hardware watchdog only while every party it watches has checked in within its
 * window. A party that misses its window is written into the crash record (EMB_FAULT_WATCHDOG, its
 * id the code), and the hardware watchdog, no longer fed, resets the part: the service never resets
 * it itself.
 *
 * @see     kernel/fault/wdog.c
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#ifndef EMB_WDOG_H
#define EMB_WDOG_H

#include <emb/device.h>
#include <emb/status.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A party the service watches, in the caller's storage: a thread, a loop, an interrupt's work.
 * Its fields are the service's; zero it, or leave it static, before emb_wdog_watch(). */
typedef struct emb_wdog_party {
    struct emb_wdog_party *next_; /* the next party watched */
    emb_tick_t window_;           /* the longest it may go without a check-in, in ticks */
    emb_tick_t last_;             /* when it last checked in */
    uint16_t id_;                 /* the caller's name for it, the crash record's code */
    uint8_t thread_;              /* the thread that began watching it; 0xFF outside one */
    uint8_t watched_;             /* 1 while watched */
} emb_wdog_party_t;

/**
 * emb_wdog_start() - Start the hardware watchdog, and the service that feeds it.
 * @wdt:        A device of the watchdog class, the board's (EMB_SOC_WATCHDOG_DEVICE).
 * @timeout_ms: The hardware watchdog's timeout; the service looks at its parties, and feeds,
 *              every quarter of it.
 *
 * @ctx      thread
 * @blocks   no
 * @time     O(1); O(active timers) list lookup with CONFIG_EMB_OBJ_LIST
 * @owns     none
 * @config   CONFIG_EMB_WDOG
 * @req      FLT-004
 * @since    0.3
 * @stable   no
 *
 * The service is then the hardware watchdog's only feeder. It looks at its parties in the
 * timer interrupt, so a thread that runs away at any priority does not stop it. A party that
 * misses its window resets the part at most its window, a quarter of @timeout_ms, and
 * @timeout_ms after its last check-in. Once started, the service cannot be stopped, as the
 * hardware watchdog cannot.
 *
 * Return: EMB_OK; EMB_ESTATE (started already, or @wdt is not READY); EMB_EINVAL (a timeout
 * under 4 ms, or one the hardware watchdog cannot count: nothing is started).
 */
emb_status_t emb_wdog_start(const emb_device_t *wdt, uint32_t timeout_ms);

/**
 * emb_wdog_watch() - Watch a party: from now it must check in within every window.
 * @party:     The party's storage, kept until emb_wdog_unwatch().
 * @id:        The caller's name for it, which the crash record carries when it misses.
 * @window_ms: The longest it may go without a check-in.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WDOG
 * @req      FLT-004
 * @since    0.3
 * @stable   no
 *
 * Its first window begins now. Parties may be watched before emb_wdog_start().
 *
 * Return: EMB_OK; EMB_EINVAL (a window of 0); EMB_ESTATE (watched already).
 */
emb_status_t emb_wdog_watch(emb_wdog_party_t *party, uint16_t id, uint32_t window_ms);

/**
 * emb_wdog_unwatch() - Stop watching a party, a thread about to end, say.
 * @party: A party being watched.
 *
 * @ctx      thread prekernel
 * @blocks   no
 * @time     O(wdog_parties): the list walked to unlink it
 * @owns     none
 * @config   CONFIG_EMB_WDOG
 * @req      FLT-004
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ESTATE (not watched).
 */
emb_status_t emb_wdog_unwatch(emb_wdog_party_t *party);

/**
 * emb_wdog_checkin() - A party shows it made progress: its window begins again.
 * @party: A party being watched.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_WDOG
 * @req      FLT-004
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_ESTATE (not watched).
 */
emb_status_t emb_wdog_checkin(emb_wdog_party_t *party);

#ifdef __cplusplus
}
#endif

#endif /* EMB_WDOG_H */
