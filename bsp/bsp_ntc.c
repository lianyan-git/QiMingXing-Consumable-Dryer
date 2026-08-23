#ifndef BOOTLOADER_BUILD
#include "bsp_ntc.h"
#include "pin_config.h"
#include "stm32f10x.h"
#include <math.h>

/* 100K/B3950 NTC + 10K? ??? 3.3V
 * ???V_REF(ADC??) = V_pullup(????) = 3.3V ? ??? V ??
 * ADC = Rntc * 4095 / (Rntc + 10000)?? VREF ??
 * Rntc = 10000 * ADC / (4095 - ADC)
 * ??? B ???????????? LUT ???? VREFINT */

static uint16_t adc_read_channel(uint8_t ch)
{
    volatile uint32_t guard = 0;
    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) {
        if (++guard > 100000U) return 0;
    }
    return ADC_GetConversionValue(ADC1);
}

void NTC_Init(void)
{
    ADC_InitTypeDef adc;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin = PIN_NTC_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(PIN_NTC_PORT, &gpio);

    ADC_DeInit(ADC1);
    ADC_StructInit(&adc);
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);

    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    adc_read_channel(ADC_Channel_2);   /* ?????? */
}

uint16_t NTC_ReadADC(void)
{
    return adc_read_channel(ADC_Channel_2);
}

int16_t NTC_GetTemperature(void)
{
    /* ?????????? ADC ?? */
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read_channel(ADC_Channel_2);
    uint16_t adc = (uint16_t)(sum / 8);

    if (adc >= 4090) return -200;
    if (adc <= 10)   return 1200;

    float R = 10000.0f * (float)adc / (4095.0f - (float)adc);
    float invT = 1.0f / 298.15f + (1.0f / 3950.0f) * logf(R / 100000.0f);
    float tempC = 1.0f / invT - 273.15f;

    if (tempC < -40.0f) tempC = -40.0f;
    if (tempC > 125.0f) tempC = 125.0f;

    /* ?????????? 25%???? 75%????? */
    static float filtered = -999.0f;
    if (filtered < -100.0f) filtered = tempC;   /* ?????? */
    else filtered = filtered * 0.75f + tempC * 0.25f;

    return (int16_t)(filtered * 10.0f);
}

uint8_t NTC_IsOverTemp(void)
{
    int16_t temp = NTC_GetTemperature();
    return (temp >= (int16_t)(NTC_OVERTEMP_THRESHOLD * 10)) ? 1 : 0;
}
#endif