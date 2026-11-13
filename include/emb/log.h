/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    log.h
 * @brief   Logging: levels, modules, and records packed in the caller (SPEC-015 §2, A127).
 *
 * @details
 * A module is defined once and declared in its other files, each with the most detailed level
 * that file compiles:
 *
 *     EMB_LOG_MODULE_DEFINE(sensor, EMB_LOG_LEVEL_INF);   (one file)
 *     EMB_LOG_MODULE_DECLARE(sensor, EMB_LOG_LEVEL_DBG);  (the others)
 *     EMB_LOG_ERR(sensor, "read %u failed: %d", id, status);
 *
 * A call takes a printf format and at most eight arguments, which the compiler checks against
 * it. The call packs its arguments into the log ring as SPEC-015 §2.3's record, typed by a
 * signature the compiler computes from their C types: int and the types narrower than int,
 * unsigned, long, unsigned long, long long, unsigned long long, double, a string (char *, cut
 * at CONFIG_EMB_LOG_STR_MAX) and any other pointer. Nothing is formatted in the caller: the
 * deferred backend sends the record and the host formats it from the ELF (tools/logdec); the
 * plain-text backend formats it in the log thread. A call costs one comparison when the module's
 * run-time level is below it, and nothing at all when its file's level or
 * CONFIG_EMB_LOG_LEVEL_MAX is.
 *
 * Each call site's metadata (format, file, line, level, module, signature) is a constant of its
 * own. The deferred backend places it in the section emb_log, which the generated linker scripts
 * keep out of the image: its offset in that section, plus 8, is the record's identifier.
 *
 * @see     subsys/logging/log.c
 * @author  Junior Deogracias
 * @date    2026-11-13 (created)
 */
#ifndef EMB_LOG_H
#define EMB_LOG_H

#include <emb/compiler.h>
#include <emb/config.h>
#include <emb/status.h>
#include <emb/types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Levels: a call is kept when its level is at most the limit. */
#define EMB_LOG_LEVEL_NONE 0u
#define EMB_LOG_LEVEL_ERR  1u
#define EMB_LOG_LEVEL_WRN  2u
#define EMB_LOG_LEVEL_INF  3u
#define EMB_LOG_LEVEL_DBG  4u

/* An argument's type in a call's signature, four bits each, the first argument lowest. */
#define EMB_LOG_T_END    0u
#define EMB_LOG_T_INT    1u /* int, and the types promoted to it */
#define EMB_LOG_T_UINT   2u
#define EMB_LOG_T_LONG   3u
#define EMB_LOG_T_ULONG  4u
#define EMB_LOG_T_LLONG  5u
#define EMB_LOG_T_ULLONG 6u
#define EMB_LOG_T_DOUBLE 7u  /* float and double */
#define EMB_LOG_T_STR    8u  /* char *: its bytes, cut at CONFIG_EMB_LOG_STR_MAX */
#define EMB_LOG_T_PTR    9u  /* any other pointer: its value */
#define EMB_LOG_T_HEX    10u /* a hexdump: a pointer and a size_t, cut at _HEXDUMP_MAX */
#define EMB_LOG_T_BAD    15u /* a type a record cannot carry (long double) */
#define EMB_LOG_ARGS_MAX 8u

/* The stream's own records (A127): identifiers 0 to 7. */
#define EMB_LOG_ID_SYNC    0u /* for unframed transports; not sent in M4.3 */
#define EMB_LOG_ID_DROPPED 1u /* u16: records dropped since the last record that fitted */
#define EMB_LOG_ID_START   2u /* the image's build id (20 bytes) and the tick rate (u32) */
#define EMB_LOG_ID_FIRST   8u /* a call site's: its metadata's offset in emb_log, plus 8 */

/* A module: its run-time level, which starts at EMB_LOG_LEVEL_DBG. */
typedef struct emb_log_module {
    uint8_t level;
} emb_log_module_t;

/* A call site's metadata. With the deferred backend it is in the ELF only, never in the
 * image: the target never reads it. tools/logdec reads it with this layout. */
