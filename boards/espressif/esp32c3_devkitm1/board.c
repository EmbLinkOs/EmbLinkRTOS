/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * esp32c3_devkitm1 (ESP32-C3-DevKitM-1 V1.0): the ESP32-C3-MINI-1 as its boot ROM leaves
 * it after loading this image from flash offset 0 (CPU_CLK XTAL_CLK / 2, UART0 at 115200
 * on GPIO21/20); console on UART0, which the board's CP2102N carries to its USB port.
 * Register addresses and bits from the ESP32-C3 Technical Reference Manual v1.4,
 * chapters 6 (clocks) and 26 (UART). The board's only LED is the SK6812 on GPIO8, an
 * addressable one: emb_board_led_toggle() keeps its state only, as QEMU virt's does. A
 * hardware target: the run ends in a WFI loop with interrupts masked (an emulator ends the
 * run at the console's EMB_TEST_END).
 *
 * emb_board_init() moves the CPU to 160 MHz as ESP-IDF v5.4's bootloader does
 * (esp_hw_support/port/esp32c3/rtc_clk.c and hal/esp32c3/clk_tree_ll.h, with the module's
 * 40 MHz crystal): once the ROM's boot messages have left UART0, it powers the BBPLL
 * (RTC_CNTL_OPTIONS0), opens the analog I2C bus to it and starts its calibration
 * (I2C_MST_ANA_CONF0, ANA_CONFIG), sets it to 480 MHz (SYSTEM_CPU_PER_CONF.PLL_FREQ_SEL and
 * the BBPLL block's registers, written through the boot ROM's rom_i2c_writeReg and
 * rom_i2c_writeReg_Mask as ESP-IDF's REGI2C_WRITE does), selects 160 MHz from it
 * (CPUPERIOD_SEL, SOC_CLK_SEL, PRE_DIV_CNT 0), stores APB_CLK's 80 MHz where the ROM reads it
 * (RTC_CNTL_STORE5) and tells the ROM the CPU's frequency (ets_update_cpu_frequency). UART0
 * runs from APB_CLK, which was 20 MHz and is now 80 MHz: its divider is set again for
 * 115200 baud. SYSTIMER, the tick's source, runs from the crystal and does not change.
 */
#include <emb/config.h>
#include <emb/fault.h>
#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#endif

#include <emb_arch.h>

#include <emb_board.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define UART_FIFO        REG32(EMB_HW_UART0_BASE + 0x00u)
#define UART_STATUS      REG32(EMB_HW_UART0_BASE + 0x1Cu)
#define STATUS_TXFIFO(s) (((s) >> 16) & 0x3FFu)
#define TXFIFO_SIZE      128u
#define UART_CLKDIV      REG32(EMB_HW_UART0_BASE + 0x14u)
#define UART_CLK_CONF    REG32(EMB_HW_UART0_BASE + 0x78u)
#define TX_BOUND         800000u /* well past one character time at 115200 baud and 160 MHz */

/* clocks (TRM v1.4 ch. 6; ESP-IDF v5.4 soc/esp32c3 register headers) */
#define RTC_CNTL_OPTIONS0 REG32(0x60008000u)
#define RTC_CNTL_STORE5   REG32(0x600080BCu) /* RTC_APB_FREQ_REG */
#define SYSTEM_CPU_PER    REG32(0x600C0008u)
#define SYSTEM_SYSCLK     REG32(0x600C0058u)
#define I2C_MST_ANA_CONF0 REG32(0x6000E040u)
#define ANA_CONFIG        REG32(0x6000E044u)

#define OPTIONS0_BB_I2C_FORCE_PD    (1u << 6)
#define OPTIONS0_BBPLL_I2C_FORCE_PD (1u << 8)
#define OPTIONS0_BBPLL_FORCE_PD     (1u << 10)
#define ANA_CONF0_STOP_FORCE_HIGH   (1u << 2)
#define ANA_CONF0_STOP_FORCE_LOW    (1u << 3)
#define ANA_CONFIG_I2C_BBPLL        (1u << 17) /* set: the BBPLL's bus is off */
#define CPU_PER_PLL_480M            (1u << 2)
#define CPU_PER_CPUPERIOD_MASK      3u
#define CPU_PER_CPUPERIOD_160M      1u
#define SYSCLK_PRE_DIV_MASK         0x3FFu
#define SYSCLK_SOC_SEL_MASK         (3u << 10)
#define SYSCLK_SOC_SEL_PLL          (1u << 10)
#define CLK_CONF_DIV_MASK           0x000FFFFFu /* SCLK_DIV_B, SCLK_DIV_A, SCLK_DIV_NUM */
#define CLK_CONF_SEL_MASK           (3u << 20)
#define CLK_CONF_SEL_APB            (1u << 20)

/* the BBPLL block on the analog I2C bus (ESP-IDF v5.4 soc/esp32c3/include/soc/regi2c_bbpll.h) */
#define I2C_BBPLL          0x66u
#define I2C_BBPLL_HOSTID   0u
#define BBPLL_MODE_HF      4u
#define BBPLL_OC_REF_DIV   2u
#define BBPLL_OC_DIV_7_0   3u
#define BBPLL_OC_DR        5u /* DR1 bits 2:0, DR3 bits 6:4 */
#define BBPLL_OC_DCUR      6u /* DCUR 2:0, DHREF_SEL 5:4, DLREF_SEL 7:6 */
#define BBPLL_OC_VCO_DBIAS 9u /* bits 1:0 */

/* the boot ROM's routines (ESP-IDF v5.4 esp_rom/esp32c3/ld/esp32c3.rom.ld) */
typedef void (*rom_i2c_write_t)(uint8_t block, uint8_t host, uint8_t reg, uint8_t data);
typedef void (*rom_i2c_write_mask_t)(uint8_t block, uint8_t host, uint8_t reg, uint8_t msb,
                                     uint8_t lsb, uint8_t data);
typedef void (*rom_cpu_freq_t)(uint32_t ticks_per_us);
#define ROM_I2C_WRITE      ((rom_i2c_write_t)0x4000195Cu)
#define ROM_I2C_WRITE_MASK ((rom_i2c_write_mask_t)0x40001960u)
#define ROM_CPU_FREQ       ((rom_cpu_freq_t)0x40000588u) /* ets_update_cpu_frequency */

#define CPU_MHZ 160u
#define APB_HZ  80000000u

static void tx_wait_idle(void)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND * TXFIFO_SIZE; n++) { /* bounded: the FIFO's last characters */
        if (STATUS_TXFIFO(UART_STATUS) == 0u) {
            break;
        }
    }
}

