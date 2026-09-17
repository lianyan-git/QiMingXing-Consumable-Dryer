#ifndef BOOTLOADER_BUILD
#include "bsp_ntc.h"

/* NTC 校准偏移（单位 0.1℃）：显示值 = 计算值 + 偏移。
 * 实测比 SHT40 低约 2℃（NTC 的 B 值/R25/上拉阻值与假定值存在个体容差），
 * 默认 +20 = +2.0℃；如用参考温度计校验后需微调改此值。
 * 注意：该偏移同样作用于 PTC 过热保护阈值（85℃），修正后保护点更准确。 */
#define NTC_CAL_OFFSET_10C  (0)   /* 偏移归零: 显示=原始换算 (如需校准按参考温度计调) */

#include "pin_config.h"
#include "stm32f10x.h"
#include <math.h>

/* 100K/B3950 NTC + 10K pull-up @ 3.3V
 * V_REF(ADC) = V_pullup(pullup) = 3.3V
 * ADC = Rntc * 4095 / (Rntc + 10000), VREF 12bit
 * Rntc = 10000 * ADC / (4095 - ADC)
 * 温度换算按 B 参数公式 + VREFINT 校准 */

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

    /* 异常哨兵必须移出真实测量域 [-400,2000](= -40.0..200.0℃)：
     * 旧值 -200/1200 与真实 -20℃/120℃ 同值，120℃以上读数会被 IsValid
     * 误判异常 → 烘干 2s 误触发安全停机（ptc_max 可配 40~160℃ 时必现）。
     * 160℃ 的绝对保护在控制层(control_update/校准 abort)，传感器只负责测量。 */
    if (adc >= 4090) return NTC_ERR_OPEN;    /* 开路/线松 */
    if (adc <= 10)   return NTC_ERR_SHORT;   /* 短路 */

    float R = 10000.0f * (float)adc / (4095.0f - (float)adc);
    float invT = 1.0f / 298.15f + (1.0f / 3950.0f) * logf(R / 100000.0f);
    float tempC = 1.0f / invT - 273.15f;

    if (tempC < -40.0f) tempC = -40.0f;
    if (tempC > 200.0f) tempC = 200.0f;   /* 测量域到 200℃；160℃软件绝对闸在控制层 */

    /* 轻量平滑（10Hz 采样下响应更快，避免腔内温度"一度一度慢慢爬"） */
    static float filtered = -999.0f;
    if (filtered < -100.0f) filtered = tempC;   /* 首帧直通 */
    else filtered = filtered * 0.6f + tempC * 0.4f;

    return (int16_t)(filtered * 10.0f) + NTC_CAL_OFFSET_10C;
}

uint8_t NTC_IsOverTemp(void)
{
    int16_t temp = NTC_GetTemperature();
    return (temp >= (int16_t)(NTC_OVERTEMP_THRESHOLD * 10)) ? 1 : 0;
}

uint8_t NTC_IsValid(void)
{
    int16_t t = NTC_GetTemperature();
    /* 有效域=真实测量域 -40.0..200.0℃；哨兵(-9999/9999)天然在域外 */
    return (t >= NTC_READ_LO_10C && t <= NTC_READ_HI_10C) ? 1 : 0;
}
#endif