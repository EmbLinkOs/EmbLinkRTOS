/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_crash_reset.c
 * @brief   The crash record across a real reset (SPEC-015 §4, OBS-016, ARCH-AVR-008).
 *
 * @details
 * On the first boot the test raises a fault whose hook resets the part (emb_arch_system_reset),
 * after the kernel wrote the record and before the fault action; on the second boot the framework
 * runs the case again, finds the record, checks it and clears it. Built only where the runner shows
 * the second boot of the same image with RAM kept (EMB_BOARD_RESETS_IN). Its own image, since the
 * second boot runs every case of the image from the top.
 *
 * @author  Junior Deogracias
 * @date    2026-11-04 (created)
 */
#include "crash_print.h"

#include <emb/arch.h>
#include <emb/crash.h>

#include <emb_test.h>

#define SURVIVAL_CODE 0x5EEDu

static void reset_now(const emb_fault_info_t *info)
{
    (void)info;
    emb_arch_system_reset(); /* the record is already written */
}

EMB_TEST(crash_record_survives_reset)
{
    static emb_crash_record_t r; /* not on the runner's stack: 256 bytes on AVR */
    EMB_TEST_REQ("OBS-016", "KRN-IRQ-030", "ARCH-AVR-008");
    if (emb_crash_record_get(&r) != EMB_OK || r.code != SURVIVAL_CODE) {
        emb_board_puts("crash_record_survives_reset: first boot, faulting\n");
        emb_fault_set_hook(reset_now);
        emb_fault(EMB_FAULT_CHECK, SURVIVAL_CODE, "crash_record_survives_reset");
    }
    EMB_ASSERT_EQ(r.fault_class, EMB_FAULT_CHECK);
    EMB_ASSERT_EQ(r.code, SURVIVAL_CODE);
    EMB_ASSERT_EQ(r.api_id, EMB_API_ID_NONE);
    EMB_ASSERT_TRUE(r.thread_index != EMB_CRASH_THREAD_NONE);
    EMB_ASSERT_EQ(r.reset_reason,
                  CONFIG_EMB_FAULT_ACTION_REBOOT ? EMB_CRASH_RESET_REBOOT : EMB_CRASH_RESET_HALT);
    crash_print(&r);
    EMB_ASSERT_OK(emb_crash_record_clear());
    EMB_ASSERT_STATUS(emb_crash_record_get(&r), EMB_ENOENT);
}
