/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * esp32_devkitc (an ESP32 development board with the ESP-WROOM-32 module: the DevKitC and its
 * "ESP-32S" copies) as its boot ROM leaves it after loading this image from flash offset
 * 0x1000: the CPU on the 40 MHz crystal, UART0 at 115200 on GPIO1/3, which the board's
 * USB-serial bridge carries. Register addresses from Espressif's ESP32 Technical Reference
 * Manual (UART, chapter 13) and ESP-IDF v5.4's soc/esp32 headers. The board has no LED the
 * CPU drives on every maker's copy: emb_board_led_toggle() keeps its state only. A hardware
 * target: the run ends in a waiti loop with interrupts masked.
 *
 * emb_board_init() moves the CPU to 160 MHz, ESP-IDF's default, as ESP-IDF v6.1's
 * rtc_clk_cpu_freq_set_config() does it from the 40 MHz crystal (esp_hw_support/port/esp32/
 * rtc_clk.c, esp_hal_clock/esp32/include/hal/clk_tree_ll.h): once the ROM's boot messages have
 * left UART0, it opens the analog I2C bus to the BBPLL (ANA_CONFIG, as rtc_clk_init() does
 * first), powers the BBPLL and that bus (RTC_CNTL_OPTIONS0), sets it to 320 MHz through the
 * boot ROM's I2C register write (its divider registers 2, 3 and 5 only), selects 160 MHz
 * from it (DPORT_CPU_PER_CONF, then RTC_CNTL_CLK_CONF.SOC_CLK_SEL), sets REF_TICK's divider for
 * the 80 MHz APB clock, stores APB_CLK where the ROM reads it (RTC_CNTL_STORE5) and tells the ROM
 * the CPU's frequency (ets_update_cpu_frequency). The core voltage stays as the ROM set it: 160
 * MHz with 40 MHz flash runs at the crystal's 1.10 V, unlike 240 MHz, whose voltage is in eFuse.
 * UART0 runs from APB_CLK, which was 40 MHz and is now 80 MHz: its divider is set again for
 * 115200 baud. CCOUNT, the tick's and the cycle counter's source, runs at the CPU clock
 * (CONFIG_EMB_XT_CPU_HZ, board.conf).
 *
 * Measured on this board (2026-11-06): ESP-IDF's sequence also writes BBPLL registers 0, 1, 4, 9
 * and 11 (the calibration reset and the 320 MHz output selection). Done here, those writes left
 * the PLL at half its rate, 160 MHz (the CPU at 80, APB at 40, UART0 at 57600). Read back under
 * ESP-IDF v6.1 at 160 MHz, the same registers hold other values (0x18, 0x20, 0x9A, 0x84, 0x43),
 * which this board's reset leaves in them, so the board writes the three divider registers only.
 * The register state then matches ESP-IDF's, and the CPU runs at 160 MHz (CCOUNT against the host's
 * clock: 80 million cycles in 0.50 s).
 */
#include <emb/config.h>
#include <emb/fault.h>
#if CONFIG_EMB_DEVICE
#include <emb/device.h>
#include <emb/drv_dma.h>
#endif

#include <emb_arch.h>

#include <emb_board.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define UART_FIFO        REG32(EMB_HW_UART0_BASE + 0x00u)
#define UART_CLKDIV      REG32(EMB_HW_UART0_BASE + 0x14u) /* CLKDIV 19:0, CLKDIV_FRAG 23:20 */
#define UART_STATUS      REG32(EMB_HW_UART0_BASE + 0x1Cu)
#define STATUS_TXFIFO(s) (((s) >> 16) & 0xFFu)
#define STATUS_TX_BUSY   (0xFu << 24) /* ST_UTX_OUT: the transmitter's state, 0 when idle */
#define TXFIFO_SIZE      128u
#define TX_BOUND         800000u /* well past one character time at 115200 baud and 160 MHz */

/* clocks (ESP-IDF v5.4-v6.1 soc/esp32 register headers) */
#define RTC_CNTL_OPTIONS0  REG32(0x3FF48000u)
#define RTC_CNTL_CLK_CONF  REG32(0x3FF48070u)
#define RTC_CNTL_STORE5    REG32(0x3FF480B4u) /* RTC_APB_FREQ_REG */
#define DPORT_CPU_PER_CONF REG32(0x3FF0003Cu)
#define SYSCON_PLL_TICK    REG32(0x3FF66008u)
#define ANA_CONFIG         REG32(0x6000E044u) /* the analog I2C buses' enables, set: off */

#define OPTIONS0_BB_I2C_FORCE_PD    (1u << 6)
#define OPTIONS0_BBPLL_I2C_FORCE_PD (1u << 8)
#define OPTIONS0_BBPLL_FORCE_PD     (1u << 10)
#define OPTIONS0_BIAS_I2C_FORCE_PD  (1u << 18)
#define CLK_CONF_SOC_CLK_SEL_MASK   (3u << 27)
#define CLK_CONF_SOC_CLK_SEL_PLL    (1u << 27)
#define CPU_PER_CONF_160M           1u
#define ANA_CONFIG_ALL_BUSES        (0x3FFu << 8)
#define ANA_CONFIG_I2C_BBPLL        (1u << 17)

