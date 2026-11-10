/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The STM32F4's RTC (RM0390 Rev 6 §26) as an RTC class device (SPEC-017 §10, A123): a BCD
 * calendar in the backup domain, clocked by the 32.768 kHz LSE crystal through the 128 and 256
 * prescalers, so its subseconds count 1/256 s; alarm A through EXTI line 17; 20 backup
 * registers. It runs on across resets and is lost with power, the NUCLEO-F446RE having no
 * battery on VBAT. Initialization starts the LSE and does not wait for it, which takes up to
 * 2 s from cold: the first set waits instead. */
#include <emb/irq.h>
#include <emb/thread.h>

#include <emb_stm32_devices.h>
#include <hw_config.h>

#if CONFIG_EMB_DRV_RTC
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define RTC_TR       0x00u
#define RTC_DR       0x04u
#define RTC_CR       0x08u
#define RTC_ISR      0x0Cu
#define RTC_PRER     0x10u
#define RTC_ALRMAR   0x1Cu
#define RTC_WPR      0x24u
#define RTC_SSR      0x28u
#define RTC_SHIFTR   0x2Cu
#define RTC_ALRMASSR 0x44u
#define RTC_BKP0R    0x50u

#define CR_ALRAE   ((uint32_t)1u << 8)
#define CR_ALRAIE  ((uint32_t)1u << 12)
#define ISR_ALRAWF ((uint32_t)1u << 0)
#define ISR_SHPF   ((uint32_t)1u << 3)
#define ISR_INITS  ((uint32_t)1u << 4)
#define ISR_RSF    ((uint32_t)1u << 5)
#define ISR_INITF  ((uint32_t)1u << 6)
#define ISR_INIT   ((uint32_t)1u << 7)
#define ISR_ALRAF  ((uint32_t)1u << 8)
#define WPR_KEY1   0xCAu
#define WPR_KEY2   0x53u
#define WPR_LOCK   0xFFu
#define PREDIV_A   127u /* 32768 / 128 = 256 Hz */
#define PREDIV_S   255u /* 256 / 256 = 1 Hz, subseconds of 1/256 s */
#define SUBSECONDS 256u
#define MASKSS_ALL ((uint32_t)15u << 24) /* SS[14:0] compared: the whole count */

#define RCC_APB1ENR  0x40u
#define RCC_BDCR     0x70u
#define APB1_PWREN   ((uint32_t)1u << 28)
#define BDCR_LSEON   ((uint32_t)1u << 0)
#define BDCR_LSERDY  ((uint32_t)1u << 1)
#define BDCR_RTCSEL  ((uint32_t)3u << 8)
#define BDCR_LSE     ((uint32_t)1u << 8)
#define BDCR_RTCEN   ((uint32_t)1u << 15)
#define BDCR_BDRST   ((uint32_t)1u << 16)
#define PWR_CR       0x00u
#define PWR_CR_DBP   ((uint32_t)1u << 8)
#define EXTI_IMR     0x00u
#define EXTI_RTSR    0x08u
#define EXTI_PR      0x14u
#define EXTI_ALARM   ((uint32_t)1u << 17) /* the RTC alarm's line */
#define BACKUP_COUNT 20u

#define POLL      100000u /* bounded: a few RTCCLK cycles at 32.768 kHz */
#define LSE_WAITS 300u    /* 10 ms each: the LSE's 2 s start-up, with margin */

#define NS_PER_S  1000000000
#define S_PER_DAY 86400
#define FIRST_S   946684800  /* 2000-01-01T00:00:00Z */
#define END_S     4102444800 /* 2100-01-01T00:00:00Z */
#define ALARM_REACH \
    ((int64_t)28 * S_PER_DAY * NS_PER_S) /* the alarm compares the day of the month */

/* the blocks around the RTC, from the description */
static const uintptr_t stm32_rtc_rcc = EMB_HW_RCC_BASE;
static const uintptr_t stm32_rtc_exti = EMB_HW_EXTI_BASE;

static const emb_stm32_rtc_config_t *stm32_rtc_config(const emb_device_t *dev)
{
    return (const emb_stm32_rtc_config_t *)dev->config;
}

