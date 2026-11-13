/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_stack_limit.c
 * @brief   The hardware stack limit (KRN-MEM-010, A80), on Armv8-M Mainline.
 *
 * @details
 * A thread that runs past its stack's base takes a STKOF UsageFault at the first push below PSPLIM,
 * before it writes beyond its stack. The fault is the stack overflow class, with the fault status
 * registers in the crash record. The first boot overflows with a hook that resets the part; the
 * second finds the record.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#include "crash_print.h"

#include <emb/arch.h>
#include <emb/crash.h>

#include <emb_test.h>

static void reset_now(const emb_fault_info_t *info)
{
    (void)info;
    emb_arch_system_reset(); /* the record is already written */
}

/* far more frames than any test stack holds; volatile, so the recursion is not unbounded to the
 * compiler and is not folded away */
static volatile uint32_t depth_bound = 1000000u;

static uint32_t dive(uint32_t n)
{
    volatile uint8_t pad[64];
    pad[0] = (uint8_t)n;
    if (n >= depth_bound) {
        return pad[0];
    }
    return dive(n + 1u) + pad[0];
}

static void overflow(void *arg)
{
    (void)arg;
    (void)dive(0u);
}

EMB_TEST(stack_limit_stops_an_overflowing_thread)
{
    emb_crash_record_t r;
    EMB_TEST_REQ("KRN-MEM-010", "OBS-016");
    if (emb_crash_record_get(&r) != EMB_OK || r.fault_class != EMB_FAULT_STACK_OVERFLOW) {
        emb_board_puts("stack_limit_stops_an_overflowing_thread: first boot, overflowing\n");
        emb_fault_set_hook(reset_now);
        (void)emb_test_thread(EMB_TEST_NAME("deep"), 3u, 0u, overflow, NULL); /* preempts now */
        EMB_ASSERT_TRUE(false); /* not reached: the overflow resets the part */
    }
    EMB_ASSERT_EQ(r.fault_class, EMB_FAULT_STACK_OVERFLOW);
    EMB_ASSERT_EQ(r.code, 6u); /* the UsageFault exception */
    EMB_ASSERT_EQ(r.status_count, 4u);
    EMB_ASSERT_EQ(r.gpr_count, 0u);                  /* the frame was not stacked */
    EMB_ASSERT_TRUE((r.regs[0] & (1u << 20)) != 0u); /* CFSR.STKOF */
    EMB_ASSERT_EQ(r.pc, 0u);
    crash_print(&r);
    EMB_ASSERT_OK(emb_crash_record_clear());
}
