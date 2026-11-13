/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    descriptor.c
 * @brief   The debug descriptor (SPEC-015 §5, OBS-008, A107).
 *
 * @details
 * The kernel's private layout, written out as addresses and offsets for tools/descriptor and
 * debugger plugins. Nothing in the kernel reads it; emb_kernel_init() refers to it so that the
 * linker keeps it.
 *
 * @author  Junior Deogracias
 * @date    2026-11-08 (created)
 */
#include <emb/debug.h>
#include <emb/version.h>

#include <embk/kernel.h>
#include <embk/sched.h>
#include <embk/thread.h>
#if CONFIG_EMB_MUTEX
#include <embk/sync.h>
#endif
#if CONFIG_EMB_CRASH_RECORD
#include <embk/crash.h>
#endif
#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif

#include <emb_arch_debug.h>

#include <stddef.h>
#include <stdint.h>

/* The descriptor promises these, whatever the kernel's names for them. */
EMB_STATIC_ASSERT(offsetof(emb_debug_descriptor_t, value) == 16u, "the values start at byte 16");
EMB_STATIC_ASSERT(EMBK_THREAD_STARTED == EMB_DEBUG_TF_STARTED, "tflags");
EMB_STATIC_ASSERT(EMBK_THREAD_TERMINATED == EMB_DEBUG_TF_TERMINATED, "tflags");
EMB_STATIC_ASSERT(EMBK_THREAD_SUSPENDED == EMB_DEBUG_TF_SUSPENDED, "tflags");
EMB_STATIC_ASSERT(EMBK_THREAD_KERNEL == EMB_DEBUG_TF_KERNEL, "tflags");
EMB_STATIC_ASSERT(offsetof(embk_thread_t, arch) == 0u, "the saved sp comes first");

#define ABSENT           EMB_DEBUG_ABSENT
#define OFF(type, field) ((uintptr_t)offsetof(type, field))
#define ADDR(x)          ((uintptr_t)(x))

#ifdef EMB_ARCH_DEBUG_FRAME_TABLE
static const uint8_t frame_table[][2] = {EMB_ARCH_DEBUG_FRAME_TABLE};
#define FRAME_TABLE       ADDR(frame_table)
#define FRAME_TABLE_COUNT (sizeof(frame_table) / sizeof(frame_table[0]))
#else
#define FRAME_TABLE       ABSENT
#define FRAME_TABLE_COUNT 0u
#endif

#ifdef EMB_ARCH_DEBUG_ISR_STACK
#define ISR_STACK      ADDR(EMB_ARCH_DEBUG_ISR_STACK)
#define ISR_STACK_SIZE ((uintptr_t)(EMB_ARCH_DEBUG_ISR_STACK_SIZE))
#else
#define ISR_STACK      ABSENT
#define ISR_STACK_SIZE ABSENT
#endif

static const char port_name[] = CONFIG_EMB_ARCH;
static const char port_variant[] = CONFIG_EMB_ARCH_VARIANT;

/* The object type tags, EMBK_OBJ_IDLE first. */
static const char type_idle[] = "idle";
static const char type_thread[] = "thread";
static const char type_sem[] = "semaphore";
static const char type_mutex[] = "mutex";
static const char type_event[] = "event";
static const char type_cond[] = "condition";
static const char type_msgq[] = "message queue";
static const char type_workq[] = "work queue";
static const char type_timer[] = "timer";
static const char *const type_names[] = {type_idle, type_thread, type_sem,   type_mutex, type_event,
                                         type_cond, type_msgq,   type_workq, type_timer};
EMB_STATIC_ASSERT(EMBK_OBJ_TIMER - EMBK_OBJ_IDLE + 1 == 9, "one name per object type");

#define FLAG(cond, bit) ((cond) ? (uint32_t)(bit) : 0u)

