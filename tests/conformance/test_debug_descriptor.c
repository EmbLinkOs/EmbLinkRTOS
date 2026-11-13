/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_debug_descriptor.c
 * @brief   The debug descriptor (OBS-008, PORT-012, A107), read the way a debugger reads it.
 *
 * @details
 * This test knows only <emb/debug.h> and reads memory at the addresses and offsets the descriptor
 * gives, never the kernel's structures. It sets up threads in known states, finds them through the
 * descriptor, and checks what it finds against the public API. tools/descriptor runs the same image
 * under a debugger and stops in debug_descriptor_checkpoint(), where every thread is in the state
 * checked here.
 *
 * @author  Junior Deogracias
 * @date    2026-11-08 (created)
 */
#include <emb/arch.h> /* emb_arch_isr_stack: where the port says its interrupt stack is */
#include <emb/debug.h>
#include <emb/version.h>

#include <emb_test.h>
#include <string.h>

#define V(i) (emb_debug_descriptor.value[(i)])

static uint8_t rd8(uintptr_t a)
{
    return *(const volatile uint8_t *)a;
}

static uintptr_t rdp(uintptr_t a)
{
    return *(const volatile uintptr_t *)a;
}

static uintptr_t field(uintptr_t base, unsigned index)
{
    return base + V(index);
}

/* The thread control blocks, as a debugger enumerates them. */
static size_t threads_of(uintptr_t *out, size_t max)
{
    size_t n = 0u;
    if (V(EMB_DEBUG_THREAD_LIST) != EMB_DEBUG_ABSENT) {
        uintptr_t head = V(EMB_DEBUG_THREAD_LIST);
        uintptr_t node = rdp(head); /* a node's first word is its next */
        while (node != head && n < max) {
            out[n++] = node - V(EMB_DEBUG_THREAD_LIST_NODE);
            node = rdp(node);
        }
    } else {
        uintptr_t i;
        for (i = 0u; i < V(EMB_DEBUG_THREAD_TABLE_COUNT) && n < max; i++) {
            uintptr_t t = rdp(V(EMB_DEBUG_THREAD_TABLE) + i * sizeof(uintptr_t));
            if (t != 0u) {
                out[n++] = t;
            }
        }
    }
    return n;
}

static uint8_t index_of(uintptr_t t)
{
    if (V(EMB_DEBUG_T_INDEX) == EMB_DEBUG_ABSENT) {
        uint8_t packed = (uint8_t)(rd8(field(t, EMB_DEBUG_T_FLAGS)) >> 5);
        return packed == 7u ? 0xFEu : packed;
    }
    return rd8(field(t, EMB_DEBUG_T_INDEX));
}

static uintptr_t find(emb_thread_t h)
{
    uintptr_t all[16];
    size_t n = threads_of(all, 16u);
    size_t i;
    for (i = 0u; i < n; i++) {
        if (index_of(all[i]) == emb_thread_index(h)) {
            return all[i];
        }
    }
    return 0u;
}

/* emb_thread_state()'s rule, from the descriptor's fields (docs/engineering/debug-descriptor.md).
 */
static uint8_t state_of(uintptr_t t, uint8_t *reason)
{
    uint8_t tflags = rd8(field(t, EMB_DEBUG_T_TFLAGS));
    *reason = EMB_WAIT_REASON_NONE;
    if ((tflags & EMB_DEBUG_TF_STARTED) == 0u) {
        return EMB_THREAD_INACTIVE;
    }
    if ((tflags & EMB_DEBUG_TF_TERMINATED) != 0u) {
        return EMB_THREAD_TERMINATED;
    }
    if (t == rdp(V(EMB_DEBUG_CPU) + V(EMB_DEBUG_CPU_CURRENT))) {
        return EMB_THREAD_RUNNING;
    }
    if ((tflags & EMB_DEBUG_TF_SUSPENDED) != 0u) {
        *reason = EMB_WAIT_REASON_SUSPEND;
        return EMB_THREAD_BLOCKED;
    }
    if (rd8(field(t, EMB_DEBUG_T_WAIT_STATE)) == 2u) {
        *reason = rd8(field(t, EMB_DEBUG_T_WAIT_REASON));
        return EMB_THREAD_BLOCKED;
    }
    return EMB_THREAD_READY;
}

static void check_state(emb_thread_t h, uint8_t want_state, uint8_t want_reason)
{
    uint8_t state = 0u;
    uint8_t reason = 0u;
    uint8_t api_state = 0u;
    uint8_t api_reason = 0u;
    uintptr_t t = find(h);
    EMB_ASSERT_TRUE(t != 0u);
    state = state_of(t, &reason);
    EMB_ASSERT_OK(emb_thread_state(h, &api_state, &api_reason));
    EMB_ASSERT_EQ(state, api_state);
    EMB_ASSERT_EQ(reason, api_reason);
    EMB_ASSERT_EQ(state, want_state);
    EMB_ASSERT_EQ(reason, want_reason);
}

