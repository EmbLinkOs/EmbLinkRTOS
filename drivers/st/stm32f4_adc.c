/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* STM32F4 ADC1 reading the internal temperature sensor on an external trigger (RM0390 Rev 6
 * §13): one regular conversion of channel 18 per TIM2 TRGO edge, its end-of-conversion
 * interrupt on, and the factory calibration (DS10693 §6.3.22) to turn a reading into degrees. */
#include <emb_stm32_drivers.h>

#define REG32(a)       (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a)       (*(const volatile uint16_t *)(uintptr_t)(a))
#define ADC_SR         0x00u
#define ADC_CR1        0x04u
#define ADC_CR2        0x08u
#define ADC_SMPR1      0x0Cu
#define ADC_SQR1       0x2Cu
#define ADC_SQR3       0x34u
#define ADC_DR         0x4Cu
#define ADC_CCR        0x04u /* in the common block */
#define SR_EOC         (1u << 1)
#define SR_OVR         (1u << 5)
#define CR1_EOCIE      (1u << 5)
#define CR1_OVRIE      (1u << 26)
#define CR2_ADON       (1u << 0)
#define CR2_EXTSEL_T2  (6u << 24) /* TIM2 TRGO (RM0390 Table 69) */
#define CR2_EXTEN_RISE (1u << 28)
#define CCR_ADCPRE_4   (1u << 16) /* PCLK2 / 4: 22.5 MHz from 90 MHz, under the 36 MHz limit */
#define CCR_TSVREFE    (1u << 23) /* the temperature sensor and VREFINT on */
#define CHANNEL_TEMP   18u
#define SMP_480        7u /* 480 cycles: 21 us at 22.5 MHz, over the sensor's 10 us minimum */
#define SMP18_SHIFT    24u

#define CAL_TS1 2u /* offsets in the factory calibration block */
#define CAL_TS2 4u

void emb_stm32f4_adc_temp_on_tim2(uintptr_t adc, uintptr_t common)
{
    REG32(adc + ADC_CR2) = 0u;
    REG32(common + ADC_CCR) = CCR_ADCPRE_4 | CCR_TSVREFE;
    REG32(adc + ADC_SMPR1) = SMP_480 << SMP18_SHIFT;
    REG32(adc + ADC_SQR1) = 0u; /* one conversion in the sequence */
    REG32(adc + ADC_SQR3) = CHANNEL_TEMP;
    REG32(adc + ADC_SR) = 0u;
    REG32(adc + ADC_CR1) = CR1_EOCIE | CR1_OVRIE;
    REG32(adc + ADC_CR2) = CR2_ADON | CR2_EXTSEL_T2 | CR2_EXTEN_RISE;
}

int emb_stm32f4_adc_take(uintptr_t adc, uint16_t *out)
{
    uint32_t sr = REG32(adc + ADC_SR);
    if ((sr & SR_OVR) != 0u) {
        REG32(adc + ADC_SR) = 0u; /* a conversion was lost: report it; the next one continues */
        (void)REG32(adc + ADC_DR);
        return -1;
    }
    if ((sr & SR_EOC) == 0u) {
        return 0;
    }
    *out = (uint16_t)REG32(adc + ADC_DR); /* the read clears EOC */
    return 1;
}

int32_t emb_stm32f4_temp_mc(uint16_t raw, uintptr_t factory_cal)
{
    int32_t t1 = (int32_t)REG16(factory_cal + CAL_TS1);
    int32_t t2 = (int32_t)REG16(factory_cal + CAL_TS2);
    if (t2 <= t1) {
        return INT32_MIN; /* no calibration: refuse rather than guess */
    }
    /* linear between the two points: 30 C at TS_CAL1, 110 C at TS_CAL2 (VDDA = 3.3 V) */
    return 30000 + (((int32_t)raw - t1) * 80000) / (t2 - t1);
}
