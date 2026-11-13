/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    wdog.c
 * @brief   The software watchdog (FLT-004, A121).
 *
 * @details
 * A periodic timer, its callback in the timer interrupt, looks at every party watched and feeds the
 * hardware watchdog only when none is past its window. The first party found past it is written
 * into the crash record, and the feeding stops for good: the hardware watchdog resets the part.
 *
 * @see     include/emb/wdog.h
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/drv_wdt.h>
#include <emb/timer.h>
#include <emb/wdog.h>

#include <embk/crash.h>
#include <embk/kernel.h>
#include <embk/time.h>

#define NO_THREAD 0xFFu

static emb_wdog_party_t *parties; /* lock domain: the critical section */
static const emb_device_t *hw;    /* the hardware watchdog, once started */

static uint8_t current_thread(void)
{
    return ((embk_cpu.kernel_state == EMBK_KERNEL_RUNNING) && (embk_cpu.irq_nesting_depth == 0u) &&
            (embk_cpu.current != NULL))
               ? embk_thread_index(embk_cpu.current)
               : NO_THREAD;
}

/* The first party past its window, or NULL; in the critical section. */
static const emb_wdog_party_t *wdog_late(emb_tick_t now)
{
    const emb_wdog_party_t *p;
    for (p = parties; p != NULL; p = p->next_) { /* bounded: the parties watched */
        if ((emb_tick_t)(now - p->last_) > p->window_) {
            return p;
        }
    }
    return NULL;
}

/* A missed window, into the crash record: the party's id, and the thread that watched it. */
static void wdog_record(const emb_wdog_party_t *p)
{
#if CONFIG_EMB_CRASH_RECORD
    emb_fault_info_t info;
    info.fault_class = EMB_FAULT_WATCHDOG;
    info.argument = 0u;
    info.code = p->id_;
    info.api_id = 0u;
    info.thread_index = p->thread_;
    info.context = emb_context();
    info.address = 0u;
    info.pc = 0u;
    info.sp = 0u;
    info.where = NULL;
    embk_crash_record_write(&info, EMB_CRASH_RESET_WATCHDOG);
#else
    (void)p;
#endif
}

static void wdog_feeder(emb_timer_t timer, void *arg)
{
    static uint8_t missed; /* lock domain: the critical section */
    emb_irq_key_t key;
    (void)timer;
    (void)arg;
    key = embk_irq_lock();
    if (missed == 0u) {
        const emb_wdog_party_t *late = wdog_late(embk_time_ticks_locked());
        if (late == NULL) {
            (void)emb_drv_wdt_feed(hw);
        } else {
            missed = 1u; /* for good: the hardware watchdog resets the part */
            EMBK_TRACE(EMB_TRACE_FAULT, EMB_FAULT_WATCHDOG, late->id_, 0u);
            wdog_record(late);
        }
    }
    embk_irq_unlock(key);
}

emb_status_t emb_wdog_start(const emb_device_t *wdt, uint32_t timeout_ms)
{
    static emb_timer_storage_t feeder_storage;
    static emb_timer_t feeder;
    static uint8_t feeder_made;
    static uint8_t started; /* thread context only: start is never called from an interrupt */
    emb_timer_attr_t attr;
    emb_status_t st;
    uint32_t quarter = timeout_ms / 4u; /* the feeder's period */
    EMBK_API_REQUIRE(EMB_API_ID_WDOG_START, wdt != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_WDOG_START);
    if (timeout_ms < 4u) {
        return EMB_EINVAL; /* a quarter of it is the feeder's period, at least 1 ms */
    }
    if (started != 0u) {
        return EMB_ESTATE;
    }
    if (feeder_made == 0u) {
        emb_timer_attr_default(&attr);
        attr.name = "wdog";
        attr.fn = wdog_feeder;
        attr.flags = EMB_TIMER_ISR_CONTEXT;
        st = emb_timer_init(&feeder_storage, &attr, &feeder);
        if (st != EMB_OK) {
            return st;
        }
        feeder_made = 1u;
    }
    st = emb_drv_wdt_start(wdt, timeout_ms);
    if (st != EMB_OK) {
        return st; /* EMB_ESTATE, or EMB_EINVAL: a timeout it cannot count */
    }
    hw = wdt;
    started = 1u;
    return emb_timer_start_periodic(feeder, EMB_MS(quarter), EMB_MS(quarter));
}

emb_status_t emb_wdog_watch(emb_wdog_party_t *party, uint16_t id, uint32_t window_ms)
{
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_WDOG_WATCH, party != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_WDOG_WATCH);
    if (window_ms == 0u) {
        return EMB_EINVAL;
    }
    key = embk_irq_lock();
    if (party->watched_ != 0u) {
        embk_irq_unlock(key);
        return EMB_ESTATE;
    }
    party->window_ = emb_ticks_at_least(EMB_MS_TO_TICKS(window_ms));
    party->last_ = embk_time_ticks_locked();
    party->id_ = id;
    party->thread_ = current_thread();
    party->watched_ = 1u;
    party->next_ = parties;
    parties = party;
    embk_irq_unlock(key);
    return EMB_OK;
}

emb_status_t emb_wdog_unwatch(emb_wdog_party_t *party)
{
    emb_wdog_party_t **at;
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_WDOG_UNWATCH, party != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_WDOG_UNWATCH);
    key = embk_irq_lock();
    if (party->watched_ == 0u) {
        embk_irq_unlock(key);
        return EMB_ESTATE;
    }
    at = &parties;
    while ((*at != NULL) && (*at != party)) { /* bounded: the parties watched */
        at = &(*at)->next_;
    }
    if (*at == party) {
        *at = party->next_;
    }
    party->next_ = NULL;
    party->watched_ = 0u;
    embk_irq_unlock(key);
    return EMB_OK;
}

emb_status_t emb_wdog_checkin(emb_wdog_party_t *party)
{
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_WDOG_CHECKIN, party != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL,
                     1u);
    key = embk_irq_lock();
    if (party->watched_ == 0u) {
        embk_irq_unlock(key);
        return EMB_ESTATE;
    }
    party->last_ = embk_time_ticks_locked();
    embk_irq_unlock(key);
    return EMB_OK;
}