static emb_stm32_rtc_data_t *stm32_rtc_data(const emb_device_t *dev)
{
    return (emb_stm32_rtc_data_t *)dev->data;
}

static uint32_t stm32_rtc_bcd(uint32_t v)
{
    return ((v / 10u) << 4) | (v % 10u);
}

static uint32_t stm32_rtc_bin(uint32_t v)
{
    return ((v >> 4) * 10u) + (v & 0xFu);
}

/* Days since 1970-01-01 of a civil date, and back (Howard Hinnant's algorithms, for years from
 * 2000 to 2099, where they need no negative eras). */
static int64_t stm32_rtc_days(int64_t y, int64_t m, int64_t d)
{
    int64_t yy = (m <= 2) ? (y - 1) : y;
    int64_t era = yy / 400;
    int64_t yoe = yy - (era * 400);
    int64_t mp = (m > 2) ? (m - 3) : (m + 9);
    int64_t doy = (((153 * mp) + 2) / 5) + d - 1;
    int64_t doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
    return (era * 146097) + doe - 719468;
}

static void stm32_rtc_civil(int64_t days, uint32_t *y, uint32_t *m, uint32_t *d)
{
    int64_t z = days + 719468;
    int64_t era = z / 146097;
    int64_t doe = z - (era * 146097);
    int64_t yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
    int64_t doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
    int64_t mp = ((5 * doy) + 2) / 153;
    int64_t dd = doy - (((153 * mp) + 2) / 5) + 1;
    int64_t mm = (mp < 10) ? (mp + 3) : (mp - 9);
    *y = (uint32_t)(yoe + (era * 400) + ((mm <= 2) ? 1 : 0));
    *m = (uint32_t)mm;
    *d = (uint32_t)dd;
}

static bool stm32_rtc_poll(uintptr_t reg, uint32_t bit, bool set)
{
    uint32_t n;
    for (n = 0u; n < POLL; n++) {
        if (((REG32(reg) & bit) != 0u) == set) {
            return true;
        }
    }
    return false;
}

static void stm32_rtc_unlock(uintptr_t base)
{
    REG32(base + RTC_WPR) = WPR_KEY1;
    REG32(base + RTC_WPR) = WPR_KEY2;
}

/* ALRAF cleared, INIT left as it is (RM0390 §26.6.4: the flags clear by writing 0). */
static void stm32_rtc_clear_alarm(uintptr_t base)
{
    REG32(base + RTC_ISR) = (~(ISR_ALRAF | ISR_INIT)) | (REG32(base + RTC_ISR) & ISR_INIT);
    REG32(stm32_rtc_exti + EXTI_PR) = EXTI_ALARM;
}

static bool stm32_rtc_running(void)
{
    uint32_t bdcr = REG32(stm32_rtc_rcc + RCC_BDCR);
    return ((bdcr & BDCR_RTCEN) != 0u) && ((bdcr & BDCR_RTCSEL) == BDCR_LSE) &&
           ((bdcr & BDCR_LSERDY) != 0u);
}

static void stm32_rtc_isr(void *arg)
{
    emb_stm32_rtc_data_t *d = (emb_stm32_rtc_data_t *)arg;
    uintptr_t base = stm32_rtc_config(d->dev)->base;
    if ((REG32(base + RTC_ISR) & ISR_ALRAF) == 0u) {
        REG32(stm32_rtc_exti + EXTI_PR) = EXTI_ALARM;
        return;
    }
    stm32_rtc_unlock(base);
    REG32(base + RTC_CR) &= ~(CR_ALRAE | CR_ALRAIE); /* once: it would match again next month */
    stm32_rtc_clear_alarm(base);
    REG32(base + RTC_WPR) = WPR_LOCK;
    (void)emb_drv_rtc_alarm_fired(d->dev);
}

