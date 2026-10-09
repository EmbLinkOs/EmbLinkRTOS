/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Kernel faults (SPEC-001 §5.3, SPEC-002 §9, 03 §10). A fault records the class and
 * location, calls the registered hook, and then takes CONFIG_EMB_FAULT_ACTION.
 * Misuse classes are raised in checked builds where a release build returns a status.
 */
#ifndef EMB_FAULT_H
#define EMB_FAULT_H

#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum emb_fault_class {
    EMB_FAULT_NONE = 0,
    EMB_FAULT_API_CONTEXT = 1,      /* wrong execution context or sub-state for the call */
    EMB_FAULT_API_ARGUMENT = 2,     /* null pointer, out-of-range value, bad storage */
    EMB_FAULT_API_HANDLE = 3,       /* invalid handle */
    EMB_FAULT_API_OWNER = 4,        /* operation by a thread that is not allowed to */
    EMB_FAULT_API_LIFECYCLE = 5,    /* destroy with waiters, exit with owned mutexes, ... */
    EMB_FAULT_KERNEL_INVARIANT = 6, /* an invariant of a kernel protocol was violated */
    EMB_FAULT_STACK_OVERFLOW = 7,   /* guard words or limit register (KRN-MEM-010) */
    EMB_FAULT_IRQ_NESTING = 8,      /* nesting beyond CONFIG_EMB_IRQ_MAX_NESTING */
    EMB_FAULT_SPURIOUS_IRQ = 9,     /* an interrupt with no handler */
    EMB_FAULT_HARDWARE = 10,        /* architecture fault entry (SPEC-002 §9) */
    EMB_FAULT_CHECK = 11,           /* EMB_CHECK() saw a failure */
    EMB_FAULT_PI_DEPTH = 12,        /* inheritance chain deeper than CONFIG_EMB_PI_MAX_DEPTH */
    EMB_FAULT_LOCK_BUDGET = 13      /* critical section longer than its declared budget */
};

typedef struct emb_fault_info {
    uint8_t fault_class;  /* enum emb_fault_class */
    uint8_t argument;     /* offending argument index for API faults, 0 otherwise */
    uint16_t code;        /* class-specific code */
    uint16_t api_id;      /* identifier of the API function, 0 when not an API fault */
    uint8_t thread_index; /* index of the faulting thread, 0xFF in prekernel or ISR context */
    uint8_t context;      /* emb_context_t at the time of the fault */
    uintptr_t address;    /* faulting address for hardware faults, 0 otherwise */
    uintptr_t pc;         /* program counter where the port can provide it, 0 otherwise */
    uintptr_t sp;         /* stack pointer where the port can provide it, 0 otherwise */
    const char *where;    /* source location or expression, may be NULL in release builds */
} emb_fault_info_t;

typedef void (*emb_fault_hook_t)(const emb_fault_info_t *info);

/**
 * emb_fault_set_hook() - Register the function called on every kernel fault.
 * @hook: May run in any context and must be ISR-safe; NULL removes the hook.
 *
 * @ctx      prekernel thread
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      FLT-002
 * @since    0.2
 * @stable   yes
 */
void emb_fault_set_hook(emb_fault_hook_t hook);

/**
 * emb_fault() - Raise a kernel fault from application or subsystem code.
 * @fault_class: An enum emb_fault_class value.
 * @code:        Class-specific code.
 * @where:       Source location or description; may be NULL.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1) plus the hook
 * @owns     none
 * @config
 * @req      FLT-001
 * @since    0.2
 * @stable   yes
 */
EMB_NORETURN void emb_fault(uint8_t fault_class, uint16_t code, const char *where);

#ifdef __cplusplus
}
#endif

#endif /* EMB_FAULT_H */