/* the BBPLL block on the analog I2C bus (soc/esp32/include/soc/regi2c_bbpll.h) */
#define I2C_BBPLL        0x66u
#define I2C_BBPLL_HOSTID 4u
#define BBPLL_OC_LREF    2u
#define BBPLL_OC_DIV_7_0 3u
#define BBPLL_OC_DCUR    5u

/* the boot ROM's routines (esp_hal_regi2c/esp32/rom.regi2c.ld, esp_rom/esp32/ld/esp32.rom.ld) */
typedef void (*rom_i2c_write_t)(uint8_t block, uint8_t host, uint8_t reg, uint8_t data);
typedef void (*rom_cpu_freq_t)(uint32_t ticks_per_us);
typedef void (*rom_delay_us_t)(uint32_t us);
/* Called through volatile pointers: a constant ROM address lets GCC emit a direct call8, which
 * does not reach the ROM from IRAM (0x400041A4 is more than 512 KB below 0x40080400). */
static rom_i2c_write_t volatile const rom_i2c_write = (rom_i2c_write_t)0x400041A4u;
static rom_cpu_freq_t volatile const rom_cpu_freq =
    (rom_cpu_freq_t)0x40008550u; /* ets_update_cpu_frequency */
static rom_delay_us_t volatile const rom_delay_us = (rom_delay_us_t)0x40008534u; /* ets_delay_us */

#define CPU_MHZ 160u
#define APB_HZ  80000000u

static void tx_wait_idle(void)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND * TXFIFO_SIZE; n++) { /* bounded: the FIFO's last characters */
        if (STATUS_TXFIFO(UART_STATUS) == 0u && (UART_STATUS & STATUS_TX_BUSY) == 0u) {
            break;
        }
    }
}

static void bbpll_write(uint8_t reg, uint8_t value)
{
    rom_i2c_write((uint8_t)I2C_BBPLL, (uint8_t)I2C_BBPLL_HOSTID, reg, value);
}

/* rtc_clk_bbpll_enable(), clk_ll_bbpll_set_config(320, 40) without the writes the note above
 * describes, and rtc_clk_cpu_freq_to_pll_mhz(160) */
static void clock_to_pll_160m(void)
{
    tx_wait_idle(); /* the ROM's last characters leave at the old rate */
    /* regi2c_ctrl_ll_i2c_reset() and regi2c_ctrl_ll_i2c_bbpll_enable(), as rtc_clk_init() does
     * first: the analog I2C buses off, then the BBPLL's on */
    ANA_CONFIG |= ANA_CONFIG_ALL_BUSES;
    ANA_CONFIG &= ~ANA_CONFIG_I2C_BBPLL;
    RTC_CNTL_OPTIONS0 &= ~(OPTIONS0_BIAS_I2C_FORCE_PD | OPTIONS0_BB_I2C_FORCE_PD |
                           OPTIONS0_BBPLL_FORCE_PD | OPTIONS0_BBPLL_I2C_FORCE_PD);
    rom_delay_us(10u);                          /* a cycle of the 150 kHz slow clock */
    bbpll_write(BBPLL_OC_LREF, 0x00u);          /* lref 0, div10_8 0, div_ref 0 */
    bbpll_write(BBPLL_OC_DIV_7_0, 32u);         /* div7_0 */
    bbpll_write(BBPLL_OC_DCUR, (3u << 6) | 6u); /* bw 3, dcur 6 */
    rom_delay_us(160u); /* the PLL settles: SOC_DELAY_PLL_ENABLE_WITH_32K, the longer one */
    DPORT_CPU_PER_CONF = CPU_PER_CONF_160M;
    SYSCON_PLL_TICK = 80u - 1u; /* REF_TICK at 1 MHz from the 80 MHz APB clock */
    RTC_CNTL_CLK_CONF = (RTC_CNTL_CLK_CONF & ~CLK_CONF_SOC_CLK_SEL_MASK) | CLK_CONF_SOC_CLK_SEL_PLL;
    RTC_CNTL_STORE5 = ((APB_HZ >> 12) & 0xFFFFu) | (((APB_HZ >> 12) & 0xFFFFu) << 16);
    rom_cpu_freq(CPU_MHZ);
    rom_delay_us(30u);
    /* UART0 from APB_CLK, 80 MHz: (80 MHz x 16) / 115200 = 11111, 694 and 7/16 */
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

/* Before the port's reset (a fault check's, A89): the line in the FIFO reaches the host. */
void emb_board_reset_prepare(void)
{
    tx_wait_idle();
}

void emb_board_exit(int code)
{
    (void)code; /* no host to report it to */
    tx_wait_idle();
    emb_xt_halt();
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

#if CONFIG_EMB_DRV_DMA
/* No DMA controller is described for this board: the DMA class refuses every buffer. */
const emb_drv_dma_region_t emb_board_dma_regions[1] = {{0u, 0u}};
const unsigned emb_board_dma_region_count = 0u;
#endif
