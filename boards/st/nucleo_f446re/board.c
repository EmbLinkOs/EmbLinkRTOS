/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * nucleo_f446re (MB1136 C-03, UM1724): the STM32F446RE at 180 MHz from the PLL, its input the
 * ST-LINK's 8 MHz MCO on PH0 in HSE bypass (solder bridges SB16 and SB50 as shipped); console
 * on USART2 (PA2/PA3, AF7), which the ST-LINK carries as its virtual COM port; LD2 on PA5.
 * Register addresses and bits from RM0390 Rev 6. A hardware target: the run ends in a WFI
 * loop with interrupts masked, since a part with no debugger attached has nowhere to exit to
 * (an emulator ends the run at the console's EMB_TEST_END).
 */
#include <emb/fault.h>

#include <emb_board.h>
#include <emb_cortex_m.h>
#include <hw_config.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define RCC_BASE       0x40023800u
#define RCC_CR         REG32(RCC_BASE + 0x00u)
#define RCC_PLLCFGR    REG32(RCC_BASE + 0x04u)
#define RCC_CFGR       REG32(RCC_BASE + 0x08u)
#define RCC_AHB1ENR    REG32(RCC_BASE + 0x30u)
#define RCC_APB1ENR    REG32(RCC_BASE + 0x40u)
#define CR_HSEON       (1u << 16)
#define CR_HSERDY      (1u << 17)
#define CR_HSEBYP      (1u << 18)
#define CR_PLLON       (1u << 24)
#define CR_PLLRDY      (1u << 25)
#define CFGR_SW_MASK   0x3u
#define CFGR_SW_PLL    0x2u
#define CFGR_SWS_MASK  (0x3u << 2)
#define CFGR_SWS_PLL   (0x2u << 2)
#define CFGR_PPRE1_4   (0x5u << 10) /* APB1 = HCLK / 4: 45 MHz, its limit */
#define CFGR_PPRE2_2   (0x4u << 13) /* APB2 = HCLK / 2: 90 MHz, its limit */
#define AHB1ENR_GPIOA  (1u << 0)
#define APB1ENR_USART2 (1u << 17)
#define APB1ENR_PWR    (1u << 28)

/* VCO input 8 MHz / M 8 = 1 MHz, VCO 1 MHz x N 360 = 360 MHz; SYSCLK = VCO / P 2 = 180 MHz,
 * the 48 MHz domain VCO / Q 7 (51.4 MHz: no USB here), I2S and SAI VCO / R 2. */
#define PLL_M       8u
#define PLL_N       360u
#define PLL_P_DIV2  0u
#define PLL_Q       7u
#define PLL_R       2u
#define PLLCFGR_HSE (1u << 22)
#define PLLCFGR_VALUE \
    (PLL_M | (PLL_N << 6) | (PLL_P_DIV2 << 16) | PLLCFGR_HSE | (PLL_Q << 24) | (PLL_R << 28))

#define PWR_BASE    0x40007000u
#define PWR_CR      REG32(PWR_BASE + 0x00u)
#define PWR_CSR     REG32(PWR_BASE + 0x04u)
#define PWR_VOS_1   (0x3u << 14) /* scale 1: 168 MHz, 180 MHz with over-drive */
#define PWR_ODEN    (1u << 16)
#define PWR_ODSWEN  (1u << 17)
#define CSR_ODRDY   (1u << 16)
#define CSR_ODSWRDY (1u << 17)

#define FLASH_ACR    REG32(0x40023C00u)
#define ACR_LATENCY5 5u /* 2.7-3.6 V at 150-180 MHz (RM0390 Table 5) */
#define ACR_LAT_MASK 0xFu
#define ACR_PRFTEN   (1u << 8)
#define ACR_ICEN     (1u << 9)
#define ACR_DCEN     (1u << 10)

#define GPIOA_BASE  0x40020000u
#define GPIOA_MODER REG32(GPIOA_BASE + 0x00u)
#define GPIOA_ODR   REG32(GPIOA_BASE + 0x14u)
#define GPIOA_BSRR  REG32(GPIOA_BASE + 0x18u)
#define GPIOA_AFRL  REG32(GPIOA_BASE + 0x20u)
#define MODE_OUT    1u
#define MODE_AF     2u
#define AF7         7u

#define USART_SR  REG32(EMB_HW_USART2_BASE + 0x00u)
#define USART_DR  REG32(EMB_HW_USART2_BASE + 0x04u)
#define USART_BRR REG32(EMB_HW_USART2_BASE + 0x08u)
#define USART_CR1 REG32(EMB_HW_USART2_BASE + 0x0Cu)
#define SR_TC     (1u << 6)
#define SR_TXE    (1u << 7)
#define CR1_TE    (1u << 3)
#define CR1_UE    (1u << 13)

#define HSI_HZ 16000000u
/* The ready flags' bound: HAL_RCC's HSE start-up timeout is 100 ms, about 400 000 polls at
 * HSI's 16 MHz; PLL lock and the over-drive switch take microseconds. */
#define WAIT_BOUND 500000u
/* One character at 115200 baud from a 180 MHz core is ~15 600 cycles, a few thousand polls. */
#define TX_BOUND 100000u

/* Polls until (reg & mask) == want, at most WAIT_BOUND times; nonzero once it holds. */
static int wait_for(volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    uint32_t n;
    for (n = 0u; n < WAIT_BOUND; n++) { /* bounded by WAIT_BOUND */
        if ((*reg & mask) == want) {
            return 1;
        }
    }
    return 0;
}

/* HSE bypass, the PLL, over-drive, five wait states, then the switch (RM0390 §5.1.4, §6.3). */
static int clock_init(void)
{
    RCC_APB1ENR |= APB1ENR_PWR;
    (void)RCC_APB1ENR; /* the clock reaches PWR before its first access */
    PWR_CR = (PWR_CR & ~PWR_VOS_1) | PWR_VOS_1;
    RCC_CR |= CR_HSEBYP;
    RCC_CR |= CR_HSEON;
    if (wait_for(&RCC_CR, CR_HSERDY, CR_HSERDY) == 0) {
        return 0; /* no MCO on PH0: SB16 or SB50 open, or an ST-LINK that does not drive it */
    }
    RCC_PLLCFGR = PLLCFGR_VALUE;
    RCC_CR |= CR_PLLON;
    if (wait_for(&RCC_CR, CR_PLLRDY, CR_PLLRDY) == 0) {
        return 0;
    }
    PWR_CR |= PWR_ODEN;
    if (wait_for(&PWR_CSR, CSR_ODRDY, CSR_ODRDY) == 0) {
        return 0;
    }
    PWR_CR |= PWR_ODSWEN;
    if (wait_for(&PWR_CSR, CSR_ODSWRDY, CSR_ODSWRDY) == 0) {
        return 0;
    }
    FLASH_ACR = ACR_LATENCY5 | ACR_PRFTEN | ACR_ICEN | ACR_DCEN;
    if (wait_for(&FLASH_ACR, ACR_LAT_MASK, ACR_LATENCY5) == 0) {
        return 0;
    }
    RCC_CFGR = (RCC_CFGR & ~(CFGR_SW_MASK | (0x3Fu << 10) | (0xFu << 4))) | CFGR_PPRE1_4 |
               CFGR_PPRE2_2;
    RCC_CFGR = (RCC_CFGR & ~CFGR_SW_MASK) | CFGR_SW_PLL;
    return wait_for(&RCC_CFGR, CFGR_SWS_MASK, CFGR_SWS_PLL);
}

static void console_init(uint32_t pclk)
{
    RCC_APB1ENR |= APB1ENR_USART2;
    (void)RCC_APB1ENR;
    USART_CR1 = 0u;
    USART_BRR = (pclk + EMB_HW_CONSOLE_BAUD / 2u) / EMB_HW_CONSOLE_BAUD; /* 16x oversampling */
    USART_CR1 = CR1_UE | CR1_TE; /* 8 data bits, no parity; 1 stop bit in CR2's reset value */
}

void emb_board_init(void)
{
    int clocked = clock_init();
    RCC_AHB1ENR |= AHB1ENR_GPIOA;
    (void)RCC_AHB1ENR;
    /* PA2 and PA3 to USART2 (AF7), PA5 an output */
    GPIOA_AFRL = (GPIOA_AFRL & ~((0xFu << 8) | (0xFu << 12))) | (AF7 << 8) | (AF7 << 12);
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 4) | (3u << 6) | (3u << (EMB_HW_LED_PIN * 2u)))) |
                  (MODE_AF << 4) | (MODE_AF << 6) | (MODE_OUT << (EMB_HW_LED_PIN * 2u));
    if (clocked != 0) {
        console_init(EMB_HW_APB1_HZ);
        return;
    }
    /* still on HSI: say why, then stop, since the kernel's tick assumes 180 MHz */
    console_init(HSI_HZ);
    emb_board_puts("\nemb: nucleo_f446re: no 180 MHz clock (is the ST-LINK's MCO on PH0?)\n");
    emb_board_exit(3);
}

