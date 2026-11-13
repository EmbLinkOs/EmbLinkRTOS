/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    wallclock.c
 * @brief   The wall clock (SPEC-003 §10, KRN-TIM-031, A123).
 *
 * @details
 * The monotonic clock in nanoseconds plus an offset, kept under the kernel's lock; nothing in the
 * kernel reads it.
 *
 * @see     include/emb/wallclock.h
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb/time.h>
#include <emb/wallclock.h>

#include <embk/kernel.h>

static int64_t wall_offset; /* lock domain: the critical section */
static uint8_t wall_set;

/* The monotonic clock in nanoseconds: 64-bit ticks (the option needs them) times the tick. */
static int64_t mono_ns(void)
{
    uint64_t ns = (uint64_t)emb_time_now().ticks * (uint64_t)CONFIG_EMB_TICK_NS;
    return (int64_t)ns;
}

emb_status_t emb_wallclock_get(int64_t *out_ns_since_epoch)
{
    emb_irq_key_t key;
    emb_status_t st = EMB_ENOTSET;
    EMBK_API_REQUIRE(EMB_API_ID_WALLCLOCK_GET, out_ns_since_epoch != NULL, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    key = embk_irq_lock();
    if (wall_set != 0u) {
        *out_ns_since_epoch = mono_ns() + wall_offset;
        st = EMB_OK;
    }
    embk_irq_unlock(key);
    return st;
}

emb_status_t emb_wallclock_set(int64_t ns_since_epoch)
{
    emb_irq_key_t key;
    if (ns_since_epoch < 0) {
        return EMB_EINVAL;
    }
    key = embk_irq_lock();
    wall_offset = ns_since_epoch - mono_ns();
    wall_set = 1u;
    embk_irq_unlock(key);
    return EMB_OK;
}

emb_status_t emb_wallclock_adjust(int64_t delta_ns)
{
    emb_irq_key_t key = embk_irq_lock();
    emb_status_t st = EMB_ENOTSET;
    if (wall_set != 0u) {
        int64_t now = mono_ns() + wall_offset;
        if ((delta_ns < 0) && (now < -delta_ns)) {
            st = EMB_EINVAL; /* before the epoch */
        } else {
            wall_offset += delta_ns;
            st = EMB_OK;
        }
    }
    embk_irq_unlock(key);
    return st;
}