EMB_TEST(debug_descriptor_header)
{
    EMB_TEST_REQ("OBS-008", "PORT-012");
    EMB_ASSERT_EQ(emb_debug_descriptor.magic[0], 'E');
    EMB_ASSERT_EQ(emb_debug_descriptor.magic[1], 'M');
    EMB_ASSERT_EQ(emb_debug_descriptor.magic[2], 'B');
    EMB_ASSERT_EQ(emb_debug_descriptor.magic[3], 'D');
    EMB_ASSERT_EQ(emb_debug_descriptor.version, EMB_DEBUG_VERSION);
    EMB_ASSERT_EQ(emb_debug_descriptor.count, EMB_DEBUG_COUNT);
    EMB_ASSERT_EQ(emb_debug_descriptor.pointer_size, sizeof(uintptr_t));
    EMB_ASSERT_EQ(V(EMB_DEBUG_KERNEL_VERSION), EMB_VERSION);
    EMB_ASSERT_EQ(V(EMB_DEBUG_PRIORITY_COUNT), CONFIG_EMB_PRIORITY_COUNT);
    EMB_ASSERT_TRUE(strcmp((const char *)V(EMB_DEBUG_PORT_NAME), CONFIG_EMB_ARCH) == 0);
    EMB_ASSERT_TRUE(V(EMB_DEBUG_TYPE_COUNT) >= 2u);
    EMB_ASSERT_TRUE(
        strcmp((const char *)rdp(V(EMB_DEBUG_TYPE_NAMES) + sizeof(uintptr_t)), "thread") == 0);
}

EMB_TEST(debug_descriptor_finds_the_running_thread)
{
    uintptr_t cpu = V(EMB_DEBUG_CPU);
    uintptr_t self = find(emb_thread_self());
    EMB_TEST_REQ("OBS-008");
    EMB_ASSERT_TRUE(self != 0u);
    EMB_ASSERT_EQ(rdp(cpu + V(EMB_DEBUG_CPU_CURRENT)), self);
    EMB_ASSERT_EQ(rd8(cpu + V(EMB_DEBUG_CPU_STATE)), 2u); /* running */
    EMB_ASSERT_EQ(rd8(cpu + V(EMB_DEBUG_CPU_IRQ_NESTING)), 0u);
    EMB_ASSERT_EQ(rd8(field(self, EMB_DEBUG_T_TYPE)),
                  V(EMB_DEBUG_TYPE_BASE) + 1u); /* the second tag: thread */
    EMB_ASSERT_EQ(rd8(field(self, EMB_DEBUG_T_BASE_PRIO)), EMB_TEST_RUNNER_PRIORITY);
    EMB_ASSERT_EQ(rd8(field(self, EMB_DEBUG_T_WAIT_STATE)), 0u);
}

EMB_TEST(debug_descriptor_isr_stack)
{
    size_t size = 0u;
    uint8_t *base = emb_arch_isr_stack(&size);
    EMB_TEST_REQ("PORT-012");
    if (V(EMB_DEBUG_ISR_STACK) == EMB_DEBUG_ABSENT) {
        EMB_ASSERT_TRUE(base == NULL || CONFIG_EMB_ARCH_AVR); /* none, or not described */
    } else {
        EMB_ASSERT_EQ(V(EMB_DEBUG_ISR_STACK), (uintptr_t)base);
        EMB_ASSERT_EQ(V(EMB_DEBUG_ISR_STACK_SIZE), size);
    }
}

/* ---- threads in known states ------------------------------------------------------- */

#define SLEEPER_STACK 1024u
EMB_THREAD_STACK(sleeper_stack, SLEEPER_STACK);
static emb_thread_storage_t sleeper_storage;
#if CONFIG_EMB_MUTEX
static emb_mutex_storage_t lock_storage;
static emb_mutex_t lock;
#endif

/* Each helper does something after its call, so that its own frame is on the stack for a
 * debugger's backtrace (a tail call would leave none). */
static volatile uint8_t woke;

static void sleeper(void *arg)
{
    (void)arg;
    (void)emb_thread_sleep(EMB_TICKS(5));
    woke++;
}

static void parked(void *arg)
{
    (void)arg;
    (void)emb_thread_suspend(emb_thread_self()); /* resumed to exit */
    woke++;
}

#if CONFIG_EMB_ARCH_HAS_FPU_LAZY
/* A thread switched out with a live floating-point context: on Cortex-M its frame holds
 * s16-s31 and the extended hardware frame (EXC_RETURN bit 4 clear). */
