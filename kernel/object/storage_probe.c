/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Layout probe for the generated <emb/storage.h> (SPEC-001 §6.2, SPEC-009, ADR-006).
 * Compiled with the real configuration, never linked into an image. Each PROBE writes
 * the size and alignment of a kernel object into the object file as a text marker
 * (EMB_LAYOUT_MARKER), which tools/storage/gen_storage.py reads from an ELF, Mach-O,
 * or COFF object alike, for the host or a cross target.
 */
#include <embk/ipc.h>
#include <embk/kernel.h>
#include <embk/sync.h>
#include <embk/timer.h>
#include <embk/work.h>

#include <stdalign.h>

#ifndef EMB_LAYOUT_MARKER
#error "the compiler header does not define EMB_LAYOUT_MARKER: storage.h cannot be generated"
#endif

#define PROBE(name, type) EMB_LAYOUT_MARKER(name, sizeof(type), alignof(type))

void embk_storage_probe(void);

void embk_storage_probe(void)
{
    PROBE(thread, embk_thread_t);
#if CONFIG_EMB_SEM
    PROBE(sem, embk_sem_t);
#endif
#if CONFIG_EMB_MUTEX
    PROBE(mutex, embk_mutex_t);
#endif
#if CONFIG_EMB_EVENT
    PROBE(event, embk_event_t);
#endif
#if CONFIG_EMB_COND
    PROBE(cond, embk_cond_t);
#endif
#if CONFIG_EMB_MSGQ
    PROBE(msgq, embk_msgq_t);
#endif
#if CONFIG_EMB_WORKQ
    PROBE(work, embk_work_t);
    PROBE(workq, embk_workq_t);
#endif
#if CONFIG_EMB_TIMER
    PROBE(timer, embk_timer_t);
#endif
}
