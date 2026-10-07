/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Time types, constructors, and conversions (SPEC-001 §7; SPEC-003 §3, §5.2, §6).
 * Three structs over one scalar: an instant on the kernel clock, a duration, and a
 * timeout. Constructors are macros because EmbCC does not inline functions that
 * return a struct (09 §7); conversions round up and saturate (SPEC-001 §7.3).
 * Compile-time detection of a constant that overflows the tick type is not provided
 * by these macros (it needs a compiler-specific construct); saturation applies to
 * constants as to runtime values, and emb_duration_is_saturated() reports it.
 */
#ifndef EMB_TIME_H
#define EMB_TIME_H

#include <emb/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emb_instant {
    emb_tick_t ticks;
} emb_instant_t;

typedef struct emb_duration {
    emb_tick_t ticks;
} emb_duration_t;

typedef struct emb_timeout {
    emb_tick_t ticks;
} emb_timeout_t;

#define EMB_TICK_NS CONFIG_EMB_TICK_NS

/* Longest finite timeout: SPEC-003 §5.2 (32-bit profile: signed-difference window). */
#if CONFIG_EMB_TICK_32BIT
#define EMB_TIMEOUT_MAX_TICKS EMB_TICK_C(0x7FFFFFFF)
#else
#define EMB_TIMEOUT_MAX_TICKS (EMB_TICK_MAX - EMB_TICK_C(1))
#endif

#ifdef __cplusplus
#define EMB_DURATION_(x) \
    emb_duration_t       \
    {                    \
        (emb_tick_t)(x)  \
    }
#define EMB_TIMEOUT_(x) \
    emb_timeout_t       \
    {                   \
        (emb_tick_t)(x) \
    }
#define EMB_INSTANT_(x) \
    emb_instant_t       \
    {                   \
        (emb_tick_t)(x) \
    }
#else
#define EMB_DURATION_(x) ((emb_duration_t){.ticks = (emb_tick_t)(x)})
#define EMB_TIMEOUT_(x)  ((emb_timeout_t){.ticks = (emb_tick_t)(x)})
#define EMB_INSTANT_(x)  ((emb_instant_t){.ticks = (emb_tick_t)(x)})
#endif

/* Saturating unsigned multiply used by the unit conversions. */
static EMB_INLINE uint64_t emb_mul_sat_u64(uint64_t a, uint64_t b)
{
    return (b != 0u && a > (UINT64_MAX / b)) ? UINT64_MAX : (a * b);
}

/* Nanoseconds to ticks, rounding up, saturating at EMB_TICK_MAX (SPEC-001 §7.3). */
static EMB_INLINE emb_tick_t emb_ns_to_ticks(uint64_t ns)
{
    uint64_t t;
    if (ns > (UINT64_MAX - (uint64_t)(CONFIG_EMB_TICK_NS - 1))) {
        return EMB_TICK_MAX;
    }
    t = (ns + (uint64_t)(CONFIG_EMB_TICK_NS - 1)) / (uint64_t)CONFIG_EMB_TICK_NS;
    return (t > (uint64_t)EMB_TICK_MAX) ? EMB_TICK_MAX : (emb_tick_t)t;
}

#define EMB_NS_TO_TICKS(n)  emb_ns_to_ticks((uint64_t)(n))
#define EMB_US_TO_TICKS(n)  emb_ns_to_ticks(emb_mul_sat_u64((uint64_t)(n), 1000u))
#define EMB_MS_TO_TICKS(n)  emb_ns_to_ticks(emb_mul_sat_u64((uint64_t)(n), 1000000u))
#define EMB_SEC_TO_TICKS(n) emb_ns_to_ticks(emb_mul_sat_u64((uint64_t)(n), 1000000000u))

#define EMB_TICKS(n) EMB_DURATION_(n)
#define EMB_NS(n)    EMB_DURATION_(EMB_NS_TO_TICKS(n))
#define EMB_US(n)    EMB_DURATION_(EMB_US_TO_TICKS(n))
#define EMB_MS(n)    EMB_DURATION_(EMB_MS_TO_TICKS(n))
#define EMB_SEC(n)   EMB_DURATION_(EMB_SEC_TO_TICKS(n))

#define EMB_NO_WAIT      EMB_TIMEOUT_(0)
#define EMB_WAIT_FOREVER EMB_TIMEOUT_(EMB_TICK_MAX)
#define EMB_TIMEOUT(d)   emb_timeout_from_duration(d)

#define EMB_TIMEOUT_IS_FOREVER(t) ((t).ticks == EMB_TICK_MAX)
#define EMB_TIMEOUT_IS_NO_WAIT(t) ((t).ticks == 0u)
#define EMB_DURATION_IS_ZERO(d)   ((d).ticks == 0u)

/**
 * emb_time_now() - Read the kernel monotonic clock.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      KRN-TIM-001 KRN-TIM-016
 * @since    0.2
 * @stable   yes
 *
 * Return: the current instant; zero before kernel start, never moving backward.
 */
emb_instant_t emb_time_now(void);

/**
 * emb_timeout_from_duration() - A timeout that waits at most @d.
 * @d: The duration; saturates to EMB_TIMEOUT_MAX_TICKS so a finite wait never
 *     reads as EMB_WAIT_FOREVER (SPEC-001 §7.3, SPEC-003 §5.2).
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-022 KRN-TIM-022
 * @since    0.2
 * @stable   yes
 */
emb_timeout_t emb_timeout_from_duration(emb_duration_t d);

/**
 * emb_instant_add() - @t plus @d, saturating at EMB_TICK_MAX.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-021
 * @since    0.2
 * @stable   yes
 */
emb_instant_t emb_instant_add(emb_instant_t t, emb_duration_t d);

/**
 * emb_instant_sub() - @later minus @earlier, zero when @later is not after @earlier.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-021
 * @since    0.2
 * @stable   yes
 */
emb_duration_t emb_instant_sub(emb_instant_t later, emb_instant_t earlier);

/**
 * emb_instant_before() - Is @a before @b? Wrap-safe in the 32-bit profile.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-021 KRN-TIM-022
 * @since    0.2
 * @stable   yes
 */
bool emb_instant_before(emb_instant_t a, emb_instant_t b);

/**
 * emb_duration_to_ns() - Exact ticks-to-nanoseconds conversion, saturating.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-021
 * @since    0.2
 * @stable   yes
 */
uint64_t emb_duration_to_ns(emb_duration_t d);

/**
 * emb_duration_is_saturated() - Did a conversion into @d overflow the tick type?
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config
 * @req      API-021
 * @since    0.2
 * @stable   yes
 */
bool emb_duration_is_saturated(emb_duration_t d);

#ifdef __cplusplus
}
#endif

#endif /* EMB_TIME_H */