typedef struct emb_log_meta {
    const char *format;
    const char *file;   /* NULL with the plain-text backend */
    const char *module; /* the module's name */
    uint32_t line;
    uint32_t sig; /* EMB_LOG_T_* per argument */
    uint8_t level;
    uint8_t reserved_[3];
} emb_log_meta_t;

/* What tools/logdec needs to read records, in the emb_log section as emb_log_header. */
typedef struct emb_log_header {
    char magic[4];       /* "EMBL" */
    uint8_t version;     /* 1 */
    uint8_t flags;       /* bit 0: records carry a timestamp */
    uint8_t long_size;   /* sizeof(long): the width of a long argument */
    uint8_t ptr_size;    /* sizeof(void *) */
    uint8_t double_size; /* sizeof(double) */
    uint8_t str_max;     /* CONFIG_EMB_LOG_STR_MAX */
    uint8_t reserved_[2];
} emb_log_header_t;

/* Where a call site's metadata and names go. */
#if CONFIG_EMB_LOG_DEFERRED
#define EMB_LOG_SECTION_ EMB_SECTION(EMB_TABLE_SECTION("emb_log"))
#else
#define EMB_LOG_SECTION_
#endif

/* A call site's file, which only the deferred backend keeps. */
#if CONFIG_EMB_LOG_DEFERRED
#ifdef __FILE_NAME__
#define EMB_LOG_FILE_ __FILE_NAME__
#else
#define EMB_LOG_FILE_ __FILE__
#endif
#endif

/* The most detailed level compiled at all. */
#if CONFIG_EMB_LOG
#define EMB_LOG_LEVEL_LIMIT_ CONFIG_EMB_LOG_LEVEL_MAX
#else
#define EMB_LOG_LEVEL_LIMIT_ 0
#endif

/* NOLINTBEGIN(bugprone-macro-parentheses): MISRA Dev CS-12 rule 20.7, `m` is a token */

/**
 * EMB_LOG_MODULE_DEFINE() - Define a log module, once, in one file.
 * @m:   The module's name, an identifier.
 * @lvl: The most detailed level this file compiles for it.
 */
#if CONFIG_EMB_LOG
#define EMB_LOG_MODULE_DEFINE(m, lvl)                             \
    EMB_LOG_SECTION_ const char emb_log_name_##m##_[] = #m;       \
    emb_log_module_t emb_log_module_##m##_ = {EMB_LOG_LEVEL_DBG}; \
    enum {                                                        \
        emb_log_file_level_##m##_ = (lvl)                         \
    }
#else
#define EMB_LOG_MODULE_DEFINE(m, lvl)     \
    enum {                                \
        emb_log_file_level_##m##_ = (lvl) \
    }
#endif

/**
 * EMB_LOG_MODULE_DECLARE() - Use a module defined in another file.
 * @m:   The module's name.
 * @lvl: The most detailed level this file compiles for it.
 */
#if CONFIG_EMB_LOG
#define EMB_LOG_MODULE_DECLARE(m, lvl)                        \
    extern EMB_LOG_SECTION_ const char emb_log_name_##m##_[]; \
    extern emb_log_module_t emb_log_module_##m##_;            \
    enum {                                                    \
        emb_log_file_level_##m##_ = (lvl)                     \
    }
#else
#define EMB_LOG_MODULE_DECLARE(m, lvl) EMB_LOG_MODULE_DEFINE(m, lvl)
#endif

/* The module's run-time state, for emb_log_level_set(). */
#define EMB_LOG_MODULE(m) (&emb_log_module_##m##_)

/* Calls, each with a format and at most eight arguments. */
#define EMB_LOG_ERR(m, ...) EMB_LOG_AT_(EMB_LOG_LEVEL_ERR, m, __VA_ARGS__)
#define EMB_LOG_WRN(m, ...) EMB_LOG_AT_(EMB_LOG_LEVEL_WRN, m, __VA_ARGS__)
#define EMB_LOG_INF(m, ...) EMB_LOG_AT_(EMB_LOG_LEVEL_INF, m, __VA_ARGS__)
#define EMB_LOG_DBG(m, ...) EMB_LOG_AT_(EMB_LOG_LEVEL_DBG, m, __VA_ARGS__)