void emb_board_led_toggle(void)
{
    uint32_t bit = 1u << EMB_HW_LED_PIN;
    GPIOA_BSRR = ((GPIOA_ODR & bit) != 0u) ? (bit << 16) : bit; /* one write: no lost update */
}

static void put_char(char c)
{
    uint32_t n;
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: one character time */
        if ((USART_SR & SR_TXE) != 0u) {
            break;
        }
    }
    USART_DR = (uint32_t)(unsigned char)c;
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
    uint32_t n;
    (void)code;                       /* no host to report it to */
    for (n = 0u; n < TX_BOUND; n++) { /* bounded by TX_BOUND: the last character's frame */
        if ((USART_SR & SR_TC) != 0u) {
            break;
        }
    }
    emb_cm_halt();
}

void emb_board_fault_report(const emb_fault_info_t *info)
{
    emb_board_puts("\nemb: kernel fault class ");
    emb_board_put_u32((info != NULL) ? info->fault_class : 0u);
    emb_board_puts(" code ");
    emb_board_put_u32((info != NULL) ? info->code : 0u);
    emb_board_puts(" pc ");
    emb_board_put_u32((info != NULL) ? (uint32_t)info->pc : 0u);
    emb_board_puts(" at ");
    emb_board_puts((info != NULL && info->where != NULL) ? info->where : "?");
    emb_board_puts("\n");
    emb_board_exit(2);
}
