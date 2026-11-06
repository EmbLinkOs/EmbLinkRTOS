/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * A hardware fault in the crash record, across a real reset (SPEC-015 §4, A74): the first
 * boot executes a trap instruction (EMB_TRAP: udf on Cortex-M; on RISC-V, ebreak from GCC and
 * unimp from Clang) with a fault hook that resets the part; the second boot finds the record with
 * the port's registers: the fault status registers and the stacked or trapped general registers.
 * Its own image, like test_crash_reset, since the second boot runs every case of the image from the
 * top.
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

EMB_TEST(crash_record_keeps_a_hardware_fault)
{
    emb_crash_record_t r;
    EMB_TEST_REQ("OBS-016", "KRN-IRQ-030");
    if (emb_crash_record_get(&r) != EMB_OK || r.fault_class != EMB_FAULT_HARDWARE) {
        emb_board_puts("crash_record_keeps_a_hardware_fault: first boot, trapping\n");
        emb_fault_set_hook(reset_now);
        EMB_TRAP();
    }
    EMB_ASSERT_EQ(r.fault_class, EMB_FAULT_HARDWARE);
    EMB_ASSERT_EQ(r.status_count, 4u);
    EMB_ASSERT_TRUE(r.pc != 0u);
#if CONFIG_EMB_ARCH_CORTEX_M
    /* CFSR, HFSR, MMFAR, BFAR; r0-r3, r12, lr, pc, xPSR */
    EMB_ASSERT_EQ(r.gpr_count, 8u);
    EMB_ASSERT_TRUE((r.regs[0] & (1u << 16)) != 0u); /* CFSR.UNDEFINSTR: the udf */
    EMB_ASSERT_EQ(r.regs[4 + 6], (uint32_t)r.pc);    /* the stacked pc */
    EMB_ASSERT_EQ(r.regs[4 + 5], r.lr);              /* the stacked lr */
#elif CONFIG_EMB_ARCH_RISCV
    /* mcause, mtval, mepc, mstatus; ra, a0-a7 */
    EMB_ASSERT_EQ(r.gpr_count, 9u);
    /* mcause: breakpoint (GCC's ebreak) or illegal instruction (Clang's unimp) */
    EMB_ASSERT_TRUE(r.regs[0] == 3u || r.regs[0] == 2u);
    EMB_ASSERT_EQ(r.regs[2], (uint32_t)r.pc); /* mepc */
    EMB_ASSERT_EQ(r.regs[4], r.lr);           /* ra */
#endif
    EMB_ASSERT_EQ(r.length, 92u + 4u * (uint32_t)(r.status_count + r.gpr_count));
    crash_print(&r);
    EMB_ASSERT_OK(emb_crash_record_clear());
}
