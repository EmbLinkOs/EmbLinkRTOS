/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Native port controls for test harnesses and samples (SPEC-013 §5, §6, §9).
 * Everything here runs under the gate: call it from simulated code only.
 */
#ifndef EMB_NATIVE_H
#define EMB_NATIVE_H

#include <emb/compiler.h>
#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMB_NATIVE_EXIT_FAULT \
    2 /* process exit code after a kernel fault (CONFIG_EMB_FAULT_ACTION) */
#define EMB_NATIVE_EXIT_DEADLOCK 3 /* nothing runnable, no pending event, no deadline (SIM-005) */

/* Queue interrupt @irq; delivered at the next gate crossing, or now when unmasked. */
void emb_native_irq_raise(emb_irq_t irq);

/* An explicit gate crossing: pending interrupts are delivered here when unmasked. */
void emb_native_yield_point(void);

/* End the run with @code as the process exit status. */
EMB_NORETURN void emb_native_exit(int code);

/* Called when the simulated CPU is idle with nothing pending and no deadline (SIM-005);
 * the default reports a deadlock and exits with EMB_NATIVE_EXIT_DEADLOCK. */
void emb_native_set_idle_hook(void (*hook)(void));

/* The virtual clock in raw units (ticks). */
uint64_t emb_native_virtual_now(void);

/* Number of host-level context switches performed so far. */
uint64_t emb_native_switch_count(void);

#ifdef __cplusplus
}
#endif

#endif /* EMB_NATIVE_H */
