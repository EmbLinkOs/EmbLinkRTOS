/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The system work queue (SPEC-006 §4.3). Its own object file: an image links the queue,
 * its thread and its stack only when it references emb_workq_system(), whose init-table
 * entry then constructs it in emb_kernel_init() (ADR-048).
 */
#include <emb/kernel.h>
#include <emb/sched.h>
#include <emb/thread.h>
#include <emb/work.h>

#if CONFIG_EMB_SYSTEM_WORKQ

#if CONFIG_EMB_SYSTEM_WORKQ_PRIO == 0
#define SYSTEM_WORKQ_PRIO ((uint8_t)(EMB_PRIORITY_MAX - 1u)) /* the second-highest priority */
#else
#define SYSTEM_WORKQ_PRIO ((uint8_t)CONFIG_EMB_SYSTEM_WORKQ_PRIO)
EMB_STATIC_ASSERT(CONFIG_EMB_SYSTEM_WORKQ_PRIO < CONFIG_EMB_PRIORITY_COUNT,
                  "CONFIG_EMB_SYSTEM_WORKQ_PRIO is not an application priority");
#endif

static emb_workq_storage_t system_storage;
EMB_THREAD_STACK(system_stack, CONFIG_EMB_SYSTEM_WORKQ_STACK);
static emb_workq_t system_queue;

static void system_workq_init(void)
{
    emb_workq_attr_t attr;
    emb_workq_attr_default(&attr);
    attr.name = "sysworkq";
    attr.stack = system_stack;
    attr.stack_size = sizeof(system_stack);
    attr.priority = SYSTEM_WORKQ_PRIO;
    (void)emb_check_status(emb_workq_init(&system_storage, &attr, &system_queue),
                           "system work queue");
}
EMB_INIT_TABLE_ENTRY(system_workq_init);

emb_workq_t emb_workq_system(void)
{
    return system_queue;
}

#endif /* CONFIG_EMB_SYSTEM_WORKQ */