static void fpu_user(void *arg)
{
    volatile float x = 1.5f;
    (void)arg;
    x = x * 3.0f;
    (void)emb_thread_sleep(EMB_TICKS(5));
    woke = (uint8_t)(woke + (x > 4.0f ? 1u : 0u));
}
#endif

#if CONFIG_EMB_MUTEX
static void waiter(void *arg)
{
    (void)arg;
    if (emb_mutex_lock(lock, EMB_WAIT_FOREVER) == EMB_OK) {
        (void)emb_mutex_unlock(lock);
    }
}
#endif

/* A debugger stops here (tools/descriptor's check): every thread is in its state. The
 * store keeps the call. */
static volatile uint8_t checkpoints;
EMB_NOINLINE EMB_USED void debug_descriptor_checkpoint(void);
EMB_NOINLINE EMB_USED void debug_descriptor_checkpoint(void)
{
    checkpoints++;
}

static void check_switched_out(uintptr_t t, bool fp)
{
    uintptr_t sp = rdp(field(t, EMB_DEBUG_T_SP));
    uintptr_t base = rdp(field(t, EMB_DEBUG_T_STACK_BASE));
    uintptr_t size = rdp(field(t, EMB_DEBUG_T_STACK_SIZE));
    if (V(EMB_DEBUG_FRAME_KIND) == EMB_DEBUG_KIND_NONE) {
        return; /* host threads */
    }
    EMB_ASSERT_TRUE(sp >= base && sp + V(EMB_DEBUG_FRAME_SIZE) <= base + size);
    if (V(EMB_DEBUG_FRAME_KIND) == EMB_DEBUG_KIND_CORTEX_M) {
        /* the table's last pair is EXC_RETURN: thread mode on the process stack */
        uintptr_t table = V(EMB_DEBUG_FRAME_TABLE);
        uintptr_t last = table + 2u * (V(EMB_DEBUG_FRAME_TABLE_COUNT) - 1u);
        uint32_t exc;
        EMB_ASSERT_EQ(rd8(last), EMB_DEBUG_REG_EXC_RETURN);
        exc = *(const volatile uint32_t *)(sp + 4u * rd8(last + 1u));
        EMB_ASSERT_EQ(exc & 0xFFFFFF0Du, 0xFFFFFF0Du);
        EMB_ASSERT_EQ((exc & 0x10u) == 0u, fp); /* bit 4 clear: a floating-point frame */
    } else {
        /* the pc's word is nonzero */
        uintptr_t table = V(EMB_DEBUG_FRAME_TABLE);
        EMB_ASSERT_TRUE(*(const volatile uint32_t *)(sp + 4u * rd8(table + 1u)) != 0u);
    }
}

