/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_log.c
 * @brief   Logging: the record, the levels, the ring's order and drops (SPEC-015 §2, A127).
 *
 * @details
 * The cases read the ring themselves (embs/log.h) while the log thread, at the lowest priority,
 * cannot run: none of them blocks. With the plain-text backend each record is also formatted as
 * the thread would send it.
 *
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#include <emb/log.h>

#include <emb_test.h>
#include <embs/log.h>
#include <string.h>

EMB_LOG_MODULE_DEFINE(t, EMB_LOG_LEVEL_INF);

static uint8_t rec[160]; /* the cases' records are short */
static char line[128];

/* Empties the ring: records from earlier cases or from the start. */
static void drain(void)
{
    while (embs_log_take(rec, sizeof(rec)) != 0u) { /* bounded by the ring */
    }
}

static size_t take(void)
{
    return embs_log_take(rec, sizeof(rec));
}

#if CONFIG_EMB_LOG_TEXT
/* The line the plain-text backend sends for the oldest record, after its timestamp. */
static const char *take_line(void)
{
    size_t n = take();
    size_t len;
    const char *body;
    if (n == 0u) {
        return "";
    }
    len = embs_log_line(rec, n, line, sizeof(line));
    line[(len < sizeof(line)) ? len : (sizeof(line) - 1u)] = '\0';
    body = strstr(line, "] ");
    return (body != NULL) ? (body + 2) : line;
}

#define EXPECT_LINE(want) EMB_ASSERT_TRUE(strcmp(take_line(), (want)) == 0)
#endif

EMB_TEST(log_record_layout)
{
    size_t n;
    uint32_t idb = embs_log_id_bytes();
    uint32_t ts = CONFIG_EMB_LOG_TIMESTAMP ? 4u : 0u;
    EMB_TEST_REQ("OBS-013");
    drain();
    EMB_LOG_INF(t, "a %d b %u c %s", -5, 7u, "xyz");
    n = take();
    /* the identifier, the timestamp, then int -5 and unsigned 7 as four bytes each and the
     * string as its length and its bytes */
    EMB_ASSERT_EQ(n, idb + ts + 4u + 4u + 1u + 3u);
    EMB_ASSERT_TRUE(memcmp(&rec[idb + ts], "\xFB\xFF\xFF\xFF\x07\x00\x00\x00\x03xyz", 12u) == 0);
    EMB_ASSERT_EQ(take(), 0u);
}

#if CONFIG_EMB_LOG_TEXT
EMB_TEST(log_text_formats)
{
    static const uint8_t bytes[] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    static const char long_str[] = "0123456789012345678901234567890123456789";
    int local = 0;
    char want[64];
    EMB_TEST_REQ("OBS-013");
    drain();
    EMB_LOG_INF(t, "a %d b %u c %s", -5, 7u, "xyz");
    EXPECT_LINE("I t: a -5 b 7 c xyz\n");
    EMB_LOG_WRN(t, "[%5d|%-5d|%05d|%+d]", 42, 42, 42, 42);
    EXPECT_LINE("W t: [   42|42   |00042|+42]\n");
    EMB_LOG_ERR(t, "%x %X %#x %o %c%%", 255u, 255u, 255u, 8u, 'z');
    EXPECT_LINE("E t: ff FF 0xff 10 z%\n");
    EMB_LOG_INF(t, "%lld %llu %ld %lu", -1LL, 18446744073709551615ULL, -70000L, 70000UL);
    EXPECT_LINE("I t: -1 18446744073709551615 -70000 70000\n");
    EMB_LOG_INF(t, "%.2f %.0f %.0f %f", 3.14159, 2.5, 3.5, -0.5); /* a half to the even */
#if CONFIG_EMB_LOG_FLOAT
    EXPECT_LINE("I t: 3.14 2 4 -0.500000\n");
#else
    EXPECT_LINE("I t: (float) (float) (float) (float)\n");
#endif
    EMB_LOG_INF(t, "%hhx %hd", 0x1FF, 70000);
    EXPECT_LINE("I t: ff 4464\n");
    EMB_LOG_INF(t, "<%.3s|%4s>", "abcdef", "z");
    EXPECT_LINE("I t: <abc|   z>\n");
    EMB_LOG_INF(t, "%s", long_str); /* cut at CONFIG_EMB_LOG_STR_MAX */
    (void)memcpy(want, "I t: ", 5u);
    (void)memcpy(&want[5], long_str, CONFIG_EMB_LOG_STR_MAX);
    want[5 + CONFIG_EMB_LOG_STR_MAX] = '\n';
    want[6 + CONFIG_EMB_LOG_STR_MAX] = '\0';
    EXPECT_LINE(want);
    EMB_LOG_HEXDUMP_INF(t, bytes, sizeof(bytes), "rx");
    EXPECT_LINE("I t: rx: de ad be ef\n");
    EMB_LOG_INF(t, "%p", (void *)&local);
    EMB_ASSERT_TRUE(strncmp(take_line(), "I t: 0x", 7u) == 0);
    EMB_LOG_INF(t, "no arguments");
    EXPECT_LINE("I t: no arguments\n");
}
#endif