/* A hexdump of len bytes at buf, with a label (a string literal, no arguments). */
#define EMB_LOG_HEXDUMP_ERR(m, buf, len, label) EMB_LOG_HEX_(EMB_LOG_LEVEL_ERR, m, buf, len, label)
#define EMB_LOG_HEXDUMP_WRN(m, buf, len, label) EMB_LOG_HEX_(EMB_LOG_LEVEL_WRN, m, buf, len, label)
#define EMB_LOG_HEXDUMP_INF(m, buf, len, label) EMB_LOG_HEX_(EMB_LOG_LEVEL_INF, m, buf, len, label)
#define EMB_LOG_HEXDUMP_DBG(m, buf, len, label) EMB_LOG_HEX_(EMB_LOG_LEVEL_DBG, m, buf, len, label)

/* ---- the call site (internal) ------------------------------------------------------- */

/* Whether a level is compiled for a module in this file: constant, so the call vanishes. */
#define EMB_LOG_ON_(lvl, m)                                      \
    (((unsigned)(lvl) <= (unsigned)emb_log_file_level_##m##_) && \
     ((unsigned)(lvl) <= (unsigned)EMB_LOG_LEVEL_LIMIT_))

#if CONFIG_EMB_LOG_DEFERRED
#define EMB_LOG_META_(lvl, m, fmt, sig)                                 \
    static EMB_LOG_SECTION_ const char emb_log_fmt_[] = fmt;            \
    static EMB_LOG_SECTION_ const char emb_log_file_[] = EMB_LOG_FILE_; \
    static EMB_LOG_SECTION_ const emb_log_meta_t emb_log_meta_ = {      \
        emb_log_fmt_, emb_log_file_, emb_log_name_##m##_, (uint32_t)__LINE__, (sig), (lvl), {0u}}
#else
#define EMB_LOG_META_(lvl, m, fmt, sig)           \
    static const char emb_log_fmt_[] = fmt;       \
    static const emb_log_meta_t emb_log_meta_ = { \
        emb_log_fmt_, NULL, emb_log_name_##m##_, (uint32_t)__LINE__, (sig), (lvl), {0u}}
#endif

#if CONFIG_EMB_LOG
#define EMB_LOG_AT_(lvl, m, ...)                                                           \
    do {                                                                                   \
        if (EMB_LOG_ON_(lvl, m)) {                                                         \
            EMB_LOG_META_(lvl, m, EMB_LOG_FIRST_(__VA_ARGS__), EMB_LOG_SIG_(__VA_ARGS__)); \
            (void)sizeof(emb_log_format_check_(__VA_ARGS__), 0);                           \
            if ((uint8_t)(lvl) <= emb_log_module_##m##_.level) {                           \
                emb_log_write(&emb_log_meta_,                                              \
                              EMB_LOG_SIG_(__VA_ARGS__) EMB_LOG_REST_(__VA_ARGS__));       \
            }                                                                              \
        }                                                                                  \
    } while (0)

#define EMB_LOG_HEX_(lvl, m, buf, len, label)                                                     \
    do {                                                                                          \
        if (EMB_LOG_ON_(lvl, m)) {                                                                \
            EMB_LOG_META_(lvl, m, label, EMB_LOG_T_HEX);                                          \
            if ((uint8_t)(lvl) <= emb_log_module_##m##_.level) {                                  \
                emb_log_write(&emb_log_meta_, EMB_LOG_T_HEX, (const void *)(buf), (size_t)(len)); \
            }                                                                                     \
        }                                                                                         \
    } while (0)
#else
/* logging off: the arguments are still checked against the format, and nothing is emitted */
#define EMB_LOG_AT_(lvl, m, ...)                             \
    do {                                                     \
        (void)sizeof(emb_log_format_check_(__VA_ARGS__), 0); \
        (void)emb_log_file_level_##m##_;                     \
    } while (0)
#define EMB_LOG_HEX_(lvl, m, buf, len, label) \
    do {                                      \
        (void)sizeof((buf), (len), 0);        \
    } while (0)
#endif

/* NOLINTEND(bugprone-macro-parentheses) */

/* The argument count (the format included, so at least one) and the pieces of a call. */
#define EMB_LOG_CAT_(a, b)                                          EMB_LOG_CAT_I_(a, b)
#define EMB_LOG_CAT_I_(a, b)                                        a##b
#define EMB_LOG_NARG_(...)                                          EMB_LOG_NARG_I_(__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define EMB_LOG_NARG_I_(a1, a2, a3, a4, a5, a6, a7, a8, a9, n, ...) n
#define EMB_LOG_FIRST_(...)                                         EMB_LOG_FIRST_I_(__VA_ARGS__, 0)
#define EMB_LOG_FIRST_I_(f, ...)                                    f
#define EMB_LOG_SIG_(...)                                           EMB_LOG_CAT_(EMB_LOG_SIG_, EMB_LOG_NARG_(__VA_ARGS__))(__VA_ARGS__)
#define EMB_LOG_REST_(...)                                          EMB_LOG_CAT_(EMB_LOG_REST_, EMB_LOG_NARG_(__VA_ARGS__))(__VA_ARGS__)

/* An argument's type code, from its C type after the default argument promotions. */
#define EMB_LOG_T_(x)                                                                              \
    _Generic((x),                                                                                  \
        _Bool: EMB_LOG_T_INT,                                                                      \
        char: EMB_LOG_T_INT,                                                                       \
        signed char: EMB_LOG_T_INT,                                                                \
        unsigned char: EMB_LOG_T_INT,                                                              \
        short: EMB_LOG_T_INT,                                                                      \
        unsigned short: ((sizeof(unsigned short) < sizeof(int)) ? EMB_LOG_T_INT : EMB_LOG_T_UINT), \
        int: EMB_LOG_T_INT,                                                                        \
        unsigned int: EMB_LOG_T_UINT,                                                              \
        long: EMB_LOG_T_LONG,                                                                      \
        unsigned long: EMB_LOG_T_ULONG,                                                            \
        long long: EMB_LOG_T_LLONG,                                                                \
        unsigned long long: EMB_LOG_T_ULLONG,                                                      \
        float: EMB_LOG_T_DOUBLE,                                                                   \
        double: EMB_LOG_T_DOUBLE,                                                                  \
        long double: EMB_LOG_T_BAD,                                                                \
        char *: EMB_LOG_T_STR,                                                                     \
        const char *: EMB_LOG_T_STR,                                                               \
        default: EMB_LOG_T_PTR)
#define EMB_LOG_S_(x, i) ((uint32_t)EMB_LOG_T_(x) << (4u * (i)))

#define EMB_LOG_SIG_1(f)                   0u
#define EMB_LOG_SIG_2(f, a)                EMB_LOG_S_(a, 0u)
#define EMB_LOG_SIG_3(f, a, b)             (EMB_LOG_S_(a, 0u) | EMB_LOG_S_(b, 1u))
#define EMB_LOG_SIG_4(f, a, b, c)          (EMB_LOG_SIG_3(f, a, b) | EMB_LOG_S_(c, 2u))
#define EMB_LOG_SIG_5(f, a, b, c, d)       (EMB_LOG_SIG_4(f, a, b, c) | EMB_LOG_S_(d, 3u))
#define EMB_LOG_SIG_6(f, a, b, c, d, e)    (EMB_LOG_SIG_5(f, a, b, c, d) | EMB_LOG_S_(e, 4u))
#define EMB_LOG_SIG_7(f, a, b, c, d, e, g) (EMB_LOG_SIG_6(f, a, b, c, d, e) | EMB_LOG_S_(g, 5u))
#define EMB_LOG_SIG_8(f, a, b, c, d, e, g, h) \
    (EMB_LOG_SIG_7(f, a, b, c, d, e, g) | EMB_LOG_S_(h, 6u))
#define EMB_LOG_SIG_9(f, a, b, c, d, e, g, h, k) \
    (EMB_LOG_SIG_8(f, a, b, c, d, e, g, h) | EMB_LOG_S_(k, 7u))

#define EMB_LOG_REST_1(f)
#define EMB_LOG_REST_2(f, a)                      , (a)
#define EMB_LOG_REST_3(f, a, b)                   , (a), (b)
#define EMB_LOG_REST_4(f, a, b, c)                , (a), (b), (c)
#define EMB_LOG_REST_5(f, a, b, c, d)             , (a), (b), (c), (d)
#define EMB_LOG_REST_6(f, a, b, c, d, e)          , (a), (b), (c), (d), (e)
#define EMB_LOG_REST_7(f, a, b, c, d, e, g)       , (a), (b), (c), (d), (e), (g)
#define EMB_LOG_REST_8(f, a, b, c, d, e, g, h)    , (a), (b), (c), (d), (e), (g), (h)
#define EMB_LOG_REST_9(f, a, b, c, d, e, g, h, k) , (a), (b), (c), (d), (e), (g), (h), (k)

/**
 * emb_log_format_check_() - Never called or defined: how a call's format is checked.
 * @format: The call's format.
 * @...:    Its arguments.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1): it appears only inside sizeof, so no code is emitted
 * @owns     none
 * @config   CONFIG_EMB_LOG
 * @req      OBS-001
 * @since    0.3
 * @stable   no
 *
 * The log macros name it inside sizeof with the call's format and arguments, so the compiler
 * checks them as it checks printf's, and nothing is evaluated.
 */
void emb_log_format_check_(const char *format, ...) EMB_PRINTF_FORMAT(1, 2);

/**
 * emb_log_write() - Pack one record into the log ring: what a call site calls.
 * @meta: The call site's metadata.
 * @sig:  Its arguments' types, EMB_LOG_T_* four bits each.
 * @...:  The arguments, as @sig types them.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1) critical section to reserve; O(arguments plus their strings) copy outside it
 * @owns     none
 * @config   CONFIG_EMB_LOG
 * @req      OBS-001 OBS-003 OBS-013
 * @since    0.3
 * @stable   no
 *
 * Reserves the record's room in the ring with interrupts masked for a bounded number of
 * instructions, copies the arguments with interrupts enabled, then commits the record. A record
 * that does not fit is dropped and counted; the next one that fits is preceded by a record of
 * the count (A127). Nothing waits and nothing is formatted here.
 */
void emb_log_write(const emb_log_meta_t *meta, uint32_t sig, ...);

/**
 * emb_log_level_set() - Set a module's run-time level.
 * @module: EMB_LOG_MODULE(name).
 * @level:  EMB_LOG_LEVEL_NONE to EMB_LOG_LEVEL_DBG: calls above it return at once.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_LOG
 * @req      OBS-002
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a NULL @module or a level above EMB_LOG_LEVEL_DBG.
 */
emb_status_t emb_log_level_set(emb_log_module_t *module, uint8_t level);

/**
 * emb_log_flush() - Send every committed record now, polled.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(records in the ring) plus the console's line time
 * @owns     none
 * @config   CONFIG_EMB_LOG
 * @req      OBS-003
 * @since    0.3
 * @stable   no
 *
 * For a fault's last words and for tests: takes the records the log thread has not, in order,
 * up to the first one not yet committed, and sends them from the caller.
 */
void emb_log_flush(void);

/* The ring's counters. */
typedef struct emb_log_stats {
    uint32_t records;    /* committed */
    uint32_t dropped;    /* refused for want of room */
    uint32_t sent;       /* taken from the ring and sent */
    uint32_t high_water; /* the most bytes the ring held */
} emb_log_stats_t;

/**
 * emb_log_stats_get() - The log ring's counters.
 * @out: Receives them, read together.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_LOG
 * @req      OBS-003
 * @since    0.3
 * @stable   no
 *
 * Return: EMB_OK; EMB_EINVAL for a NULL @out.
 */
emb_status_t emb_log_stats_get(emb_log_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EMB_LOG_H */