static emb_status_t stm32_rtc_init(const emb_device_t *dev)
{
    const emb_stm32_rtc_config_t *cfg = stm32_rtc_config(dev);
    emb_stm32_rtc_data_t *d = stm32_rtc_data(dev);
    uintptr_t pwr = EMB_HW_PWR_BASE;
    d->dev = dev;
    REG32(stm32_rtc_rcc + RCC_APB1ENR) |= APB1_PWREN;
    REG32(pwr + PWR_CR) |= PWR_CR_DBP;             /* the backup domain writable */
    REG32(stm32_rtc_rcc + RCC_BDCR) |= BDCR_LSEON; /* starting, not waited for */
    if (stm32_rtc_running()) {
        stm32_rtc_unlock(cfg->base);
        REG32(cfg->base + RTC_CR) &= ~(CR_ALRAE | CR_ALRAIE); /* a previous boot's alarm */
        stm32_rtc_clear_alarm(cfg->base);
        REG32(cfg->base + RTC_ISR) = (~(ISR_RSF | ISR_INIT)) |
                                     (REG32(cfg->base + RTC_ISR) & ISR_INIT);
        REG32(cfg->base + RTC_WPR) = WPR_LOCK;
        (void)stm32_rtc_poll(cfg->base + RTC_ISR, ISR_RSF, true); /* the shadows in step */
    }
    REG32(stm32_rtc_exti + EXTI_IMR) |= EXTI_ALARM;
    REG32(stm32_rtc_exti + EXTI_RTSR) |= EXTI_ALARM;
    if ((emb_irq_connect((emb_irq_t)cfg->irq, stm32_rtc_isr, d) != EMB_OK) ||
        (emb_irq_enable((emb_irq_t)cfg->irq) != EMB_OK)) {
        return EMB_EINVAL;
    }
    return emb_drv_rtc_started(dev);
}

static emb_status_t stm32_rtc_get(const emb_device_t *dev, int64_t *ns)
{
    uintptr_t base = stm32_rtc_config(dev)->base;
    uint32_t ss;
    uint32_t tr;
    uint32_t dr;
    int64_t days;
    int64_t s;
    if (!stm32_rtc_running() || ((REG32(base + RTC_ISR) & ISR_INITS) == 0u)) {
        return EMB_ENOTSET;
    }
    ss = REG32(base + RTC_SSR) & 0xFFFFu; /* locks TR and DR until DR is read */
    tr = REG32(base + RTC_TR);
    dr = REG32(base + RTC_DR);
    days = stm32_rtc_days(2000 + (int64_t)stm32_rtc_bin((dr >> 16) & 0xFFu),
                          (int64_t)stm32_rtc_bin((dr >> 8) & 0x1Fu),
                          (int64_t)stm32_rtc_bin(dr & 0x3Fu));
    s = (days * S_PER_DAY) + ((int64_t)stm32_rtc_bin((tr >> 16) & 0x3Fu) * 3600) +
        ((int64_t)stm32_rtc_bin((tr >> 8) & 0x7Fu) * 60) + (int64_t)stm32_rtc_bin(tr & 0x7Fu);
    /* the fraction: SS counts down from PREDIV_S, and past it after a shift (negative) */
    *ns = (s * NS_PER_S) + ((((int64_t)PREDIV_S - (int64_t)ss) * NS_PER_S) / (int64_t)SUBSECONDS);
    return EMB_OK;
}

/* The LSE started and selected, waited for in steps; a domain clocked otherwise reset first. */
static emb_status_t stm32_rtc_clock(void)
{
    uint32_t i;
    uint32_t bdcr = REG32(stm32_rtc_rcc + RCC_BDCR);
    if (stm32_rtc_running()) {
        return EMB_OK;
    }
    if (((bdcr & BDCR_RTCSEL) != 0u) && ((bdcr & BDCR_RTCSEL) != BDCR_LSE)) {
        REG32(stm32_rtc_rcc + RCC_BDCR) |= BDCR_BDRST; /* the RTC's clock is chosen once */
        REG32(stm32_rtc_rcc + RCC_BDCR) &= ~BDCR_BDRST;
    }
    REG32(stm32_rtc_rcc + RCC_BDCR) |= BDCR_LSEON;
    for (i = 0u; i < LSE_WAITS; i++) { /* bounded: 3 s */
        if ((REG32(stm32_rtc_rcc + RCC_BDCR) & BDCR_LSERDY) != 0u) {
            break;
        }
        (void)emb_thread_sleep(EMB_MS(10u));
    }
    if ((REG32(stm32_rtc_rcc + RCC_BDCR) & BDCR_LSERDY) == 0u) {
        return EMB_EOSCFAIL;
    }
    REG32(stm32_rtc_rcc + RCC_BDCR) |= BDCR_LSE | BDCR_RTCEN;
    return EMB_OK;
}

