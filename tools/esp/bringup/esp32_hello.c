/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */

/*
 * The classic ESP32's bring-up probe (docs/ports/xtensa.md): no kernel and no
 * ESP-IDF. The boot ROM loads it from flash offset 0x1000 and calls
 * call_start_cpu0 on its own stack, with UART0 at 115200 baud. It stops the
 * three watchdogs the ROM leaves guarding a flash boot, then prints a
 * numbered line on UART0 about twice a second, forever. It proves the
 * toolchain (EmbCC's xtensa-none-elf, windowed ABI), the image
 * (tools/esp/esp_image.py --chip esp32), the flasher (embflash --esp) and the
 * register addresses before the core port is written.
 *
 * Addresses: ESP-IDF v5.4, components/soc/esp32/register/soc/.
 */
typedef unsigned int u32;

#define REG(a) (*(volatile u32 *)(a))

#define RTC_WDT_CONFIG0   0x3FF4808Cu /* RTC_CNTL_WDTCONFIG0_REG */
#define RTC_WDT_PROTECT   0x3FF480A4u /* RTC_CNTL_WDTWPROTECT_REG */
#define TIMG0_WDT_CONFIG0 0x3FF5F048u /* TIMG_WDTCONFIG0_REG(0) */
#define TIMG0_WDT_PROTECT 0x3FF5F064u /* TIMG_WDTWPROTECT_REG(0) */
#define TIMG1_WDT_CONFIG0 0x3FF60048u
#define TIMG1_WDT_PROTECT 0x3FF60064u
#define WDT_KEY           0x50D83AA1u /* the write key of all three */

#define UART0_FIFO   0x3FF40000u
#define UART0_STATUS 0x3FF4001Cu /* TXFIFO_CNT in bits 23:16, of 128 */

/* IRAM takes only 32-bit loads, so byte data must live in DRAM: .data is
 * stored after the code and loaded at 0x3FFF0000 (embld -Tdata). */
#define DRAM __attribute__((section(".data")))

static DRAM char msg_hello[] = "emb: hello from the ESP32 (Xtensa LX6), line ";
static DRAM char msg_eol[] = "\r\n";
static DRAM char digits[11];

static void put_char(char c)
{
    while (((REG(UART0_STATUS) >> 16) & 0xFFu) >= 120u) {
    }
    REG(UART0_FIFO) = (u32)(unsigned char)c;
}

static void put_string(const char *s)
{
    while (*s != '\0') {
        put_char(*s++);
    }
}

static void put_u32(u32 v)
{
    int i = 10;

    digits[i] = '\0';
    do {
        digits[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u && i > 0);
    put_string(&digits[i]);
}

static void wdt_off(u32 protect, u32 config0)
{
    REG(protect) = WDT_KEY;
    REG(config0) = 0u;
    REG(protect) = 0u;
}

void call_start_cpu0(void)
{
    wdt_off(RTC_WDT_PROTECT, RTC_WDT_CONFIG0);
    wdt_off(TIMG0_WDT_PROTECT, TIMG0_WDT_CONFIG0);
    wdt_off(TIMG1_WDT_PROTECT, TIMG1_WDT_CONFIG0);
    for (u32 n = 0u;; n++) {
        put_string(msg_hello);
        put_u32(n);
        put_string(msg_eol);
        for (volatile u32 d = 0u; d < 4000000u; d++) {
        }
    }
}