EMB_TEST(debug_descriptor_reads_thread_states)
{
    emb_thread_attr_t attr;
    emb_thread_t sleeping;
    emb_thread_t suspended;
    uintptr_t t;
#if CONFIG_EMB_ARCH_HAS_FPU_LAZY
    emb_thread_t fpu;
#endif
#if CONFIG_EMB_MUTEX
    emb_mutex_attr_t ma;
    emb_thread_t waiting;
    uintptr_t self;
    uintptr_t m;
#endif
    EMB_TEST_REQ("OBS-008", "PORT-012");
    emb_thread_attr_default(&attr);
    attr.name = "dd_sleeper";
    attr.priority = 4u;
    attr.stack = sleeper_stack;
    attr.stack_size = sizeof(sleeper_stack);
    EMB_ASSERT_OK(emb_thread_init(&sleeper_storage, &attr, sleeper, NULL, &sleeping));
    EMB_ASSERT_OK(emb_thread_start(sleeping)); /* runs at once and sleeps */
    suspended = emb_test_thread(EMB_TEST_NAME("dd_parked"), 3u, 0u, parked, NULL); /* suspends */
#if CONFIG_EMB_ARCH_HAS_FPU_LAZY
    fpu = emb_test_thread(EMB_TEST_NAME("dd_fpu"), 6u, 0u, fpu_user, NULL); /* sleeps */
#endif
#if CONFIG_EMB_MUTEX
    emb_mutex_attr_default(&ma);
    ma.name = "dd_lock";
    EMB_ASSERT_OK(emb_mutex_init(&lock_storage, &ma, &lock));
    EMB_ASSERT_OK(emb_mutex_lock(lock, EMB_NO_WAIT));
    waiting = emb_test_thread(EMB_TEST_NAME("dd_waiter"), 5u, 0u, waiter, NULL); /* blocks */
#endif
    debug_descriptor_checkpoint();

    t = find(sleeping);
    EMB_ASSERT_TRUE(t != 0u);
    EMB_ASSERT_EQ(rd8(field(t, EMB_DEBUG_T_BASE_PRIO)), 4u);
    check_state(sleeping, EMB_THREAD_BLOCKED, EMB_WAIT_REASON_SLEEP);
    EMB_ASSERT_TRUE(rdp(field(t, EMB_DEBUG_T_STACK_BASE)) >= (uintptr_t)sleeper_stack);
    EMB_ASSERT_TRUE(rdp(field(t, EMB_DEBUG_T_STACK_BASE)) + rdp(field(t, EMB_DEBUG_T_STACK_SIZE)) <=
                    (uintptr_t)sleeper_stack + sizeof(sleeper_stack));
    if (V(EMB_DEBUG_T_NAME) != EMB_DEBUG_ABSENT) {
        EMB_ASSERT_TRUE(strcmp((const char *)rdp(field(t, EMB_DEBUG_T_NAME)), "dd_sleeper") == 0);
    }
    check_switched_out(t, false);

    t = find(suspended);
    EMB_ASSERT_TRUE(t != 0u);
    EMB_ASSERT_EQ(rd8(field(t, EMB_DEBUG_T_BASE_PRIO)), 3u);
    EMB_ASSERT_TRUE((rd8(field(t, EMB_DEBUG_T_TFLAGS)) & EMB_DEBUG_TF_SUSPENDED) != 0u);
    check_state(suspended, EMB_THREAD_BLOCKED, EMB_WAIT_REASON_SUSPEND);
    check_switched_out(t, false);

#if CONFIG_EMB_ARCH_HAS_FPU_LAZY
    check_state(fpu, EMB_THREAD_BLOCKED, EMB_WAIT_REASON_SLEEP);
    check_switched_out(find(fpu), true);
#endif

#if CONFIG_EMB_MUTEX
    self = find(emb_thread_self());
    t = find(waiting);
    EMB_ASSERT_TRUE(t != 0u);
    check_state(waiting, EMB_THREAD_BLOCKED, EMB_WAIT_REASON_OBJECT);
    check_state(emb_thread_self(), EMB_THREAD_RUNNING, EMB_WAIT_REASON_NONE);
    EMB_ASSERT_TRUE(rdp(field(t, EMB_DEBUG_T_WAIT_QUEUE)) != 0u);
    /* the runner owns the lock, and inherits the waiter's priority through it */
    m = rdp(field(self, EMB_DEBUG_T_OWNED));
    EMB_ASSERT_TRUE(m != 0u);
    EMB_ASSERT_EQ(rdp(m + V(EMB_DEBUG_M_OWNER)), self);
    EMB_ASSERT_EQ(rdp(m + V(EMB_DEBUG_M_OWNED_NEXT)), 0u);
    EMB_ASSERT_EQ(rd8(m + V(EMB_DEBUG_M_COUNT)), 1u);
    if (V(EMB_DEBUG_M_NAME) != EMB_DEBUG_ABSENT) {
        EMB_ASSERT_TRUE(strcmp((const char *)rdp(m + V(EMB_DEBUG_M_NAME)), "dd_lock") == 0);
    }
    EMB_ASSERT_EQ(rd8(field(self, EMB_DEBUG_T_EFF_PRIO)), 5u);
    EMB_ASSERT_EQ(rd8(field(self, EMB_DEBUG_T_BASE_PRIO)), EMB_TEST_RUNNER_PRIORITY);
    if (V(EMB_DEBUG_MUTEX_LIST) != EMB_DEBUG_ABSENT) {
        uintptr_t head = V(EMB_DEBUG_MUTEX_LIST);
        uintptr_t node = rdp(head);
        bool listed = false;
        while (node != head) {
            listed = listed || node - V(EMB_DEBUG_MUTEX_LIST_NODE) == m;
            node = rdp(node);
        }
        EMB_ASSERT_TRUE(listed);
    }
    check_switched_out(t, false);
    EMB_ASSERT_OK(emb_mutex_unlock(lock)); /* the waiter takes it, gives it back and exits */
    EMB_ASSERT_OK(emb_thread_join(waiting, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_mutex_destroy(lock));
#endif
    EMB_ASSERT_OK(emb_thread_resume(suspended));
    EMB_ASSERT_OK(emb_thread_join(suspended, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_thread_join(sleeping, EMB_WAIT_FOREVER, NULL));
#if CONFIG_EMB_ARCH_HAS_FPU_LAZY
    EMB_ASSERT_OK(emb_thread_join(fpu, EMB_WAIT_FOREVER, NULL));
#endif
    EMB_ASSERT_OK(emb_thread_destroy(sleeping));
}