EMB_TEST(log_levels_compile_and_run_time)
{
    EMB_TEST_REQ("OBS-002");
    drain();
    EMB_LOG_DBG(t, "compiled out: above the file's level for t");
    EMB_ASSERT_EQ(take(), 0u);
    EMB_ASSERT_OK(emb_log_level_set(EMB_LOG_MODULE(t), EMB_LOG_LEVEL_ERR));
    EMB_LOG_WRN(t, "above the run-time level");
    EMB_ASSERT_EQ(take(), 0u);
    EMB_LOG_ERR(t, "kept");
    EMB_ASSERT_TRUE(take() != 0u);
    EMB_ASSERT_OK(emb_log_level_set(EMB_LOG_MODULE(t), EMB_LOG_LEVEL_DBG));
    EMB_LOG_INF(t, "kept again");
    EMB_ASSERT_TRUE(take() != 0u);
    EMB_ASSERT_STATUS(emb_log_level_set(NULL, EMB_LOG_LEVEL_ERR), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_log_level_set(EMB_LOG_MODULE(t), 5u), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_log_stats_get(NULL), EMB_EINVAL);
}

EMB_TEST(log_overflow_drops_and_counts)
{
    emb_log_stats_t before;
    emb_log_stats_t after;
    unsigned i;
    uint32_t kept = 0u;
    uint32_t dropped;
    uint32_t idb = embs_log_id_bytes();
    uint32_t ts = CONFIG_EMB_LOG_TIMESTAMP ? 4u : 0u;
    EMB_TEST_REQ("OBS-003");
    drain();
    EMB_ASSERT_OK(emb_log_stats_get(&before));
    for (i = 0u; i < 400u; i++) { /* bounded: more records than the ring holds */
        EMB_LOG_INF(t, "fill %u", i);
    }
    EMB_ASSERT_OK(emb_log_stats_get(&after));
    dropped = after.dropped - before.dropped;
    EMB_ASSERT_TRUE(dropped > 0u);
    EMB_ASSERT_EQ(after.records - before.records + dropped, 400u);
    EMB_ASSERT_TRUE(after.high_water <= CONFIG_EMB_LOG_RING_SIZE);
    /* the records kept are the first ones, in order: nothing is overwritten */
    while (take() != 0u) { /* bounded by the ring */
        uint32_t v = (uint32_t)rec[idb + ts] | ((uint32_t)rec[idb + ts + 1u] << 8);
        EMB_ASSERT_EQ(v, kept);
        kept++;
    }
    EMB_ASSERT_EQ(kept + dropped, 400u);
    /* the next record that fits comes after a record of the count */
    EMB_LOG_INF(t, "after");
    EMB_ASSERT_EQ(take(), idb + ts + 2u);
    EMB_ASSERT_EQ((uint32_t)rec[0], EMB_LOG_ID_DROPPED);
    EMB_ASSERT_EQ((uint32_t)rec[idb + ts] | ((uint32_t)rec[idb + ts + 1u] << 8), dropped);
    EMB_ASSERT_TRUE(take() != 0u);
    EMB_ASSERT_EQ(take(), 0u);
}

/* An interrupt that logs while a thread's record is reserved and not yet committed. */
static void isr_logs(void *arg)
{
    (void)arg;
    EMB_LOG_INF(t, "isr %u", 9u);
}

EMB_TEST(log_isr_record_waits_for_the_one_before_it)
{
    static const uint8_t held[6] = {1u, 2u, 3u, 4u, 5u, 6u};
    uint32_t pos = 0u;
    size_t n;
    EMB_TEST_REQ("OBS-003");
    drain();
    EMB_ASSERT_TRUE(embs_log_reserve(sizeof(held), &pos));
    emb_test_irq_connect(0u, isr_logs, NULL);
    emb_test_irq_raise(0u);
    emb_test_irq_settle();
    /* the interrupt's record is committed, after one that is not: nothing is published */
    EMB_ASSERT_EQ(take(), 0u);
    embs_log_put(pos, held, sizeof(held));
    embs_log_commit(pos, sizeof(held));
    n = take();
    EMB_ASSERT_EQ(n, sizeof(held));
    EMB_ASSERT_TRUE(memcmp(rec, held, sizeof(held)) == 0);
#if CONFIG_EMB_LOG_TEXT
    EXPECT_LINE("I t: isr 9\n");
#else
    EMB_ASSERT_TRUE(take() != 0u);
#endif
    EMB_ASSERT_EQ(take(), 0u);
}

