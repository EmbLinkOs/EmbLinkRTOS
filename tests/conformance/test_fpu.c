/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Floating-point context across switches (PORT-FPU, docs/ports/cortex_m.md): built where
 * the port reports lazy FPU stacking. The callee-saved registers s16-s31 move in PendSV
 * for a thread with an active FP context; the caller-saved s0-s15 and FPSCR are stacked
 * by the hardware (lazily) on interrupt entry. Both must come back intact.
 */
#include <emb_cortex_m.h> /* the register probes (port services) */
#include <emb_test.h>

#if !defined(__ARM_FP)
#error "test_fpu is built only for ports with a hardware FPU"
#endif

#define PATTERN_RUNNER 0x3F800000u /* 1.0f and the floats after it */
#define PATTERN_HELPER 0x40400000u /* 3.0f ... */

static void fill_and_wait(void *arg)
{
    (void)arg;
    emb_cm_fp_fill(PATTERN_HELPER);
    (void)emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER,
                          NULL);
    emb_test_mark(emb_cm_fp_check(PATTERN_HELPER) != 0u ? 1 : 2);
}

EMB_TEST(fpu_callee_saved_registers_survive_a_switch)
{
    emb_thread_t h;
    emb_cm_fp_fill(PATTERN_RUNNER);
    h = emb_test_thread(EMB_TEST_NAME("fp"), 3u, 0u, fill_and_wait, NULL); /* runs, blocks */
    EMB_ASSERT_TRUE(emb_cm_fp_check(PATTERN_RUNNER) != 0u);
    EMB_ASSERT_OK(emb_notify_set(h, EMB_NOTIFY_BIT(0)));
    EMB_ASSERT_MARKS(1);
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
}

/* A float recurrence whose result depends on every intermediate value. */
typedef struct fp_state {
    float a;
    float b;
    float c;
} fp_state_t;

static void fp_chunk(fp_state_t *s, unsigned n)
{
    float a = s->a;
    float b = s->b;
    float c = s->c;
    unsigned i;
    for (i = 0u; i < n; i++) { /* bounded by n */
        a = a * 1.0001f + b;
        b = b * 0.999f + c;
        c = c + a * 0.0001f;
        if (a > 1.0e6f) {
            a = a * 0.5f;
        }
    }
    s->a = a;
    s->b = b;
    s->c = c;
}

static volatile unsigned preemptions;

static void ticker(void *arg)
{
    fp_state_t mine = {2.0f, 0.25f, 0.125f};
    unsigned k;
    (void)arg;
    for (k = 0u; k < 5u; k++) {
        (void)emb_thread_sleep(EMB_TICKS(1));
        fp_chunk(&mine, 50u); /* clobbers the FP registers in the middle of the runner's loop */
        preemptions++;
    }
    emb_test_mark(mine.a > 0.0f ? 1 : 2);
}

EMB_TEST(fpu_state_survives_preemption)
{
    fp_state_t s = {1.0f, 0.5f, 0.25f};
    fp_state_t ref = {1.0f, 0.5f, 0.25f};
    unsigned chunks = 0u;
    unsigned i;
    emb_thread_t h;
    preemptions = 0u;
    h = emb_test_thread(EMB_TEST_NAME("tick"), 3u, 0u, ticker, NULL);
    /* bounded: the ticker needs five ticks; the chunk limit ends a stuck run */
    while (preemptions < 5u && chunks < 2000000u) {
        fp_chunk(&s, 100u);
        chunks++;
    }
    EMB_ASSERT_TRUE(preemptions == 5u);
    for (i = 0u; i < chunks; i++) {
        fp_chunk(&ref, 100u);
    }
    EMB_ASSERT_TRUE(s.a == ref.a && s.b == ref.b && s.c == ref.c);
    EMB_ASSERT_MARKS(1);
    EMB_ASSERT_OK(emb_thread_join(h, EMB_WAIT_FOREVER, NULL));
}
