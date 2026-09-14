#include "bsp_tft_port.h"

#include "pin_config.h"

void TftPort_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC, ENABLE);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;

    gpio.GPIO_Pin = PIN_TFT_DC_PIN;
    GPIO_Init(PIN_TFT_DC_PORT, &gpio);
    gpio.GPIO_Pin = PIN_TFT_RES_PIN;
    GPIO_Init(PIN_TFT_RES_PORT, &gpio);
    gpio.GPIO_Pin = PIN_TFT_BL_PIN;
    GPIO_Init(PIN_TFT_BL_PORT, &gpio);

    GPIO_ResetBits(PIN_TFT_DC_PORT, PIN_TFT_DC_PIN);
    GPIO_SetBits(PIN_TFT_RES_PORT, PIN_TFT_RES_PIN);
    /* 背光默认点亮（高=亮）。板子上背光 MOS 已拆除，PB0 直接驱动背光。 */
    GPIO_SetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
}

void TftPort_SetDataMode(int data_mode)
{
    if (data_mode) GPIO_SetBits(PIN_TFT_DC_PORT, PIN_TFT_DC_PIN);
    else GPIO_ResetBits(PIN_TFT_DC_PORT, PIN_TFT_DC_PIN);
}

void TftPort_SetReset(int high)
{
    if (high) GPIO_SetBits(PIN_TFT_RES_PORT, PIN_TFT_RES_PIN);
    else GPIO_ResetBits(PIN_TFT_RES_PORT, PIN_TFT_RES_PIN);
}

void TftPort_SetBacklight(int enabled)
{
    if (enabled) GPIO_SetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
    else GPIO_ResetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
}

/* 背光 PWM 初始化（TIM3_CH3，与蜂鸣器共用 TIM3 时基） */
void Backlight_Init(void)
{
    GPIO_InitTypeDef g;
    /* PB0 从 GPIO 改为 AF_PP，由 TIM3_CH3 驱动 */
    g.GPIO_Pin = PIN_TFT_BL_PIN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_TFT_BL_PORT, &g);

    TIM_OCInitTypeDef o;
    o.TIM_OCMode = TIM_OCMode_PWM1;
    o.TIM_OutputState = TIM_OutputState_Enable;
    o.TIM_Pulse = 0;
    o.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC3Init(TIM3, &o);
    TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);
}

/* 设置背光亮度 0-100（PWM 占空比） */
void TFT_SetBrightness(uint8_t pct)
{
    if (pct > 100U) pct = 100U;
    /* gamma2 校正：LED 视觉非线性，低亮度段更敏感。
     * pulse = pct²/10，pct=100→1000(100%), 50→250(25%), 20→40(4%)明显变暗 */
    uint16_t pulse = (uint16_t)(((uint32_t)pct * pct) / 10U);
    if (pulse > 999) pulse = 999;
    TIM_SetCompare3(TIM3, pulse);
}

void TftPort_DelayMs(uint32_t delay_ms)
{
    volatile uint32_t count;
    uint32_t loops_per_ms = SystemCoreClock / 8000U;

    while (delay_ms-- != 0U) {
        for (count = 0U; count < loops_per_ms; ++count) {
            __NOP();
        }
    }
}