/* the BBPLL at 480 MHz from the 40 MHz crystal: clk_ll_bbpll_set_config's values */
static void bbpll_configure(void)
{
    const uint8_t div_ref = 0u, div7_0 = 8u, dr1 = 0u, dr3 = 0u, dchgp = 5u, dcur = 3u, dbias = 2u;
    I2C_MST_ANA_CONF0 &= ~ANA_CONF0_STOP_FORCE_HIGH; /* regi2c_ctrl_ll_bbpll_calibration_start */
    I2C_MST_ANA_CONF0 |= ANA_CONF0_STOP_FORCE_LOW;
    ROM_I2C_WRITE(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_MODE_HF, 0x6Bu);
    ROM_I2C_WRITE(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_REF_DIV, (uint8_t)((dchgp << 4) | div_ref));
    ROM_I2C_WRITE(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_DIV_7_0, div7_0);
    ROM_I2C_WRITE_MASK(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_DR, 2u, 0u, dr1);
    ROM_I2C_WRITE_MASK(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_DR, 6u, 4u, dr3);
    ROM_I2C_WRITE(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_DCUR,
                  (uint8_t)((2u << 6) | (1u << 4) | dcur));
    ROM_I2C_WRITE_MASK(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_VCO_DBIAS, 1u, 0u, dbias);
    ROM_I2C_WRITE_MASK(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_DCUR, 5u, 4u, 2u); /* DHREF_SEL */
    ROM_I2C_WRITE_MASK(I2C_BBPLL, I2C_BBPLL_HOSTID, BBPLL_OC_DCUR, 7u, 6u, 1u); /* DLREF_SEL */
}