EMB_TEST(log_flush_sends_what_is_committed)
{
    emb_log_stats_t before;
    emb_log_stats_t after;
    EMB_TEST_REQ("OBS-003");
    drain();
    EMB_ASSERT_OK(emb_log_stats_get(&before));
    EMB_LOG_INF(t, "flushed %d", 1);
    EMB_LOG_INF(t, "flushed %d", 2);
    emb_log_flush();
    EMB_ASSERT_OK(emb_log_stats_get(&after));
    EMB_ASSERT_EQ(after.sent - before.sent, 2u);
    EMB_ASSERT_EQ(take(), 0u);
}

/* ---- producers at once: threads at three priorities and an interrupt ---------------------- */

#define PRODUCERS    3u
#define PER_PRODUCER 300u
#define ISR_RECORDS  50u

static volatile unsigned producers_done;
static volatile unsigned isr_written;

static void produce(void *arg)
{
    unsigned id = (unsigned)(uintptr_t)arg;
    unsigned i;
    for (i = 0u; i < PER_PRODUCER; i++) { /* bounded: the records it writes */
        EMB_LOG_INF(t, "p %u %u", id, i);
        if ((i % 16u) == 15u) {
            (void)emb_thread_sleep(EMB_MS(1)); /* let the others and the reader in */
        }
    }
    producers_done = producers_done + 1u;
}

static void isr_produces(void *arg)
{
    (void)arg;
    EMB_LOG_INF(t, "p %u %u", PRODUCERS, isr_written);
    isr_written = isr_written + 1u;
}

EMB_THREAD_DEFINE(producer_a, produce, (void *)(uintptr_t)0u, 2u, 1024u, EMB_THREAD_NO_AUTOSTART);
EMB_THREAD_DEFINE(producer_b, produce, (void *)(uintptr_t)1u, 3u, 1024u, EMB_THREAD_NO_AUTOSTART);
EMB_THREAD_DEFINE(producer_c, produce, (void *)(uintptr_t)2u, 4u, 1024u, EMB_THREAD_NO_AUTOSTART);

static uint32_t rec_u(size_t at, uint32_t bytes)
{
    uint32_t v = 0u;
    uint32_t k;
    for (k = 0u; k < bytes; k++) { /* bounded: at most four bytes */
        v |= (uint32_t)rec[at + k] << (8u * k);
    }
    return v;
}

EMB_TEST(log_producers_at_once_keep_their_order)
{
    uint32_t idb = embs_log_id_bytes();
    uint32_t ts = CONFIG_EMB_LOG_TIMESTAMP ? 4u : 0u;
    uint32_t next[PRODUCERS + 1u] = {0u, 0u, 0u, 0u};
    uint32_t kept = 0u;
    uint32_t dropped = 0u;
    uint32_t spins = 0u;
    bool ended = false;
    EMB_TEST_REQ("OBS-003");
    drain();
    emb_test_irq_connect(1u, isr_produces, NULL);
    EMB_ASSERT_OK(emb_thread_start(producer_a));
    EMB_ASSERT_OK(emb_thread_start(producer_b));
    EMB_ASSERT_OK(emb_thread_start(producer_c));
    /* The reader runs below the producers and never blocks, so the log thread, at its priority
     * or below, cannot take a record from it: it reads whenever the producers sleep, and spins a
     * tick when the ring is empty (on native, where time moves only when told, that moves it). */
    for (;;) { /* bounded: the producers end, then the ring empties */
        size_t n;
        if ((isr_written < ISR_RECORDS) && ((spins++ % 7u) == 0u)) {
            emb_test_irq_raise(1u);
            emb_test_irq_settle();
        }
        n = take();
        if (n == 0u) {
            if ((producers_done < PRODUCERS) || (isr_written < ISR_RECORDS)) {
                emb_test_spin_ticks(1u); /* a tick passes: the producers' sleeps end */
                continue;
            }
            if (ended) {
                break;
            }
            EMB_LOG_INF(t, "end"); /* brings the count of any records dropped last */
            ended = true;
            continue;
        }
        if (rec_u(0u, (idb < 4u) ? idb : 4u) == EMB_LOG_ID_DROPPED) {
            dropped += rec_u(idb + ts, 2u);
        } else if (n == (idb + ts)) {
            /* the end */
        } else {
            /* "p <producer> <i>": each producer's records in its own order, none torn */
            uint32_t who = rec_u(idb + ts, 4u);
            uint32_t i = rec_u(idb + ts + 4u, 4u);
            EMB_ASSERT_EQ(n, idb + ts + 8u);
            EMB_ASSERT_TRUE(who <= PRODUCERS);
            EMB_ASSERT_TRUE(i >= next[who]);
            next[who] = i + 1u;
            kept++;
        }
    }
    EMB_ASSERT_EQ(kept + dropped, (PRODUCERS * PER_PRODUCER) + ISR_RECORDS);
    EMB_ASSERT_TRUE(kept > 0u);
}