static emb_status_t stm32_rtc_set(const emb_device_t *dev, int64_t ns)
{
    uintptr_t base = stm32_rtc_config(dev)->base;
    int64_t s = ns / NS_PER_S;
    int64_t frac = ns % NS_PER_S;
    uint32_t y;
    uint32_t mo;
    uint32_t dd;
    uint32_t sod;
    uint32_t wd;
    uint32_t shift = 0u;
    int64_t days;
    emb_status_t st;
    if ((s < FIRST_S) || (s >= END_S)) {
        return EMB_EINVAL;
    }
    st = stm32_rtc_clock();
    if (st != EMB_OK) {
        return st;
    }
    if (frac != 0) {
        /* the next whole second, held back by the rest of this one (RM0390 §26.3.8) */
        s++;
        shift = (uint32_t)(((NS_PER_S - frac) * (int64_t)SUBSECONDS) / NS_PER_S);
    }
    days = s / S_PER_DAY;
    sod = (uint32_t)(s % S_PER_DAY);
    stm32_rtc_civil(days, &y, &mo, &dd);
    wd = (uint32_t)(((days + 3) % 7) + 1); /* 1970-01-01 was a Thursday; Monday is 1 */
    stm32_rtc_unlock(base);
    REG32(base + RTC_ISR) |= ISR_INIT;
    if (!stm32_rtc_poll(base + RTC_ISR, ISR_INITF, true)) {
        REG32(base + RTC_WPR) = WPR_LOCK;
        return EMB_EIO;
    }
    REG32(base + RTC_PRER) = PREDIV_S;
    REG32(base + RTC_PRER) = PREDIV_S | ((uint32_t)PREDIV_A << 16);
    REG32(base + RTC_TR) = (stm32_rtc_bcd(sod / 3600u) << 16) |
                           (stm32_rtc_bcd((sod / 60u) % 60u) << 8) | stm32_rtc_bcd(sod % 60u);
    REG32(base + RTC_DR) = (stm32_rtc_bcd(y - 2000u) << 16) | (wd << 13) |
                           (stm32_rtc_bcd(mo) << 8) | stm32_rtc_bcd(dd);
    REG32(base + RTC_CR) &= ~((uint32_t)1u << 6); /* 24-hour */
    REG32(base + RTC_ISR) &= ~ISR_INIT;
    if ((shift != 0u) && stm32_rtc_poll(base + RTC_ISR, ISR_SHPF, false)) {
        REG32(base + RTC_SHIFTR) = shift; /* SUBFS: back by shift / 256 s */
        (void)stm32_rtc_poll(base + RTC_ISR, ISR_SHPF, false);
    }
    REG32(base + RTC_WPR) = WPR_LOCK;
    (void)stm32_rtc_poll(base + RTC_ISR, ISR_RSF, true);
    return EMB_OK;
}

/* The calendar's second and subsecond count now, as the registers hold them. */
static void stm32_rtc_raw(uintptr_t base, int64_t *sec, int64_t *ss)
{
    uint32_t r = REG32(base + RTC_SSR) & 0xFFFFu; /* locks TR and DR until DR is read */
    uint32_t tr = REG32(base + RTC_TR);
    uint32_t dr = REG32(base + RTC_DR);
    int64_t days = stm32_rtc_days(2000 + (int64_t)stm32_rtc_bin((dr >> 16) & 0xFFu),
                                  (int64_t)stm32_rtc_bin((dr >> 8) & 0x1Fu),
                                  (int64_t)stm32_rtc_bin(dr & 0x3Fu));
    *sec = (days * S_PER_DAY) + ((int64_t)stm32_rtc_bin((tr >> 16) & 0x3Fu) * 3600) +
           ((int64_t)stm32_rtc_bin((tr >> 8) & 0x7Fu) * 60) + (int64_t)stm32_rtc_bin(tr & 0x7Fu);
    *ss = (int64_t)r;
}

/* Alarm A where the calendar and the subsecond count will be at @at_ns: counted down from the
 * registers now, a second each time the count passes 0. A set's shift (above) runs the count
 * past PREDIV_S for up to a second, while the calendar shows the next second; counting from the
 * registers matches that, and every subsecond bit is compared (MASKSS 15). */
