/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    main.c
 * @brief   The M3a reference controller (docs/adoption.md) on the NUCLEO-F446RE.
 *
 * @details
 * An interrupt-driven controller in the base profile, with trusted threads.
 *
 * - Acquisition: TIM2's update event starts an ADC1 conversion of the chip's temperature
 *   sensor in hardware, at CTL_RATE_HZ; the conversion's interrupt timestamps the reading and
 *   posts it to a message queue without waiting. A full queue is counted, never silent.
 * - Control (highest priority): takes each sample with a timeout, so that a missing sample is
 *   seen; filters the temperature and switches a simulated heater (the green LED) with
 *   hysteresis around the setpoint. A sample whose processing ends after the next sample is
 *   due is a deadline miss. Three in a row put the controller in degraded mode, which
 *   processes every other sample; two seconds of processing that fits a single period bring
 *   it back.
 * - Commands: lines on USART2 (the ST-LINK's port), received by interrupt: help, status, load,
 *   rate, setpoint, fault, hang.
 * - Telemetry (lowest priority): one TLM line a second. It feeds the independent watchdog only
 *   when the control thread has made progress since the last line.
 * - The console is shared by the command replies and the telemetry under a priority-inheriting
 *   mutex: the contention on a shared peripheral.
 * - At boot: the reset's cause (RCC_CSR) and the crash record the last run left, if any, as an
 *   EMB_CRASH line that tools/crashdec decodes.
 * - Marker pins for a logic analyzer: TIM2 toggles PA0 at each sampling trigger, in hardware;
 *   PA10 is high during the ADC's handler, PA8 while the control thread processes a sample,
 *   PA9 while a thread holds the console. Each is one register write.
 *
 * @author  Junior Deogracias
 * @date    2026-11-07 (created)
 */
#include "definitions.h"

#include <emb/arch.h>
#include <emb/emb.h>

#include <emb_board.h>
#include <emb_stm32_drivers.h>
#include <emb_stm32_pins.h>
#include <hw_config.h>
#include <string.h>

#define CTL_RATE_HZ     100u /* the default sampling rate */
#define CTL_RATE_MIN    10u
#define CTL_RATE_MAX    1000u
#define CTL_SETPOINT_MC 40000 /* the default setpoint, thousandths of a degree */
#define CTL_HYST_MC     500   /* the heater's hysteresis, either side of the setpoint */
#define CTL_MISS_ENTER  3u    /* consecutive deadline misses that enter degraded mode */
#define CTL_CLEAN_LEAVE 2u    /* seconds without a miss that leave it */
#define CTL_LOAD_MAX    400u  /* percent of a period, the injected work's ceiling */
#define CTL_WDG_MS      2000u /* the watchdog's timeout */
#define CTL_LINE_MAX    48u

/* The marker pins, on GPIOA (the Arduino header's names) */
#define MARK_PORT    EMB_HW_GPIOA_BASE
#define MARK_CONTROL 8u  /* PA8, D7 */
#define MARK_CONSOLE 9u  /* PA9, D8 */
#define MARK_ISR     10u /* PA10, D2 */

static const emb_stm32_pin_t markers[] = {
    {EMB_STM32_PORT(A), 0u, EMB_STM32_PIN_AF, 1u, 0u, 0u}, /* PA0, A0: TIM2_CH1 */
    {EMB_STM32_PORT(A), MARK_CONTROL, EMB_STM32_PIN_OUTPUT, 0u, 0u, 0u},
    {EMB_STM32_PORT(A), MARK_CONSOLE, EMB_STM32_PIN_OUTPUT, 0u, 0u, 0u},
    {EMB_STM32_PORT(A), MARK_ISR, EMB_STM32_PIN_OUTPUT, 0u, 0u, 0u},
};

/* The threads, the sample and byte queues and the console's mutex are system.yaml's: the
 * generated system.c defines them, system.h declares them and the entries below (A125). */

/* What the threads and the handlers share. Each field has one writer; the readers take a
 * snapshot, and a torn read of a counter is at most one update old. */
static volatile struct {
    uint32_t processed;      /* samples processed by the control thread */
    uint32_t missed;         /* receive timeouts: no sample for three periods */
    uint32_t deadline_miss;  /* processing ended after the next sample was due */
    uint32_t shed;           /* samples skipped in degraded mode */
    uint32_t degraded_count; /* entries into degraded mode */
    uint32_t queue_drops;    /* samples the full queue refused (the handler) */
    uint32_t adc_overruns;   /* conversions the ADC lost (the handler) */
    uint32_t rx_drops;       /* received bytes the full queue refused (the handler) */
    uint32_t rx_irqs;        /* USART interrupts taken (the handler) */
    uint32_t rx_count;       /* bytes received (the handler) */
    uint32_t lat_max;        /* cycles from the conversion to the processing's start, this second */
    uint32_t proc_max;       /* cycles from the conversion to the processing's end, this second */
    uint32_t heartbeat;      /* the control thread's progress, for the watchdog */
    int32_t temp_mc;         /* the filtered temperature */
    uint8_t heater;
    uint8_t degraded;
} st;

/* Settings the command thread writes and the control thread reads. */
static volatile uint32_t rate_hz = CTL_RATE_HZ;
static volatile uint32_t load_pct;
static volatile int32_t setpoint_mc = CTL_SETPOINT_MC;
static volatile uint8_t hang_requested;
static uint16_t adc_seq;

/* ---- the console, under the mutex ---------------------------------------------------------- */

static void out(const char *s)
{
    emb_board_puts(s);
}

static void out_u32(uint32_t v)
{
    emb_board_put_u32(v);
}

/* thousandths as a decimal with one fraction digit, sign included */
static void out_milli(int32_t v)
{
    uint32_t a;
    if (v < 0) {
        out("-");
        a = (uint32_t)(-(v + 1)) + 1u;
    } else {
        a = (uint32_t)v;
    }
    out_u32(a / 1000u);
    out(".");
    out_u32((a % 1000u) / 100u);
}

static void out_hex(uint32_t v, unsigned digits)
{
    static const char hex[] = "0123456789abcdef";
    char buf[9];
    unsigned i;
    for (i = 0u; i < digits && i < 8u; i++) { /* bounded by 8 */
        buf[i] = hex[(v >> (4u * (digits - 1u - i))) & 0xFu];
    }
    buf[i] = '\0';
    out(buf);
}

static void lock(void)
{
    (void)emb_mutex_lock(console, EMB_WAIT_FOREVER);
    emb_stm32_gpio_set(MARK_PORT, MARK_CONSOLE);
}

static void unlock(void)
{
    emb_stm32_gpio_clear(MARK_PORT, MARK_CONSOLE);
    (void)emb_mutex_unlock(console);
}

static uint32_t cycles_to_us(uint32_t c)
{
    return c / (emb_arch_cycles_hz() / 1000000u);
}

/* ---- interrupt handlers -------------------------------------------------------------------- */

static void adc_isr(void *arg)
{
    sample_t s;
    uint16_t raw = 0u;
    int r;
    (void)arg;
    emb_stm32_gpio_set(MARK_PORT, MARK_ISR);
    r = emb_stm32f4_adc_take(EMB_HW_ADC1_BASE, &raw);
    if (r < 0) {
        st.adc_overruns++;
    } else if (r > 0) {
        s.cycles = (uint32_t)emb_arch_cycles();
        s.raw = raw;
        s.seq = adc_seq++;
        if (emb_msgq_send(samples, &s, EMB_NO_WAIT) != EMB_OK) {
            st.queue_drops++;
        }
    }
    emb_stm32_gpio_clear(MARK_PORT, MARK_ISR);
}

static void usart_isr(void *arg)
{
    int c = emb_stm32_usart_getc_isr(EMB_HW_CONSOLE_BASE);
    (void)arg;
    st.rx_irqs++;
    if (c >= 0) {
        st.rx_count++;
        uint8_t b = (uint8_t)c;
        if (emb_msgq_send(rx_bytes, &b, EMB_NO_WAIT) != EMB_OK) {
            st.rx_drops++;
        }
    }
}

/* ---- control ----------------------------------------------------------------------------------
 */

static void work(uint32_t from, uint32_t cycles)
{
    while ((uint32_t)emb_arch_cycles() - from < cycles) { /* bounded by the injected load */
    }
}

void control(void *arg)
{
    int32_t filtered = INT32_MIN;
    uint32_t consecutive = 0u;
    uint32_t clean = 0u;
    (void)arg;
    for (;;) {
        sample_t s;
        uint32_t rate = rate_hz;
        uint32_t period = emb_arch_cycles_hz() / rate;
        uint32_t start;
        uint32_t done;
        int32_t t;
        emb_status_t r = emb_msgq_receive(samples, &s, EMB_TIMEOUT(EMB_MS(3000u / rate + 1u)));
        st.heartbeat++;
        if (hang_requested != 0u) {
            for (;;) { /* the injected hang: the watchdog ends it */
            }
        }
        if (r != EMB_OK) {
            st.missed++; /* no sample for three periods: the acquisition stopped */
            continue;
        }
        if (st.degraded != 0u && (s.seq & 1u) != 0u) {
            st.shed++; /* degraded: every other sample, so a processing has two periods */
            continue;
        }
        start = (uint32_t)emb_arch_cycles();
        emb_stm32_gpio_set(MARK_PORT, MARK_CONTROL);
        if (start - s.cycles > st.lat_max) {
            st.lat_max = start - s.cycles;
        }
        t = emb_stm32f4_temp_mc(s.raw, EMB_HW_FACTORY_CAL_BASE);
        filtered = (filtered == INT32_MIN) ? t : filtered + (t - filtered) / 8; /* an IIR filter */
        st.temp_mc = filtered;
        if (filtered < setpoint_mc - CTL_HYST_MC) {
            st.heater = 1u;
        } else if (filtered > setpoint_mc + CTL_HYST_MC) {
            st.heater = 0u;
        }
        if (st.heater != 0u) {
            emb_board_led_toggle(); /* the heater's drive: the LED flickers while it is on */
        }
        work(start, (uint32_t)(((uint64_t)period * load_pct) / 100u));
        done = (uint32_t)emb_arch_cycles();
        emb_stm32_gpio_clear(MARK_PORT, MARK_CONTROL);
        if (done - s.cycles > st.proc_max) {
            st.proc_max = done - s.cycles;
        }
        st.processed++;
        /* the deadline: the next processed sample's arrival, one period away (two when degraded) */
        if (done - s.cycles > period * (st.degraded != 0u ? 2u : 1u)) {
            st.deadline_miss++;
            clean = 0u;
            if (++consecutive >= CTL_MISS_ENTER && st.degraded == 0u) {
                st.degraded = 1u;
                st.degraded_count++;
            }
        } else {
            consecutive = 0u;
            /* Leaving degraded mode needs headroom, not just a kept deadline: two seconds of
             * processing that would have met the normal deadline, one period. Otherwise a
             * lasting overload would leave and re-enter every two seconds. */
            if (st.degraded != 0u) {
                clean = (done - s.cycles <= period) ? clean + 1u : 0u;
                if (clean >= CTL_CLEAN_LEAVE * rate / 2u) {
                    st.degraded = 0u;
                    clean = 0u;
                }
            }
        }
    }
}

/* ---- commands -----------------------------------------------------------------------------------
 */

static int parse_u32(const char *s, uint32_t *v)
{
    uint32_t n = 0u;
    if (*s == '\0') {
        return 0;
    }
    while (*s != '\0') { /* bounded by the line */
        if (*s < '0' || *s > '9' || n > 100000000u) {
            return 0;
        }
        n = n * 10u + (uint32_t)(*s - '0');
        s++;
    }
    *v = n;
    return 1;
}

static void status(void)
{
    static const char *const names[] = {"control", "command", "telemetry"};
    emb_thread_t th[3];
    unsigned i;
    th[0] = control_thread;
    th[1] = command_thread;
    th[2] = telemetry_thread;
    for (i = 0u; i < 3u; i++) {
        size_t size = 0u;
        size_t used = 0u;
        if (emb_thread_stack_info(th[i], &size, &used) == EMB_OK) {
            out("STACK ");
            out(names[i]);
            out(" used=");
            out_u32((uint32_t)used);
            out(" size=");
            out_u32((uint32_t)size);
            out("\n");
        }
    }
    {
        size_t size = 0u;
        size_t hw = 0u;
        if (emb_isr_stack_info(&size, &hw) == EMB_OK) {
            out("STACK isr used=");
            out_u32((uint32_t)hw);
            out(" size=");
            out_u32((uint32_t)size);
            out("\n");
        }
    }
    out("OK status rate=");
    out_u32(rate_hz);
    out(" load=");
    out_u32(load_pct);
    out(" setpoint=");
    out_milli(setpoint_mc);
    out("\n");
}

static void execute(char *line)
{
    char *arg = strchr(line, ' ');
    uint32_t v = 0u;
    if (arg != NULL) {
        *arg++ = '\0';
    }
    lock();
    if (strcmp(line, "help") == 0) {
        out("OK help: status | load <0-400 %> | rate <10-1000 Hz> | setpoint <mC> | fault | "
            "hang\n");
    } else if (strcmp(line, "status") == 0) {
        status();
    } else if (strcmp(line, "load") == 0 && arg != NULL && parse_u32(arg, &v) &&
               v <= CTL_LOAD_MAX) {
        load_pct = v;
        out("OK load ");
        out_u32(v);
        out("\n");
    } else if (strcmp(line, "rate") == 0 && arg != NULL && parse_u32(arg, &v) &&
               v >= CTL_RATE_MIN && v <= CTL_RATE_MAX) {
        uint32_t clk = emb_stm32_tim_clock_hz(EMB_HW_APB1_HZ, EMB_HW_APB1_DIV);
        rate_hz = v;
        (void)emb_stm32_tim_trgo_periodic(EMB_HW_TIM2_BASE, clk, v);
        out("OK rate ");
        out_u32(v);
        out("\n");
    } else if (strcmp(line, "setpoint") == 0 && arg != NULL && parse_u32(arg, &v) && v <= 125000u) {
        setpoint_mc = (int32_t)v;
        out("OK setpoint ");
        out_milli((int32_t)v);
        out("\n");
    } else if (strcmp(line, "fault") == 0) {
        out("OK fault: a trap in the command thread\n");
        emb_stm32_usart_flush(EMB_HW_CONSOLE_BASE);
        EMB_TRAP(); /* the injected failure: a hardware fault, recorded, then a reboot */
    } else if (strcmp(line, "hang") == 0) {
        out("OK hang: the control thread stops; the watchdog resets in ");
        out_u32(CTL_WDG_MS);
        out(" ms\n");
        hang_requested = 1u;
    } else {
        out("ERR unknown or invalid: ");
        out(line);
        out("\n");
    }
    unlock();
}

void command(void *arg)
{
    char line[CTL_LINE_MAX];
    unsigned n = 0u;
    (void)arg;
    for (;;) {
        uint8_t b = 0u;
        (void)emb_msgq_receive(rx_bytes, &b, EMB_WAIT_FOREVER);
        if (b == (uint8_t)'\r' || b == (uint8_t)'\n') {
            line[n] = '\0';
            if (n != 0u) {
                execute(line);
            }
            n = 0u;
        } else if (n < CTL_LINE_MAX - 1u) {
            line[n++] = (char)b;
        }
    }
}

/* ---- telemetry ----------------------------------------------------------------------------- */

void telemetry(void *arg)
{
    emb_instant_t next = emb_time_now();
    uint32_t last_beat = st.heartbeat;
    uint32_t seconds = 0u;
    (void)arg;
    for (;;) {
        uint32_t lat;
        uint32_t proc;
        next = emb_instant_add(next, EMB_MS(1000));
        (void)emb_thread_sleep_until(next);
        seconds++;
        if (st.heartbeat != last_beat) {
            emb_stm32f4_iwdg_kick(EMB_HW_IWDG_BASE); /* only while the control thread runs */
            last_beat = st.heartbeat;
        }
        lat = st.lat_max;
        proc = st.proc_max;
        st.lat_max = 0u;
        st.proc_max = 0u;
        lock();
        out("TLM t=");
        out_u32(seconds);
        out(st.degraded != 0u ? " mode=degraded" : " mode=normal");
        out(" rate=");
        out_u32(rate_hz);
        out(" temp=");
        out_milli(st.temp_mc);
        out(" heater=");
        out_u32(st.heater);
        out(" samples=");
        out_u32(st.processed);
        out(" missed=");
        out_u32(st.missed);
        out(" deadline_miss=");
        out_u32(st.deadline_miss);
        out(" shed=");
        out_u32(st.shed);
        out(" degraded=");
        out_u32(st.degraded_count);
        out(" queue_drops=");
        out_u32(st.queue_drops);
        out(" adc_overruns=");
        out_u32(st.adc_overruns);
        out(" lat_max_us=");
        out_u32(cycles_to_us(lat));
        out(" proc_max_us=");
        out_u32(cycles_to_us(proc));
        out(" load=");
        out_u32(load_pct);
        out(" rx=");
        out_u32(st.rx_count);
        out("/");
        out_u32(st.rx_irqs);
        out(" rx_drops=");
        out_u32(st.rx_drops);
        out("\n");
        unlock();
    }
}

/* ---- boot ---------------------------------------------------------------------------------- */

static void report_reset(uint32_t flags)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } causes[] = {
        {EMB_STM32F4_RESET_IWDG, " watchdog"},  {EMB_STM32F4_RESET_SOFT, " software"},
        {EMB_STM32F4_RESET_POR, " power-on"},   {EMB_STM32F4_RESET_PIN, " pin"},
        {EMB_STM32F4_RESET_BOR, " brown-out"},  {EMB_STM32F4_RESET_WWDG, " window-watchdog"},
        {EMB_STM32F4_RESET_LPWR, " low-power"},
    };
    unsigned i;
    out("CTL reset");
    for (i = 0u; i < sizeof(causes) / sizeof(causes[0]); i++) { /* bounded by the table */
        if ((flags & causes[i].bit) != 0u) {
            out(causes[i].name);
        }
    }
    out("\n");
}