EMB_USED const emb_debug_descriptor_t emb_debug_descriptor = {
    .magic = {EMB_DEBUG_MAGIC0, EMB_DEBUG_MAGIC1, EMB_DEBUG_MAGIC2, EMB_DEBUG_MAGIC3},
    .version = EMB_DEBUG_VERSION,
    .count = EMB_DEBUG_COUNT,
    .pointer_size = (uint8_t)sizeof(uintptr_t),
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    .endian = EMB_DEBUG_BIG,
#else
    .endian = EMB_DEBUG_LITTLE,
#endif
    .reserved = 0u,
    .flags = FLAG(CONFIG_EMB_CHECKED, EMB_DEBUG_F_CHECKED) |
             FLAG(CONFIG_EMB_SCHED_TABLE, EMB_DEBUG_F_SCHED_TABLE) |
             FLAG(CONFIG_EMB_THREAD_LIST, EMB_DEBUG_F_THREAD_LIST) |
             FLAG(CONFIG_EMB_OBJ_NAMES, EMB_DEBUG_F_OBJ_NAMES) |
             FLAG(CONFIG_EMB_OBJ_LIST, EMB_DEBUG_F_OBJ_LIST) |
             FLAG(CONFIG_EMB_STACK_STATS, EMB_DEBUG_F_STACK_STATS) |
             FLAG(CONFIG_EMB_STACK_CHECK, EMB_DEBUG_F_STACK_CHECK) |
             FLAG(EMBK_THREAD_INDEX_PACKED, EMB_DEBUG_F_INDEX_PACKED) |
             FLAG(CONFIG_EMB_IDLE_THREAD, EMB_DEBUG_F_IDLE_THREAD),
    .value =
        {
            [EMB_DEBUG_KERNEL_VERSION] = EMB_VERSION,
#if CONFIG_EMB_CRASH_RECORD
            [EMB_DEBUG_BUILD_ID] = ADDR(emb_build_id),
#else
            [EMB_DEBUG_BUILD_ID] = ABSENT,
#endif
            [EMB_DEBUG_PORT_NAME] = ADDR(port_name),
            [EMB_DEBUG_PORT_VARIANT] = ADDR(port_variant),
            [EMB_DEBUG_PRIORITY_COUNT] = CONFIG_EMB_PRIORITY_COUNT,

            [EMB_DEBUG_CPU] = ADDR(&embk_cpu),
            [EMB_DEBUG_CPU_CURRENT] = OFF(embk_cpu_t, current),
            [EMB_DEBUG_CPU_IRQ_NESTING] = OFF(embk_cpu_t, irq_nesting_depth),
            [EMB_DEBUG_CPU_SCHED_LOCK] = OFF(embk_cpu_t, sched_lock_depth),
            [EMB_DEBUG_CPU_RESCHEDULE] = OFF(embk_cpu_t, reschedule_pending),
            [EMB_DEBUG_CPU_STATE] = OFF(embk_cpu_t, kernel_state),

            [EMB_DEBUG_THREAD_SIZE] = sizeof(embk_thread_t),
#if CONFIG_EMB_THREAD_LIST
            [EMB_DEBUG_THREAD_LIST] = ADDR(&embk_thread_all.head),
            [EMB_DEBUG_THREAD_LIST_NODE] = OFF(embk_thread_t, all_node),
#else
            [EMB_DEBUG_THREAD_LIST] = ABSENT,
            [EMB_DEBUG_THREAD_LIST_NODE] = ABSENT,
#endif
#if CONFIG_EMB_SCHED_TABLE
            [EMB_DEBUG_THREAD_TABLE] = ADDR(embk_thread_table),
            [EMB_DEBUG_THREAD_TABLE_COUNT] = CONFIG_EMB_PRIORITY_COUNT - 1,
#else
            [EMB_DEBUG_THREAD_TABLE] = ABSENT,
            [EMB_DEBUG_THREAD_TABLE_COUNT] = 0u,
#endif
            [EMB_DEBUG_IDLE_THREAD] = ADDR(&embk_idle_thread),

            [EMB_DEBUG_T_SP] = OFF(embk_thread_t, arch.sp),
            [EMB_DEBUG_T_TYPE] = OFF(embk_thread_t, obj.type),
            [EMB_DEBUG_T_STATE] = OFF(embk_thread_t, obj.state),
            [EMB_DEBUG_T_FLAGS] = OFF(embk_thread_t, obj.flags),
#if CONFIG_EMB_OBJ_NAMES
            [EMB_DEBUG_T_NAME] = OFF(embk_thread_t, obj.name),
#else
            [EMB_DEBUG_T_NAME] = ABSENT,
#endif
            [EMB_DEBUG_T_BASE_PRIO] = OFF(embk_thread_t, base_prio),
            [EMB_DEBUG_T_EFF_PRIO] = OFF(embk_thread_t, eff_prio),
            [EMB_DEBUG_T_WAIT_STATE] = OFF(embk_thread_t, wait_state),
            [EMB_DEBUG_T_WAIT_REASON] = OFF(embk_thread_t, wait_reason),
            [EMB_DEBUG_T_TFLAGS] = OFF(embk_thread_t, tflags),
#if EMBK_THREAD_INDEX_PACKED
            [EMB_DEBUG_T_INDEX] = ABSENT,
#else
            [EMB_DEBUG_T_INDEX] = OFF(embk_thread_t, index),
#endif
            [EMB_DEBUG_T_WAIT_QUEUE] = OFF(embk_thread_t, wait_queue),
            [EMB_DEBUG_T_DEADLINE] = OFF(embk_thread_t, timeout.deadline),
#if CONFIG_EMB_MUTEX
            [EMB_DEBUG_T_OWNED] = OFF(embk_thread_t, owned),
#else
            [EMB_DEBUG_T_OWNED] = ABSENT,
#endif
            [EMB_DEBUG_T_STACK_BASE] = OFF(embk_thread_t, stack_base),
            [EMB_DEBUG_T_STACK_SIZE] = OFF(embk_thread_t, stack_size),
#if CONFIG_EMB_NOTIFY
            [EMB_DEBUG_T_NOTIFY_BITS] = OFF(embk_thread_t, notify_bits),
            [EMB_DEBUG_NOTIFY_SIZE] = sizeof(emb_notify_bits_t),
#else
            [EMB_DEBUG_T_NOTIFY_BITS] = ABSENT,
            [EMB_DEBUG_NOTIFY_SIZE] = 0u,
#endif
            [EMB_DEBUG_TICK_SIZE] = sizeof(emb_tick_t),

#if CONFIG_EMB_MUTEX
#if CONFIG_EMB_OBJ_NAMES
            [EMB_DEBUG_M_NAME] = OFF(embk_mutex_t, obj.name),
#else
            [EMB_DEBUG_M_NAME] = ABSENT,
#endif
            [EMB_DEBUG_M_OWNER] = OFF(embk_mutex_t, owner),
            [EMB_DEBUG_M_OWNED_NEXT] = OFF(embk_mutex_t, owned_next),
            [EMB_DEBUG_M_COUNT] = OFF(embk_mutex_t, count),
#if CONFIG_EMB_OBJ_LIST
            [EMB_DEBUG_MUTEX_LIST] = ADDR(&embk_mutex_all.head),
            [EMB_DEBUG_MUTEX_LIST_NODE] = OFF(embk_mutex_t, all_node),
#else
            [EMB_DEBUG_MUTEX_LIST] = ABSENT,
            [EMB_DEBUG_MUTEX_LIST_NODE] = ABSENT,
#endif
#else
            [EMB_DEBUG_M_NAME] = ABSENT,
            [EMB_DEBUG_M_OWNER] = ABSENT,
            [EMB_DEBUG_M_OWNED_NEXT] = ABSENT,
            [EMB_DEBUG_M_COUNT] = ABSENT,
            [EMB_DEBUG_MUTEX_LIST] = ABSENT,
            [EMB_DEBUG_MUTEX_LIST_NODE] = ABSENT,
#endif

            [EMB_DEBUG_TYPE_BASE] = EMBK_OBJ_IDLE,
            [EMB_DEBUG_TYPE_NAMES] = ADDR(type_names),
            [EMB_DEBUG_TYPE_COUNT] = sizeof(type_names) / sizeof(type_names[0]),

            [EMB_DEBUG_STACK_FILL] = EMB_STACK_FILL,
            [EMB_DEBUG_STACK_GUARD] = EMBK_STACK_GUARD_BYTES,
            [EMB_DEBUG_ISR_STACK] = ISR_STACK,
            [EMB_DEBUG_ISR_STACK_SIZE] = ISR_STACK_SIZE,

            [EMB_DEBUG_FRAME_KIND] = EMB_ARCH_DEBUG_FRAME_KIND,
            [EMB_DEBUG_FRAME_FLAGS] = EMB_ARCH_DEBUG_FRAME_FLAGS,
            [EMB_DEBUG_FRAME_SIZE] = EMB_ARCH_DEBUG_FRAME_SIZE,
            [EMB_DEBUG_FRAME_TABLE] = FRAME_TABLE,
            [EMB_DEBUG_FRAME_TABLE_COUNT] = FRAME_TABLE_COUNT,

#if CONFIG_EMB_DEVICE
            [EMB_DEBUG_DEVICE_TABLE] = ADDR(&emb_board_device_table),
            [EMB_DEBUG_DT_DEVICES] = OFF(emb_device_table_t, devices),
            [EMB_DEBUG_DT_COUNT] = OFF(emb_device_table_t, count),
            [EMB_DEBUG_DT_EARLY] = OFF(emb_device_table_t, early),
            [EMB_DEBUG_D_NAME] = OFF(emb_device_t, name),
            [EMB_DEBUG_D_STATE] = OFF(emb_device_t, state),
            [EMB_DEBUG_D_CLASS] = OFF(emb_device_t, class_id),
            [EMB_DEBUG_DS_LIFECYCLE] = OFF(emb_device_state_t, lifecycle),
            [EMB_DEBUG_DS_LAST_STATUS] = OFF(emb_device_state_t, last_status),
#else
            [EMB_DEBUG_DEVICE_TABLE] = ABSENT,
            [EMB_DEBUG_DT_DEVICES] = ABSENT,
            [EMB_DEBUG_DT_COUNT] = ABSENT,
            [EMB_DEBUG_DT_EARLY] = ABSENT,
            [EMB_DEBUG_D_NAME] = ABSENT,
            [EMB_DEBUG_D_STATE] = ABSENT,
            [EMB_DEBUG_D_CLASS] = ABSENT,
            [EMB_DEBUG_DS_LIFECYCLE] = ABSENT,
            [EMB_DEBUG_DS_LAST_STATUS] = ABSENT,
#endif
        },
};
