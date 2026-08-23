#ifndef BOOTLOADER_BUILD
#include "bsp_ptc.h"
#include "bsp_fan.h"
#include "bsp_ntc.h"
#include "bsp_w25q128.h"
#include "pin_config.h"
#include "system_config.h"
#include "system_time.h"
#include "stm32f10x.h"

static uint8_t autotune_running = 0;
static uint8_t autotune_done = 0;
static uint8_t autotune_progress = 0;
static uint32_t autotune_phase_start = 0;
static uint8_t autotune_phase = 0;
static float measured_kp = 0.0f, measured_ki = 0.0f, measured_kd = 0.0f;
static float temp_peak = 0.0f, temp_prev_peak = 0.0f;
static uint32_t peak_time = 0, prev_peak_time = 0;
static float oscillation_amplitude = 0.0f;
static float oscillation_period = 0.0f;
static uint8_t peak_count = 0;

void PTC_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    g.GPIO_Pin = PIN_PTC_PWM_PIN;
    g.GPIO_Mode = GPIO_Mode_AF_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_PTC_PWM_PORT, &g);

    TIM_TimeBaseInitTypeDef t;
    t.TIM_Prescaler = 71;
    t.TIM_Period = 999;
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    t.TIM_CounterMode = TIM_CounterMode_Up;
    t.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(PTC_PWM_TIM, &t);

    TIM_OCInitTypeDef o;
    o.TIM_OCMode = TIM_OCMode_PWM1;
    o.TIM_OutputState = TIM_OutputState_Enable;
    o.TIM_Pulse = 0;
    o.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(PTC_PWM_TIM, &o);
    TIM_OC1PreloadConfig(PTC_PWM_TIM, TIM_OCPreload_Enable);

    TIM_CtrlPWMOutputs(PTC_PWM_TIM, ENABLE);
    TIM_Cmd(PTC_PWM_TIM, ENABLE);
}

void PTC_SetPower(uint8_t percent)
{
    if (percent > 100) percent = 100;
    TIM_SetCompare1(PTC_PWM_TIM, (percent * 1000) / 100);
}

void PTC_PID_AutotuneStart(void)
{
    PTC_PID_AutotuneStartWithFan(100);   /* 风扇恒全功率 */
}

void PTC_PID_AutotuneStartWithFan(uint8_t fan_pct)
{
    autotune_running = 1;
    autotune_done = 0;
    autotune_progress = 0;
    autotune_phase = 0;
    autotune_phase_start = SystemTime_Millis();
    temp_peak = 0.0f;
    temp_prev_peak = 0.0f;
    peak_time = 0;
    prev_peak_time = 0;
    oscillation_amplitude = 0.0f;
    oscillation_period = 0.0f;
    peak_count = 0;
    measured_kp = 0.0f;
    measured_ki = 0.0f;
    measured_kd = 0.0f;

    Fan_SetSpeed(fan_pct);
    PTC_SetPower(100);
}

uint8_t PTC_PID_AutotuneProcess(void)
{
    if (!autotune_running || autotune_done) return 0;

    float temp = (float)NTC_GetTemperature() / 10.0f;
    uint32_t now = SystemTime_Millis();
    uint32_t elapsed = now - autotune_phase_start;

    autotune_progress = (uint8_t)(elapsed / 300);

    if (autotune_phase == 0) {
        if (temp >= (float)(g_sys.params.ptc_max_temp - 5)) {
            PTC_SetPower(0);
            autotune_phase = 1;
            autotune_phase_start = now;
        }
    } else if (autotune_phase == 1) {
        if (temp <= (float)(g_sys.params.ptc_max_temp - 15)) {
            PTC_SetPower(100);
            autotune_phase = 2;
            autotune_phase_start = now;
        }
    }

    static float last_temp = 0;
    static uint8_t rising = 0;
    /* 峰值检测：跟踪上升/下降沿，记录极大值点
     * （修复原实现 temp_peak 初值 0 导致正常加热难进入峰值统计的问题） */
    if (temp > last_temp) {
        if (!rising) {
            rising = 1;               /* 由降转升，前一点是谷底，可能之前有峰值 */
        }
    } else if (temp < last_temp) {
        if (rising) {
            rising = 0;               /* 由升转降，前一点是峰值 */
            if (peak_count == 0) {
                temp_prev_peak = temp_peak;
                temp_peak = last_temp;
                prev_peak_time = peak_time;
                peak_time = now;
            } else {
                temp_prev_peak = temp_peak;
                temp_peak = last_temp;
                prev_peak_time = peak_time;
                peak_time = now;
            }
            peak_count++;
            if (peak_count >= 2) {
                oscillation_period = (float)(peak_time - prev_peak_time) / 1000.0f;
                oscillation_amplitude = (temp_peak - temp_prev_peak);
            }
        }
    }
    last_temp = temp;

    if (peak_count >= 4 && oscillation_period > 0.5f) {
        float ku = (4.0f * 100.0f) / (3.14159f * oscillation_amplitude);
        float tu = oscillation_period;

        measured_kp = 0.6f * ku;
        measured_ki = 2.0f * measured_kp / tu;
        measured_kd = measured_kp * tu / 8.0f;

        PTC_SetPower(0);
        Fan_SetSpeed(0);
        autotune_done = 1;
        autotune_running = 0;
        autotune_progress = 100;

        g_sys.params.pid_kp = measured_kp;
        g_sys.params.pid_ki = measured_ki;
        g_sys.params.pid_kd = measured_kd;
        System_SaveParams();
    }

    if (elapsed > 60000) {
        PTC_SetPower(0);
        Fan_SetSpeed(0);
        autotune_done = 1;
        autotune_running = 0;
        autotune_progress = 100;
    }

    return 1;
}