static void report_crash(void)
{
    emb_crash_record_t r;
    unsigned i;
    if (emb_crash_record_get(&r) != EMB_OK) {
        out("CTL crash none\n");
        return;
    }
    out("CTL crash class=");
    out_u32(r.fault_class);
    out(" code=");
    out_u32(r.code);
    out(" pc=0x");
    out_hex((uint32_t)r.pc, 8u);
    out(" uptime_ticks=");
    out_u32((uint32_t)r.uptime_ticks);
    out("\nEMB_CRASH ");
    for (i = 0u; i < r.length; i++) { /* bounded by the record's length */
        out_hex(r.raw[i], 2u);
    }
    out("\n");
    (void)emb_crash_record_clear();
}

int main(void)
{
    uint32_t flags;
    emb_board_init(); /* 180 MHz, the pins, USART2 at 115200 */
    flags = emb_stm32f4_reset_flags_take();
    out("\nCTL boot: EmbLinkRTOS reference controller, rate=");
    out_u32(CTL_RATE_HZ);
    out(" Hz\n");
    report_reset(flags);
    emb_kernel_init(); /* the crash record is read here */
    report_crash();

    emb_stm32_clock_enable(EMB_HW_TIM2_CLKEN_OFFSET, EMB_HW_TIM2_CLKEN_BIT);
    emb_stm32_clock_enable(EMB_HW_ADC1_CLKEN_OFFSET, EMB_HW_ADC1_CLKEN_BIT);
    emb_stm32_pins_apply(markers, (unsigned)(sizeof(markers) / sizeof(markers[0])));
    emb_stm32_tim_ch1_toggle_on_update(EMB_HW_TIM2_BASE);
    EMB_CHECK(emb_irq_connect(EMB_HW_ADC1_IRQ, adc_isr, NULL));
    EMB_CHECK(emb_irq_connect(EMB_HW_CONSOLE_IRQ, usart_isr, NULL));
    emb_stm32f4_adc_temp_on_tim2(EMB_HW_ADC1_BASE, EMB_HW_ADC_COMMON_BASE);
    emb_stm32_usart_rx_irq_enable(EMB_HW_CONSOLE_BASE);
    EMB_CHECK(emb_irq_enable(EMB_HW_ADC1_IRQ));
    EMB_CHECK(emb_irq_enable(EMB_HW_CONSOLE_IRQ));
    if (emb_stm32_tim_trgo_periodic(EMB_HW_TIM2_BASE,
                                    emb_stm32_tim_clock_hz(EMB_HW_APB1_HZ, EMB_HW_APB1_DIV),
                                    CTL_RATE_HZ) == 0) {
        out("CTL error: TIM2 cannot run at the rate\n");
    }
    emb_stm32f4_iwdg_start(EMB_HW_IWDG_BASE, CTL_WDG_MS);
    out("CTL ready: type help\n");
    emb_kernel_start();
}