static emb_status_t stm32_rtc_alarm(const emb_device_t *dev, int64_t at_ns)
{
    uintptr_t base = stm32_rtc_config(dev)->base;
    int64_t now = 0;
    int64_t sec = 0;
    int64_t ss = 0;
    int64_t ticks;
    uint32_t sod;
    uint32_t y;
    uint32_t mo;
    uint32_t dd;
    if ((stm32_rtc_get(dev, &now) != EMB_OK) || ((at_ns - now) > ALARM_REACH)) {
        return EMB_EINVAL;
    }
    stm32_rtc_raw(base, &sec, &ss);
    /* now from the same reading, so the count starts where the registers are */
    now = (sec * NS_PER_S) + ((((int64_t)PREDIV_S - ss) * NS_PER_S) / (int64_t)SUBSECONDS);
    ticks = (((at_ns - now) * (int64_t)SUBSECONDS) + NS_PER_S - 1) / NS_PER_S; /* up: never early */
    ss -= ticks;
    if (ss < 0) {
        int64_t over = ((-ss) + (int64_t)PREDIV_S) / (int64_t)SUBSECONDS; /* seconds rolled */
        sec += over;
        ss += over * (int64_t)SUBSECONDS;
    }
    sod = (uint32_t)(sec % S_PER_DAY);
    stm32_rtc_civil(sec / S_PER_DAY, &y, &mo, &dd);
    stm32_rtc_unlock(base);
    REG32(base + RTC_CR) &= ~(CR_ALRAE | CR_ALRAIE);
    if (!stm32_rtc_poll(base + RTC_ISR, ISR_ALRAWF, true)) {
        REG32(base + RTC_WPR) = WPR_LOCK;
        return EMB_EIO;
    }
    REG32(base + RTC_ALRMAR) = (stm32_rtc_bcd(dd) << 24) | (stm32_rtc_bcd(sod / 3600u) << 16) |
                               (stm32_rtc_bcd((sod / 60u) % 60u) << 8) | stm32_rtc_bcd(sod % 60u);
    REG32(base + RTC_ALRMASSR) = MASKSS_ALL | ((uint32_t)ss & 0x7FFFu);
    stm32_rtc_clear_alarm(base);
    REG32(base + RTC_CR) |= CR_ALRAE | CR_ALRAIE;
    REG32(base + RTC_WPR) = WPR_LOCK;
    (void)y;
    (void)mo;
    return EMB_OK;
}

static void stm32_rtc_alarm_cancel(const emb_device_t *dev)
{
    uintptr_t base = stm32_rtc_config(dev)->base;
    stm32_rtc_unlock(base);
    REG32(base + RTC_CR) &= ~(CR_ALRAE | CR_ALRAIE);
    stm32_rtc_clear_alarm(base);
    REG32(base + RTC_WPR) = WPR_LOCK;
}

static emb_status_t stm32_rtc_backup_read(const emb_device_t *dev, uint8_t index, uint32_t *value)
{
    *value = REG32(stm32_rtc_config(dev)->base + RTC_BKP0R + (4u * (uint32_t)index));
    return EMB_OK;
}

static emb_status_t stm32_rtc_backup_write(const emb_device_t *dev, uint8_t index, uint32_t value)
{
    REG32(stm32_rtc_config(dev)->base + RTC_BKP0R + (4u * (uint32_t)index)) = value;
    return EMB_OK;
}

const emb_drv_rtc_api_t emb_stm32_rtc_api = {
    {stm32_rtc_init, NULL, NULL, NULL, NULL,
     EMB_DRV_RTC_CAP_CALENDAR | EMB_DRV_RTC_CAP_SUBSECONDS | EMB_DRV_RTC_CAP_BACKUP},
    stm32_rtc_get,
    stm32_rtc_set,
    stm32_rtc_alarm,
    stm32_rtc_alarm_cancel,
    stm32_rtc_backup_read,
    stm32_rtc_backup_write,
    BACKUP_COUNT,
    {0u, 0u, 0u},
};
#else
/* ISO C: a translation unit declares something */
typedef int emb_stm32_rtc_off_t;
#endif
