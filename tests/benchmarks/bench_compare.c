/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The same four measurements for EmbLinkRTOS and for FreeRTOS on the same board (R-003 §6,
 * the review's F14): one source, two kernels. Built here for EmbLinkRTOS; built for FreeRTOS with
 * -DBENCH_FREERTOS by tools/bench/freertos-avr/build.sh on the Uno, and as an ESP-IDF application
 * by tools/bench/freertos-esp32/ on the ESP32 (ESP-IDF's FreeRTOS, as ESP32 projects run it), and
 * by tools/bench/freertos-cm4/ on Cortex-M4 boards. On the Uno both count Timer1 at F_CPU / 64
 * (250 kHz); on the ESP32 both count CCOUNT; on Cortex-M both count CPU cycles from SysTick.
 * Each figure is a total over thousands of operations:
 *
 *   notify_handover  a direct-to-thread notification bounced between two threads (2 switches)
 *   sem_handover     a binary semaphore each way between two threads (2 switches)
 *   sem_give_take    give then take, one thread, no switch
 *   isr_wake         an interrupt gives a semaphore and wakes a higher-priority thread, which
 *                    blocks again: interrupt entry, give, switch in, take, switch back
 *
 * A driver at priority 2 runs them; an echo thread at priority 3 answers. On AVR the interrupt
 * is USART_UDRE, which fires as soon as UDRIE0 is set while the transmitter is idle; elsewhere
 * (the ESP32, ADR-054) the board's test line, raised by emb_irq_pend(), and the unit is the
 * port's cycle counter.
 */
#include <stdint.h>

#define ITER     10000u
#define ISR_ITER 5000u
#define STACK    192u  /* on the Uno: FreeRTOS's words, EmbLinkRTOS's bytes */
#define STACK32  1024u /* elsewhere: bytes for both kernels */
#define M_NOTIFY 0u
#define M_SEM    1u
#define M_ISR    2u
#define M_DONE   3u

#if defined(BENCH_FREERTOS) && defined(ESP_PLATFORM)
/* ---- FreeRTOS in ESP-IDF, on the ESP32 ------------------------------------------------- */
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_intr_alloc.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/dport_reg.h"

#include <inttypes.h>

#define OS_NAME    "freertos"
#define BENCH_AVR  0
#define BENCH_FAST IRAM_ATTR /* in IRAM, as the whole EmbLinkRTOS image is */
#define IDF_STACK  2048u     /* bytes: ESP-IDF's stack depth unit */
typedef TaskHandle_t os_thread_t;
typedef SemaphoreHandle_t os_sem_t;
static StaticSemaphore_t sem_buf[4];
static StaticTask_t tcb[2];
static StackType_t stacks[2][IDF_STACK];

static inline uint32_t os_now(void)
{
    return (uint32_t)esp_cpu_get_cycle_count();
}

static void os_puts(const char *s)
{
    esp_rom_printf("%s", s);
}

static void os_put_u32(uint32_t v)
{
    esp_rom_printf("%" PRIu32, v);
}

#define os_sem_init(i, s)  ((s) = xSemaphoreCreateBinaryStatic(&sem_buf[i]))
#define os_sem_give(s)     ((void)xSemaphoreGive(s))
#define os_sem_take(s)     ((void)xSemaphoreTake((s), portMAX_DELAY))
#define os_sem_take_now(s) ((void)xSemaphoreTake((s), 0))
#define os_notify(t)       ((void)xTaskNotifyGive(t))
#define os_notify_wait()   ((void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
#define os_idle()          vTaskSuspend(NULL)
#define os_end()                \
    do {                        \
        for (;;) {              \
            vTaskSuspend(NULL); \
        }                       \
    } while (0)
static uint32_t os_unit_hz(void)
{
    return esp_rom_get_cpu_ticks_per_us() * 1000000u;
}

#elif defined(BENCH_FREERTOS) && defined(__ARM_ARCH_7EM__)
/* ---- FreeRTOS on a Cortex-M4 board (tools/bench/freertos-cm4/) ------------------------- */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define OS_NAME   "freertos"
#define BENCH_AVR 0
#define BENCH_FAST
typedef TaskHandle_t os_thread_t;
typedef SemaphoreHandle_t os_sem_t;
static StaticSemaphore_t sem_buf[4];
static StaticTask_t tcb[2];
static StackType_t stacks[2][STACK32 / sizeof(StackType_t)];

void bench_platform_init(void);
void bench_puts(const char *s);
void bench_test_irq_enable(void);
void bench_isr(void);
void bench_trigger(void);

#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)
#define SCB_ICSR (*(volatile uint32_t *)0xE000ED04u)

/* CPU cycles from SysTick, as the EmbLinkRTOS Cortex-M port's emb_arch_cycles(): the ticks
 * times the reload, plus the current count, with a tick pending but not yet counted added */
static uint32_t os_now(void)
{
    uint32_t ticks;
    uint32_t cvr;
    taskENTER_CRITICAL();
    ticks = (uint32_t)xTaskGetTickCount();
    cvr = SYST_CVR;
    if ((SCB_ICSR & (1u << 26)) != 0u) {
        ticks++;
        cvr = SYST_CVR;
    }
    taskEXIT_CRITICAL();
    return ticks * (SYST_RVR + 1u) + (SYST_RVR - cvr);
}

static void os_puts(const char *s)
{
    bench_puts(s);
}

static void os_put_u32(uint32_t v)
{
    char d[11];
    int i = 10;
    d[i] = '\0';
    do {
        d[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0);
    os_puts(&d[i]);
}

#define os_sem_init(i, s)  ((s) = xSemaphoreCreateBinaryStatic(&sem_buf[i]))
#define os_sem_give(s)     ((void)xSemaphoreGive(s))
#define os_sem_take(s)     ((void)xSemaphoreTake((s), portMAX_DELAY))
#define os_sem_take_now(s) ((void)xSemaphoreTake((s), 0))
#define os_notify(t)       ((void)xTaskNotifyGive(t))
#define os_notify_wait()   ((void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
#define os_idle()          vTaskSuspend(NULL)
#define os_end()                \
    do {                        \
        for (;;) {              \
            vTaskSuspend(NULL); \
        }                       \
    } while (0)
static uint32_t os_unit_hz(void)
{
    return (uint32_t)configCPU_CLOCK_HZ;
}

#elif defined(BENCH_FREERTOS)
/* ---- FreeRTOS on the Uno ------------------------------------------------------------- */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <avr/interrupt.h>
#include <avr/io.h>

#define OS_NAME   "freertos"
#define BENCH_AVR 1
#define BENCH_FAST
typedef TaskHandle_t os_thread_t;
typedef SemaphoreHandle_t os_sem_t;
static StaticSemaphore_t sem_buf[4];
static StaticTask_t tcb[2];
static StackType_t stacks[2][STACK];

static volatile uint16_t t1_high; /* Timer1 overflows: the count's high half */

ISR(TIMER1_OVF_vect)
{
    t1_high++;
}

static uint32_t os_now(void)
{
    uint16_t hi;
    uint16_t lo;
    uint8_t sreg = SREG;
    cli();
    hi = t1_high;
    lo = TCNT1;
    if ((TIFR1 & (1u << TOV1)) != 0u && lo < 0x8000u) {
        hi++; /* wrapped, not yet counted */
    }
    SREG = sreg;
    return ((uint32_t)hi << 16) | lo;
}

static void uart_init(void)
{
    UCSR0A = (uint8_t)(1u << U2X0);
    UBRR0 = (uint16_t)((F_CPU / 8UL / 115200UL) - 1UL); /* as the EmbLinkRTOS board does */
    UCSR0C = (uint8_t)((1u << UCSZ01) | (1u << UCSZ00));
    UCSR0B = (uint8_t)(1u << TXEN0);
}

static void os_puts(const char *s)
{
    while (*s != '\0') {
        while ((UCSR0A & (1u << UDRE0)) == 0u) {
        }
        UDR0 = (uint8_t)*s++;
    }
}

static void os_put_u32(uint32_t v)
{
    char d[11];
    int i = 10;
    d[i] = '\0';
    do {
        d[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0);
    os_puts(&d[i]);
}

#define os_sem_init(i, s)  ((s) = xSemaphoreCreateBinaryStatic(&sem_buf[i]))
#define os_sem_give(s)     ((void)xSemaphoreGive(s))
#define os_sem_take(s)     ((void)xSemaphoreTake((s), portMAX_DELAY))
#define os_sem_take_now(s) ((void)xSemaphoreTake((s), 0))
#define os_notify(t)       ((void)xTaskNotifyGive(t))
#define os_notify_wait()   ((void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY))
#define os_idle()          vTaskSuspend(NULL)
#define os_end()                \
    do {                        \
        for (;;) {              \
            vTaskSuspend(NULL); \
        }                       \
    } while (0)
static uint32_t os_unit_hz(void)
{
    return (uint32_t)(F_CPU / 64UL);
}

#else
/* ---- EmbLinkRTOS ----------------------------------------------------------------------- */
#include <emb/arch.h> /* the cycle counter is a port-contract function */
#include <emb/emb.h>

#include <emb_board.h>

#define OS_NAME    "emblink"
#define BENCH_AVR  CONFIG_EMB_ARCH_AVR
#define BENCH_FAST /* the whole image runs from RAM on every board it measures */
/* STACK bytes, or the port's minimum where that is larger (Xtensa's windowed frames) */
#if CONFIG_EMB_ARCH_AVR
#define EMB_STACK_WANT STACK
#else
#define EMB_STACK_WANT STACK32
#endif
#define EMB_STACK          (EMB_THREAD_STACK_MIN > EMB_STACK_WANT ? EMB_THREAD_STACK_MIN : EMB_STACK_WANT)
typedef emb_thread_t os_thread_t;
typedef emb_sem_t os_sem_t;
static emb_sem_storage_t sem_buf[4];

static uint32_t os_now(void)
{
    return (uint32_t)emb_arch_cycles();
}

static void os_puts(const char *s)
{
    emb_board_puts(s);
}

static void os_put_u32(uint32_t v)
{
    emb_board_put_u32(v);
}

static void os_sem_init_(unsigned i, emb_sem_t *s)
{
    emb_sem_attr_t a;
    emb_sem_attr_binary(&a, false); /* as xSemaphoreCreateBinary: empty */
    (void)emb_sem_init(&sem_buf[i], &a, s);
}
#define os_sem_init(i, s)  os_sem_init_((i), &(s))
#define os_sem_give(s)     ((void)emb_sem_give(s))
#define os_sem_take(s)     ((void)emb_sem_take((s), EMB_WAIT_FOREVER))
#define os_sem_take_now(s) ((void)emb_sem_take((s), EMB_NO_WAIT))
#define os_notify(t)       ((void)emb_notify_set((t), EMB_NOTIFY_BIT(0)))
#define os_notify_wait()                                                                           \
    ((void)emb_notify_wait(EMB_NOTIFY_BIT(0), EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_WAIT_FOREVER, \
                           NULL))
#define os_idle()                           \
    do {                                    \
        (void)emb_thread_sleep(EMB_SEC(1)); \
    } while (0)
/* the board's end: QEMU exits, a Pico returns to BOOTSEL for the next image, others halt */
#define os_end() emb_board_exit(0)
static uint32_t os_unit_hz(void)
{
    return emb_arch_cycles_hz();
}
#endif

/* ---- the benchmark, the same for both ---------------------------------------------------- */

static os_thread_t driver;
static os_thread_t echo;
static os_sem_t s_go;   /* driver -> echo */
static os_sem_t s_back; /* echo -> driver */
static os_sem_t s_self; /* give and take by one thread */
static os_sem_t s_isr;  /* the interrupt -> echo */
static volatile uint8_t mode;
static volatile uint32_t isr_wakes;

static void report(const char *name, uint32_t units, uint32_t ops)
{
    os_puts("BENCH v1 name=");
    os_puts(name);
    os_puts(" units=");
    os_put_u32(units);
    os_puts(" ops=");
    os_put_u32(ops);
    os_puts(" per_op_x100=");
    os_put_u32((uint32_t)(((uint64_t)units * 100u) / ops));
    os_puts(" unit_hz=");
    os_put_u32(os_unit_hz());
    os_puts(" os=" OS_NAME "\n");
}

/* The interrupt: off again, then the give that wakes the echo thread. */
#if defined(BENCH_FREERTOS) && defined(ESP_PLATFORM)
/* FROM_CPU_INTR2: ESP-IDF keeps 0 and 1 for its cross-core calls */
static void BENCH_FAST isr_give(void *arg)
{
    BaseType_t woken = pdFALSE;
    (void)arg;
    DPORT_REG_WRITE(DPORT_CPU_INTR_FROM_CPU_2_REG, 0);
    (void)xSemaphoreGiveFromISR(s_isr, &woken);
    if (woken != pdFALSE) {
        portYIELD_FROM_ISR();
    }
}
#define TRIGGER() DPORT_REG_WRITE(DPORT_CPU_INTR_FROM_CPU_2_REG, 1)
#elif defined(BENCH_FREERTOS) && defined(__ARM_ARCH_7EM__)
/* the board's test line, pended in the NVIC as emb_irq_pend() does */
void bench_isr(void)
{
    BaseType_t woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(s_isr, &woken);
    portYIELD_FROM_ISR(woken);
}
#define TRIGGER() bench_trigger() /* the NVIC pend and its barriers (bench_platform.c) */
#elif defined(BENCH_FREERTOS)
ISR(USART_UDRE_vect)
{
    BaseType_t woken = pdFALSE;
    UCSR0B &= (uint8_t) ~(1u << UDRIE0);
    (void)xSemaphoreGiveFromISR(s_isr, &woken);
    if (woken != pdFALSE) {
        portYIELD_FROM_ISR();
    }
}
#define TRIGGER() (UCSR0B |= (uint8_t)(1u << UDRIE0))
#elif BENCH_AVR
#include <avr/io.h>
EMB_ISR(USART_UDRE)
{
    UCSR0B &= (uint8_t) ~(1u << UDRIE0);
    (void)emb_sem_give(s_isr);
}
#define TRIGGER() (UCSR0B |= (uint8_t)(1u << UDRIE0))
#else
#include <hw_config.h>
static void isr_give(void *arg)
{
    (void)arg;
    (void)emb_sem_give(s_isr);
}
#define TRIGGER() ((void)emb_irq_pend((emb_irq_t)EMB_HW_TEST_IRQ))
#endif

#if BENCH_AVR
#define TRIGGER_AND_WAIT() TRIGGER() /* the interrupt is taken before the next instruction */
#else
/* On the ESP32 a FROM_CPU write reaches the CPU some cycles later, so a loop of bare triggers
 * could raise the next before the first was taken, and the two would merge: each waits for its
 * wake, on both kernels. */
#define TRIGGER_AND_WAIT()            \
    do {                              \
        uint32_t wakes_ = isr_wakes;  \
        TRIGGER();                    \
        while (isr_wakes == wakes_) { \
        }                             \
    } while (0)
#endif

static void BENCH_FAST echo_entry(void *arg)
{
    (void)arg;
    for (;;) {
        switch (mode) {
        case M_NOTIFY:
            os_notify_wait();
            os_notify(driver);
            break;
        case M_SEM:
            os_sem_take(s_go);
            os_sem_give(s_back);
            break;
        case M_ISR:
            os_sem_take(s_isr);
            isr_wakes++;
            break;
        default:
            os_idle();
            break;
        }
    }
}

static void BENCH_FAST driver_entry(void *arg)
{
    uint32_t i;
    uint32_t t0;
    (void)arg;
#ifdef BENCH_PROFILE_NOTIFY
    /* a profiling image (tools/hil/pcsample.py): the notification hand-over, forever */
    os_puts("PROFILE notify_handover\n");
    for (;;) {
        os_notify(echo);
        os_notify_wait();
    }
#endif
    /* notify: the last kick moves the echo thread on to the next mode */
    t0 = os_now();
    for (i = 0u; i < ITER; i++) {
        if (i == ITER - 1u) {
            mode = M_SEM;
        }
        os_notify(echo);
        os_notify_wait();
    }
    report("notify_handover", os_now() - t0, 2u * ITER);
    t0 = os_now();
    for (i = 0u; i < ITER; i++) {
        if (i == ITER - 1u) {
            mode = M_ISR;
        }
        os_sem_give(s_go);
        os_sem_take(s_back);
    }
    report("sem_handover", os_now() - t0, 2u * ITER);
    t0 = os_now();
    for (i = 0u; i < ITER; i++) {
        os_sem_give(s_self);
        os_sem_take_now(s_self);
    }
    report("sem_give_take", os_now() - t0, ITER);
#if BENCH_AVR
    /* the transmitter idle: while it still sends, UDR is full, UDRE cannot fire at once, and
     * triggers would merge. UDR empty, then the shift register's last frame (87 us at 115200
     * baud) with room to spare: 400 us */
    while ((UCSR0A & (1u << UDRE0)) == 0u) {
    }
    t0 = os_now();
    while (os_now() - t0 < os_unit_hz() / 2500u) {
    }
#endif
    t0 = os_now();
    for (i = 0u; i < ISR_ITER; i++) {
        TRIGGER_AND_WAIT(); /* the echo thread has run when this returns */
    }
    report("isr_wake", os_now() - t0, ISR_ITER);
    if (isr_wakes != ISR_ITER) {
        os_puts("bench_compare: isr_wake woke ");
        os_put_u32(isr_wakes);
        os_puts(" times\n");
    }
    mode = M_DONE;
    os_puts("BENCH_END\n");
    os_end();
}

#if defined(BENCH_FREERTOS) && defined(ESP_PLATFORM)
/* ESP-IDF has started the scheduler and runs this in its main task, at priority 1: both tasks
 * are created with the scheduler suspended, so that neither runs before both handles exist. */
void app_main(void)
{
    os_sem_init(0, s_go);
    os_sem_init(1, s_back);
    os_sem_init(2, s_self);
    os_sem_init(3, s_isr);
    DPORT_REG_WRITE(DPORT_CPU_INTR_FROM_CPU_2_REG, 0);
    (void)esp_intr_alloc(ETS_FROM_CPU_INTR2_SOURCE, ESP_INTR_FLAG_IRAM, isr_give, NULL, NULL);
    os_puts("bench_compare: FreeRTOS " tskKERNEL_VERSION_NUMBER " (ESP-IDF)\n");
    vTaskSuspendAll();
    driver = xTaskCreateStatic(driver_entry, "drv", IDF_STACK, NULL, 2, stacks[0], &tcb[0]);
    echo = xTaskCreateStatic(echo_entry, "echo", IDF_STACK, NULL, 3, stacks[1], &tcb[1]);
    (void)xTaskResumeAll();
}
#elif defined(BENCH_FREERTOS) && defined(__ARM_ARCH_7EM__)
static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb_out, StackType_t **stack_out,
                                   configSTACK_DEPTH_TYPE *size_out)
{
    *tcb_out = &idle_tcb;
    *stack_out = idle_stack;
    *size_out = configMINIMAL_STACK_SIZE;
}

int main(void)
{
    bench_platform_init();
    os_sem_init(0, s_go);
    os_sem_init(1, s_back);
    os_sem_init(2, s_self);
    os_sem_init(3, s_isr);
    bench_test_irq_enable();
    os_puts("bench_compare: FreeRTOS " tskKERNEL_VERSION_NUMBER "\n");
    driver = xTaskCreateStatic(driver_entry, "drv", STACK32 / sizeof(StackType_t), NULL, 2,
                               stacks[0], &tcb[0]);
    echo = xTaskCreateStatic(echo_entry, "echo", STACK32 / sizeof(StackType_t), NULL, 3, stacks[1],
                             &tcb[1]);
    vTaskStartScheduler();
    for (;;) {
    }
}
#elif defined(BENCH_FREERTOS)
/* static allocation only, as EmbLinkRTOS: the idle task's memory comes from here */
static StaticTask_t idle_tcb;
static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb_out, StackType_t **stack_out,
                                   configSTACK_DEPTH_TYPE *size_out)
{
    *tcb_out = &idle_tcb;
    *stack_out = idle_stack;
    *size_out = configMINIMAL_STACK_SIZE;
}

int main(void)
{
    uart_init();
    TCCR1A = 0u;
    TCCR1B = (uint8_t)((1u << CS11) | (1u << CS10)); /* free-running, F_CPU / 64 */
    TIMSK1 = (uint8_t)(1u << TOIE1);
    os_sem_init(0, s_go);
    os_sem_init(1, s_back);
    os_sem_init(2, s_self);
    os_sem_init(3, s_isr);
    os_puts("bench_compare: FreeRTOS " tskKERNEL_VERSION_NUMBER "\n");
    driver = xTaskCreateStatic(driver_entry, "drv", STACK, NULL, 2, stacks[0], &tcb[0]);
    echo = xTaskCreateStatic(echo_entry, "echo", STACK, NULL, 3, stacks[1], &tcb[1]);
    vTaskStartScheduler();
    for (;;) {
    }
}
#else
EMB_THREAD_DEFINE(driver_thread, driver_entry, NULL, 2u, EMB_STACK, EMB_THREAD_NO_AUTOSTART);
EMB_THREAD_DEFINE(echo_thread, echo_entry, NULL, 3u, EMB_STACK, EMB_THREAD_NO_AUTOSTART);

int main(void)
{
    emb_board_init();
    emb_kernel_init();
    driver = driver_thread;
    echo = echo_thread;
    os_sem_init(0, s_go);
    os_sem_init(1, s_back);
    os_sem_init(2, s_self);
    os_sem_init(3, s_isr);
#if !BENCH_AVR
    (void)emb_irq_connect((emb_irq_t)EMB_HW_TEST_IRQ, isr_give, NULL);
    (void)emb_irq_enable((emb_irq_t)EMB_HW_TEST_IRQ);
#endif
    os_puts("bench_compare: EmbLinkRTOS ");
    os_puts(emb_version_string());
    os_puts("\n");
    (void)emb_thread_start(driver_thread);
    (void)emb_thread_start(echo_thread);
    emb_kernel_start();
}
#endif
