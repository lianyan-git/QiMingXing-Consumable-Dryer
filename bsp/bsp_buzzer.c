#ifndef BOOTLOADER_BUILD
#include "bsp_buzzer.h"
#include "pin_config.h"
#include "system_config.h"
#include "stm32f10x.h"

/* 载波 2kHz（此前 4kHz 实测偏小声，压电片共振更低；2kHz 体积响亮且低音量有 2 个载波周期/ms 不刺耳） */
#define BZ_PERIOD  499U   /* 72MHz/72 = 1MHz, 1MHz/500 = 2kHz */
#define BZ_HALF    250U

void Buzzer_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    g.GPIO_Pin = PIN_BUZZER_PIN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_BUZZER_PORT, &g);

    TIM_TimeBaseInitTypeDef t;
    t.TIM_Prescaler = 71;
    t.TIM_Period = BZ_PERIOD;
    t.TIM_ClockDivision = 0;
    t.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &t);

    TIM_OCInitTypeDef o;
    o.TIM_OCMode = TIM_OCMode_PWM1;
    o.TIM_OutputState = TIM_OutputState_Enable;
    o.TIM_Pulse = 0;
    o.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC4Init(TIM3, &o);
    TIM_OC4PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_Cmd(TIM3, ENABLE);
}

void Buzzer_Beep(uint16_t ms)
{
    /* 音量=包络门控：固定 50% 占空比方波（振幅恒定→音色/频率不变），
     * 4kHz 载波在压电片共振点→最大音量；低音量时 1ms 含 4 个载波周期→不刺耳。
     * 每 10ms(100Hz) 周期内导通 vol*1ms → 占空比=vol/10 决定响度。 */
    uint32_t on_us  = (uint32_t)g_sys.buzzer_vol * 1000U;
    uint32_t cycle  = 10000U;
    uint32_t remain = (uint32_t)ms * 1000U;

    if (on_us == 0U) {
        { volatile uint32_t d = remain * 7U; while (d--) __NOP(); }
        TIM_SetCompare4(TIM3, 0);
        return;
    }
    while (remain > 0) {
        uint32_t on = (on_us < remain) ? on_us : remain;
        TIM_SetCompare4(TIM3, BZ_HALF);
        { volatile uint32_t d = on * 7U; while (d--) __NOP(); }
        remain -= on;
        if (remain > 0) {
            uint32_t off = (cycle > on_us) ? (cycle - on_us) : 0U;
            if (off > remain) off = remain;
            TIM_SetCompare4(TIM3, 0);
            { volatile uint32_t d = off * 7U; while (d--) __NOP(); }
            remain -= off;
        }
    }
    TIM_SetCompare4(TIM3, 0);
}

void Buzzer_SetVolume(uint8_t vol)
{
    g_sys.buzzer_vol = (vol > 10) ? 10 : vol;
}

void Buzzer_SetFreq(uint16_t freq)
{
    (void)freq;
}
#endif /* BOOTLOADER_BUILD */