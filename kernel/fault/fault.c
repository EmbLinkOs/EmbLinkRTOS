/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Kernel faults (03 §10, SPEC-001 §5.3): record, hook, act. The crash record and the
 * retained-RAM formats of SPEC-015 come with the observability milestone; here the
 * hook receives the information and the configured action follows.
 */
#include <emb/fault.h>
#include <emb/kernel.h>

#include <embk/kernel.h>

static emb_fault_hook_t fault_hook; /* lock: set before start or from thread context only */

void emb_fault_set_hook(emb_fault_hook_t hook)
{
    fault_hook = hook;
}

static EMB_NORETURN void act(const emb_fault_info_t *info)
{
#if CONFIG_EMB_FAULT_HALT_FOR_DEBUG
    emb_arch_breakpoint();
#endif
#if CONFIG_EMB_FAULT_ACTION_REBOOT
    emb_arch_reset(info);
#else
    emb_arch_fault_halt(info);
#endif
}

void embk_fault_raise(uint8_t fault_class, uint16_t code, uint8_t argument, const char *where)
{
    emb_fault_info_t info;
    (void)emb_arch_irq_lock(); /* never unmasked again: the fault path does not return */
    info.fault_class = fault_class;
    info.argument = argument;
    info.code = code;
    info.api_id = 0u;
    info.thread_index = (embk_cpu.kernel_state == EMBK_KERNEL_RUNNING &&
                         embk_cpu.irq_nesting_depth == 0u && embk_cpu.current != NULL)
                            ? embk_thread_index(embk_cpu.current)
                            : 0xFFu;
    info.context = emb_context();
    info.address = 0u;
    info.pc = 0u;
    info.sp = 0u;
    info.where = where;
    EMBK_TRACE(EMB_TRACE_FAULT, fault_class, code, 0u);
    if (fault_hook != NULL) {
        fault_hook(&info);
    }
    act(&info);
}

void emb_fault(uint8_t fault_class, uint16_t code, const char *where)
{
    embk_fault_raise(fault_class, code, 0u, where);
}

emb_status_t emb_check_status(emb_status_t status, const char *what)
{
    if (status != EMB_OK) {
        embk_fault_raise(EMB_FAULT_CHECK, (uint16_t)(-status), 0u, what);
    }
    return status;
}