static void clock_to_pll_160m(void)
{
    tx_wait_idle(); /* the ROM's last characters leave at the old rate */
    RTC_CNTL_OPTIONS0 &=
        ~(OPTIONS0_BB_I2C_FORCE_PD | OPTIONS0_BBPLL_FORCE_PD | OPTIONS0_BBPLL_I2C_FORCE_PD);
    ANA_CONFIG &= ~ANA_CONFIG_I2C_BBPLL;
    SYSTEM_CPU_PER |= CPU_PER_PLL_480M;
    bbpll_configure();
    SYSTEM_CPU_PER = (SYSTEM_CPU_PER & ~CPU_PER_CPUPERIOD_MASK) | CPU_PER_CPUPERIOD_160M;
    SYSTEM_SYSCLK &= ~SYSCLK_PRE_DIV_MASK;
    SYSTEM_SYSCLK = (SYSTEM_SYSCLK & ~SYSCLK_SOC_SEL_MASK) | SYSCLK_SOC_SEL_PLL;
    RTC_CNTL_STORE5 = ((APB_HZ >> 12) & 0xFFFFu) | (((APB_HZ >> 12) & 0xFFFFu) << 16);
    ROM_CPU_FREQ(CPU_MHZ);
    /* UART0 from APB_CLK, 80 MHz: (80 MHz x 16) / 115200 = 11111, 694 and 7/16 */
    UART_CLK_CONF = (UART_CLK_CONF & ~(CLK_CONF_DIV_MASK | CLK_CONF_SEL_MASK)) | CLK_CONF_SEL_APB;
    UART_CLKDIV = (((APB_HZ << 4) / EMB_HW_CONSOLE_BAUD) >> 4) |
                  ((((APB_HZ << 4) / EMB_HW_CONSOLE_BAUD) & 0xFu) << 20);
}

void emb_board_init(void)
{
    clock_to_pll_160m(); /* UART0 stays 115200 8N1 */
}

void emb_board_led_toggle(void)
{
    /* no LED driver yet: the console is the board's only output */
}

static void put_char(char c)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: one character time */
        if (STATUS_TXFIFO(UART_STATUS) < TXFIFO_SIZE - 1u) {
            break;
        }
    }
    UART_FIFO = (uint32_t)(unsigned char)c;
}

void emb_board_puts(const char *s)
{
    while (*s != '\0') { /* bounded by the string */
        put_char(*s);
        s++;
    }
}

void emb_board_put_u32(uint32_t v)
{
    char buf[11];
    unsigned i = sizeof(buf) - 1u;
    buf[i] = '\0';
    do { /* at most ten digits */
        i--;
        buf[i] = (char)('0' + (char)(v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0u);
    emb_board_puts(&buf[i]);
}

void emb_board_exit(int code)
{
    (void)code; /* no host to report it to */
    tx_wait_idle();
    emb_rv_halt();
}

void emb_board_fault_report(const emb_fault_info_t *info)
{
    emb_board_puts("\nemb: kernel fault class ");
    emb_board_put_u32((info != NULL) ? info->fault_class : 0u);
    emb_board_puts(" code ");
    emb_board_put_u32((info != NULL) ? info->code : 0u);
    emb_board_puts(" pc ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->pc : 0u);
    emb_board_puts(" address ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->address : 0u);
    emb_board_puts(" at ");
    emb_board_puts((info != NULL && info->where != NULL) ? info->where : "?");
    emb_board_puts("\n");
#if CONFIG_EMB_FAULT_ACTION_REBOOT
    tx_wait_idle(); /* the port resets next: the line must leave first */
#else
    emb_board_exit(2); /* the halt action: the run ends here */
#endif
}

#if CONFIG_EMB_DEVICE
/* No devices yet: the board has no description to generate them from (SPEC-016 §8). */
const emb_device_table_t emb_board_device_table = {NULL, 0u, 0u};
#endif
