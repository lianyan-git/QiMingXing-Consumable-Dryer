#include "board.h"

#include "pin_config.h"
#include "bsp_ntc.h"
#include "stm32f10x.h"

void Board_ForceHeaterOff(void)
{
    GPIO_ResetBits(PIN_PTC_PWM_PORT, PIN_PTC_PWM_PIN);
}

void Board_EarlyInit(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    /* Set the output latch low before PA8 changes from reset input to output. */
    Board_ForceHeaterOff();
    gpio.GPIO_Pin = PIN_PTC_PWM_PIN;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(PIN_PTC_PWM_PORT, &gpio);
    Board_ForceHeaterOff();

    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
}

#ifndef BOOTLOADER_BUILD
void Board_NTC_Init(void)
{
    NTC_Init();
}

void Board_Init(void)
{
    Board_EarlyInit();
    Board_NTC_Init();
}
#endif

void Watchdog_Init(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_64);
    IWDG_SetReload(2500U);   /* 约 4 秒窗口，给 OTA 刷写/校验留足余量 */
    IWDG_ReloadCounter();
    IWDG_Enable();
}

void Watchdog_Kick(void)
{
    IWDG_ReloadCounter();
}