uint8_t PTC_PID_AutotuneIsDone(void)
{
    return autotune_done;
}

void PTC_PID_GetParams(float *kp, float *ki, float *kd)
{
    *kp = measured_kp;
    *ki = measured_ki;
    *kd = measured_kd;
}

uint8_t PTC_PID_AutotuneGetProgress(void)
{
    return autotune_progress > 100 ? 100 : autotune_progress;
}

static uint8_t temp_pid_running = 0;
static uint8_t temp_pid_done = 0;
static uint8_t temp_pid_progress = 0;
static float temp_pid_target = 50.0f;
static float temp_pid_peak = 0.0f, temp_pid_prev_peak = 0.0f;
static uint32_t temp_pid_peak_time = 0, temp_pid_prev_peak_time = 0;
static float temp_pid_amplitude = 0.0f, temp_pid_period = 0.0f;
static uint8_t temp_pid_peak_count = 0;
static uint8_t temp_pid_phase = 0;
static uint32_t temp_pid_phase_start = 0;
static float temp_pid_last = 0;

void PTC_TempPID_AutotuneStart(float target_temp)
{
    temp_pid_running = 1;
    temp_pid_done = 0;
    temp_pid_progress = 0;
    temp_pid_target = target_temp;
    temp_pid_phase = 0;
    temp_pid_phase_start = SystemTime_Millis();
    temp_pid_peak = 0;
    temp_pid_prev_peak = 0;
    temp_pid_peak_time = 0;
    temp_pid_prev_peak_time = 0;
    temp_pid_amplitude = 0;
    temp_pid_period = 0;
    temp_pid_peak_count = 0;
    temp_pid_last = 0.0f;

    Fan_SetSpeed(100);
    PTC_SetPower(100);
}

uint8_t PTC_TempPID_AutotuneProcess(void)
{
    if (!temp_pid_running || temp_pid_done) return 0;

    float temp = g_sys.current_temp;
    uint32_t now = SystemTime_Millis();
    uint32_t elapsed = now - temp_pid_phase_start;

    temp_pid_progress = (uint8_t)(elapsed / 300);

    if (temp_pid_phase == 0) {
        if (temp >= temp_pid_target - 2.0f) {
            PTC_SetPower(0);
            temp_pid_phase = 1;
            temp_pid_phase_start = now;
        }
    } else if (temp_pid_phase == 1) {
        if (temp <= temp_pid_target - 10.0f) {
            PTC_SetPower(100);
            temp_pid_phase = 2;
            temp_pid_phase_start = now;
        }
    }

    static float temp_pid_rising_state = 0;
    uint8_t pid_rising = 0;
    /* 峰值检测：跟踪上升/下降沿（修复原实现峰值统计难进入的问题） */
    if (temp > temp_pid_last) {
        pid_rising = 1;
        temp_pid_rising_state = 1;
    } else if (temp < temp_pid_last) {
        pid_rising = 0;
        temp_pid_rising_state = 0;
    }
    if (pid_rising == 0 && temp_pid_rising_state == 0 && temp_pid_last > temp_pid_peak) {
        /* 已到峰值点（升转降） */
        temp_pid_prev_peak = temp_pid_peak;
        temp_pid_peak = temp_pid_last;
        temp_pid_prev_peak_time = temp_pid_peak_time;
        temp_pid_peak_time = now;
        temp_pid_peak_count++;
        if (temp_pid_peak_count >= 2) {
            temp_pid_period = (float)(temp_pid_peak_time - temp_pid_prev_peak_time) / 1000.0f;
            temp_pid_amplitude = (temp_pid_peak - temp_pid_prev_peak);
        }
        temp_pid_rising_state = 2;   /* 已记录峰值 */
    }
    if (pid_rising == 1 && temp_pid_rising_state == 2) {
        temp_pid_rising_state = 1;   /* 又开始上升 */
    }
    temp_pid_last = temp;

    if (temp_pid_peak_count >= 4 && temp_pid_period > 0.5f) {
        float ku = (4.0f * 100.0f) / (3.14159f * temp_pid_amplitude);
        float tu = temp_pid_period;

        g_sys.params.pid_kp = 0.6f * ku;
        g_sys.params.pid_ki = 2.0f * g_sys.params.pid_kp / tu;
        g_sys.params.pid_kd = g_sys.params.pid_kp * tu / 8.0f;

        PTC_SetPower(0);
        Fan_SetSpeed(0);
        temp_pid_done = 1;
        temp_pid_running = 0;
        temp_pid_progress = 100;
        System_SaveParams();
    }

    if (elapsed > 60000) {
        PTC_SetPower(0);
        Fan_SetSpeed(0);
        temp_pid_done = 1;
        temp_pid_running = 0;
        temp_pid_progress = 100;
    }

    return 1;
}

uint8_t PTC_TempPID_AutotuneIsDone(void)
{
    return temp_pid_done;
}

uint8_t PTC_TempPID_AutotuneGetProgress(void)
{
    return temp_pid_progress > 100 ? 100 : temp_pid_progress;
}
#endif /* BOOTLOADER_BUILD */

