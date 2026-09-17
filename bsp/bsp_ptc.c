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
static uint32_t autotune_start_ms = 0;
static uint8_t autotune_phase = 0;
static float measured_kp = 0.0f, measured_ki = 0.0f, measured_kd = 0.0f;
static float oscillation_amplitude = 0.0f;
static float oscillation_period = 0.0f;
static uint8_t peak_count = 0;

/* PA8 是否已切到 TIM1_CH1 复用。engage 只执行一次——GPIO_Init(AF_PP) 会复位整个
 * GPIOA 端口, 每次 SetPower 都执行会周期性打乱 PA8/TFT 等 GPIO 配置(影响加热与收流)。 */
static uint8_t ptc_engaged = 0;

void PTC_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    /* 掉电不彻底(灯板电容等)可能导致 RAM 残留: ptc_engaged 可能残留 1 使 engage 被跳过。
     * 这里显式清零, 并强制 PA8=GPIO 低 + CCR=0, 彻底抹掉任何残留状态。 */
    ptc_engaged = 0;

    /* PA8 保持 GPIO 推挽低(物理关断), 不切 AF_PP。
     * 只有首次 PTC_SetPower(>0) 才切到 TIM1_CH1。
     * 实测: 初始化即切 AF 时, 上电/复位窗口 PA8 被 TIM1 输出级接管, 概率输出高电平→误加热。 */
    g.GPIO_Pin = PIN_PTC_PWM_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(PIN_PTC_PWM_PORT, &g);
    GPIO_ResetBits(PIN_PTC_PWM_PORT, PIN_PTC_PWM_PIN);

    /* 配置 TIM1 时基/OC1(风扇 CH4 依赖时基), CCR1=0 */
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
    TIM_OC1PreloadConfig(PTC_PWM_TIM, TIM_OCPreload_Disable);
    TIM_CtrlPWMOutputs(PTC_PWM_TIM, ENABLE);
    TIM_Cmd(PTC_PWM_TIM, ENABLE);
    TIM_SetCompare1(PTC_PWM_TIM, 0);
    TIM_GenerateEvent(PTC_PWM_TIM, TIM_EventSource_Update);
    TIM_SetCompare1(PTC_PWM_TIM, 0);

    /* 也覆盖 CCER/BDTR 残留: 若 RAM 残留使 CH1 曾使能输出, 这里强制 CCR=0 后输出恒低 */
    TIM_SetCompare1(PTC_PWM_TIM, 0);

    /* PA8 已是 GPIO 低; TIM1 配置完成、CCR1=0 同步完成, 但未接 PA8 */
}

/* 首次 >0% 输出前把 PA8 从 GPIO 推挽低切到 TIM1_CH1 复用。
 * 只执行一次: STM32 库 GPIO_Init(AF_PP) 会复位整个 GPIOA, 反复执行会周期性
 * 打乱 PA8/TFT 等 GPIO(实测加热与上传异常)。 */
static void ptc_engage(void)
{
    if (ptc_engaged) return;
    {
        GPIO_InitTypeDef g;
        g.GPIO_Pin = PIN_PTC_PWM_PIN;
        g.GPIO_Mode = GPIO_Mode_AF_PP;
        g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(PIN_PTC_PWM_PORT, &g);
    }
    TIM_SetCompare1(PTC_PWM_TIM, 0);   /* 先钳低再放行 */
    ptc_engaged = 1;
}

void PTC_SetPower(uint8_t percent)
{
    if (percent > 100) percent = 100;
    if (percent == 0) {
        TIM_SetCompare1(PTC_PWM_TIM, 0);
        /* 硬件级保险: 0% 不只靠 CCR=0 的"定时器驱动低", 直接把 PA8 退回普通
         * 推挽输出低——AF 脚在 CCR 残留/重映射破坏/输出使能异常等任何一侧失手时
         * 都可能变成弱驱动(实测: 关烘干后 PTC 半导通、LED 偏暗), 引脚物理 0V
         * 才是真关断。下次 >0% 走 ptc_engage() 自动切回 AF。 */
        if (ptc_engaged) {
            GPIO_InitTypeDef g;
            g.GPIO_Pin = PIN_PTC_PWM_PIN;
            g.GPIO_Mode = GPIO_Mode_Out_PP;
            g.GPIO_Speed = GPIO_Speed_2MHz;
            GPIO_Init(PIN_PTC_PWM_PORT, &g);
            GPIO_ResetBits(PIN_PTC_PWM_PORT, PIN_PTC_PWM_PIN);
            ptc_engaged = 0;
        }
        return;
    }
    ptc_engage();
    TIM_SetCompare1(PTC_PWM_TIM, (percent * 1000) / 100);
}

/* 峰检测状态（元件/空气两组）。原来两组 static 都是函数内变量，跨次运行残留，
 * have_trough/last_temp 卡死 → 第二次校准只走一个峰就"进度 50 消失、参数不落"。
 * 现提升到文件级并在每次 Start 时清零。 */
static float at_last_temp = -1000.0f, at_last_trough = 0.0f;
static uint8_t at_rising = 0, at_have_trough = 0;
static uint32_t at_last_peak_time = 0;
static float tp_last_temp = -1000.0f, tp_last_trough = 0.0f;
static uint8_t tp_rising = 0, tp_have_trough = 0;
static uint32_t tp_last_peak_time = 0;

/* 自动校准阶段机（元件 at_ / 空气 tp_ 各一套）：
 * 目标 5 个计数峰（≈5 次升降循环，继电器振荡法多次测量取平均更稳），
 * 进度实时 20%/峰；第 5 峰 → 99%（后台计算+写Flash+落库）→ 100% 驻留 1.2s
 * 让页面刷新显示新 PID 值 → 结束。abort/超时也尽量用已积累的均值落盘。 */
#define AT_TARGET_PEAKS 5
static uint8_t at_stage = 0, at_meas_cnt = 0;
static float at_sum_period = 0, at_sum_amp = 0;
static uint32_t at_hold_ms = 0;
static uint8_t tp_stage = 0, tp_meas_cnt = 0;
static float tp_sum_period = 0, tp_sum_amp = 0;
static uint32_t tp_hold_ms = 0;

/* 把一组(周期,幅值)折入累计（相邻峰间才有效：period>0） */
static void tuner_accum(uint8_t *cnt, float *sum_p, float *sum_a, float period, float amp)
{
    if (period > 0.5f && amp > 0.0f) { *sum_p += period; *sum_a += amp; (*cnt)++; }
}

/* 由累计均值算保守型 PID 参数：amp/cnt, per/cnt → out_kp/ki/kd（cnt=0 时仅用传入值兜底）。
 * 两点修正（原为经典 ZN 且幅值取 100%，导致参数整体偏大约 2 倍、恒温过冲）：
 * 1. 继电器幅值 d：输出摆幅 0%↔100%，等幅振荡按中点计 d=50%，非 100%。
 *    Ku = 4d/(pi*A)。
 * 2. 参数整定改用 Tyreus-Luyben（保守型）：Kp=0.45Ku, Ti=2.2Pu, Td=Pu/6.3，
 *    比经典 ZN(0.6Ku/Pu2/Pu8) 明显温和，适配 PTC+风扇+箱体大热惯性对象。 */
static void tuner_conservative(float sum_p, float sum_a, uint8_t cnt, float period, float amp,
                     float *kp, float *ki, float *kd)
{
    float a = (cnt >= 2U) ? (sum_a / (float)cnt) : amp;
    float t = (cnt >= 2U) ? (sum_p / (float)cnt) : period;
    float ku;
    if (a < 0.5f) a = 0.5f;   /* 摆幅带压窄到 ±1℃ 后防测得过小: a 下限 0.5℃ 保 Ku 不爆 */
    if (t <= 0.0f) t = 0.5f;
    ku = (4.0f * 50.0f) / (3.14159f * a);   /* d=50%：0↔100 继电器按中点幅值 */
    *kp = 0.45f * ku;
    *ki = (*kp) / (2.2f * t);
    *kd = (*kp) * t / 6.3f;
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
    autotune_start_ms = SystemTime_Millis();
    autotune_phase = 0;

    oscillation_amplitude = 0.0f;
    oscillation_period = 0.0f;
    peak_count = 0;
    measured_kp = 0.0f;
    measured_ki = 0.0f;
    measured_kd = 0.0f;

    at_last_temp = -1000.0f; at_last_trough = 0.0f;
    at_rising = 0; at_have_trough = 0; at_last_peak_time = 0;
    at_stage = 0; at_meas_cnt = 0; at_sum_period = 0; at_sum_amp = 0; at_hold_ms = 0;

    Fan_SetSpeed(fan_pct);
    PTC_SetPower(100);
}

uint8_t PTC_PID_AutotuneProcess(void)
{
    if (!autotune_running || autotune_done) return 0;

/* 安全：PTC 元件达到硬件上限 160℃ 立即中止；中止前若已凑出有效振荡，先落盘避免整轮白跑 */
    if (at_stage == 0 && g_sys.ptc_temp >= (float)PTC_TEMP_MAX) {
        if (at_meas_cnt >= 2U) {
            float kp, ki, kd;
            tuner_conservative(at_sum_period, at_sum_amp, at_meas_cnt, 0.0f, 0.0f, &kp, &ki, &kd);
            measured_kp = kp; measured_ki = ki; measured_kd = kd;
            g_sys.params.pid_ntc_kp = kp;
            g_sys.params.pid_ntc_ki = ki;
            g_sys.params.pid_ntc_kd = kd;
            g_sys.pid_calibrated = 1;
            System_RequestSave();
        }
        autotune_done = 1; autotune_running = 0; autotune_progress = 100;
        PTC_SetPower(0); Fan_SetSpeed(0);
        return 1;
    }

    float temp = (float)NTC_GetTemperature() / 10.0f;
    uint32_t now = SystemTime_Millis();

    /* item#4 自整定独立 NTC 有效性闸: 开路线圈读域外哨兵(-99.9)会被振荡相0误判
     * "未升温"而 100% 连烧, 且 control_update 的 160 闸基于同样冻结的显示值——
     * 自整定绕开常规保护, 必须在此当场检查。越界立即断电+风扇满速+失败(不落库)。 */
    if (temp < (float)NTC_READ_LO_10C / 10.0f || temp > (float)NTC_READ_HI_10C / 10.0f) {
        PTC_SetPower(0);
        Fan_SetSpeed(100);
        autotune_done = 1; autotune_running = 0; autotune_progress = 100;
        return 1;
    }

/* 振荡控制：0=升温, 1=降温 反复切换；提交/驻留阶段(at_stage>=1)保持断电 */
    if (at_stage >= 1U) {
        PTC_SetPower(0);
    } else if (autotune_phase == 0) {
        if (temp >= (float)g_sys.params.ptc_max_temp + 1.0f) {  /* 校准只在设定±1℃带内振荡(用户要求, 不再高1~3℃工作) */
            PTC_SetPower(0);
            autotune_phase = 1;
        }
    } else if (autotune_phase == 1) {
        if (temp <= (float)g_sys.params.ptc_max_temp - 1.0f) {   /* 2℃摆幅，振荡中心=目标 */
            PTC_SetPower(100);
            autotune_phase = 0;      /* 回到升温 */
        }
    }

    /* 峰/谷检测：升转降计峰，降转升记谷；幅值=峰谷半幅（恒为正），周期=相邻峰时间。
     * 滞回死区 0.3℃ + 极值锁存；状态存于文件级 at_*（Start 清零，跨次不残留）。 */
    {
        const float HYST = 0.3f;

        if (at_last_temp < -999.0f) {
            at_last_temp = temp;
        } else if (at_rising) {
            /* 加热相位：只向上追踪峰值 */
            if (temp > at_last_temp) {
                at_last_temp = temp;
            } else if (temp < at_last_temp - HYST) {
                /* 从峰值回跌超过死区 → 确认峰 */
                if (at_have_trough) {
                    if (at_last_peak_time != 0U)
                        oscillation_period = (float)(now - at_last_peak_time) / 1000.0f;
                    at_last_peak_time = now;
                    oscillation_amplitude = (at_last_temp - at_last_trough) * 0.5f;
                    if (oscillation_amplitude < 0.0f) oscillation_amplitude = -oscillation_amplitude;
                    peak_count++;
                    tuner_accum(&at_meas_cnt, &at_sum_period, &at_sum_amp,
                                oscillation_period, oscillation_amplitude);
                    at_have_trough = 0;
                }
                at_rising = 0;
                at_last_temp = temp;
            }
        } else {
            /* 降温相位：只向下追踪谷值 */
            if (temp < at_last_temp) {
                at_last_temp = temp;
            } else if (temp > at_last_temp + HYST) {
                /* 从谷底回升超过死区 → 记录谷底，转加热 */
                at_last_trough = at_last_temp;
                at_have_trough = 1;
                at_rising = 1;
                at_last_temp = temp;
            }
        }
    }

    /* 阶段机：0=测量（每确认 1 峰 +20%）→ 第5峰转 99% 提交 → 100% 驻留后结束 */
    if (at_stage == 0) {
        autotune_progress = (uint8_t)(peak_count * 20);
        if (autotune_progress > 96) autotune_progress = 96;
        if (peak_count >= AT_TARGET_PEAKS) {
            at_stage = 1;
            autotune_progress = 99;      /* 后台计算+落库中 */
        }
    }

    if (at_stage == 1) {
        /* 用 4 组(周期,幅值)均值算保守 PID；不足两组时退回单组兜底 */
        float kp, ki, kd;
        tuner_conservative(at_sum_period, at_sum_amp, at_meas_cnt,
                 oscillation_period, oscillation_amplitude, &kp, &ki, &kd);
        measured_kp = kp; measured_ki = ki; measured_kd = kd;
        g_sys.params.pid_ntc_kp = kp;
        g_sys.params.pid_ntc_ki = ki;
        g_sys.params.pid_ntc_kd = kd;
        g_sys.pid_calibrated = 1;
        System_RequestSave();            /* 写入参数文件（同步阻塞短事务） */
        PTC_SetPower(0);
        Fan_SetSpeed(0);
        autotune_progress = 100;         /* 落库完成后再显示 100 */
        at_stage = 2;
        at_hold_ms = now;
    }

    if (at_stage == 2 && (now - at_hold_ms) > 1200U) {
        /* 驻留 1.2s：页面已按新值刷新，再退出校准态 */
        autotune_done = 1;
        autotune_running = 0;
    }

    if ((now - autotune_start_ms) > 700000 && at_stage == 0) {   /* 总超时700s */
        /* 超时：已有均值数据则按均值落盘，避免空跑结束 */
        if (at_meas_cnt >= 2U) {
            float kp, ki, kd;
            tuner_conservative(at_sum_period, at_sum_amp, at_meas_cnt, 0.0f, 0.0f, &kp, &ki, &kd);
            measured_kp = kp; measured_ki = ki; measured_kd = kd;
            g_sys.params.pid_ntc_kp = kp;
            g_sys.params.pid_ntc_ki = ki;
            g_sys.params.pid_ntc_kd = kd;
            g_sys.pid_calibrated = 1;
            System_RequestSave();
        }
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
static uint32_t temp_pid_start_ms = 0;
static float temp_pid_target = 50.0f;
static float temp_pid_amplitude = 0.0f, temp_pid_period = 0.0f;
static uint8_t temp_pid_peak_count = 0;
static uint8_t temp_pid_phase = 0;

void PTC_TempPID_AutotuneStart(float target_temp)
{
    temp_pid_running = 1;
    temp_pid_done = 0;
    temp_pid_progress = 0;
    temp_pid_start_ms = SystemTime_Millis();
    temp_pid_target = target_temp;
    temp_pid_phase = 0;

    temp_pid_amplitude = 0;
    temp_pid_period = 0;
    temp_pid_peak_count = 0;

    /* 峰检测状态清零：跨次运行不残留（原为函数内 static，第二次校准必卡进度） */
    tp_last_temp = -1000.0f; tp_last_trough = 0.0f;
    tp_rising = 0; tp_have_trough = 0; tp_last_peak_time = 0;
    tp_stage = 0; tp_meas_cnt = 0; tp_sum_period = 0; tp_sum_amp = 0; tp_hold_ms = 0;

    Fan_SetSpeed(100);
    PTC_SetPower(100);
}

/* 按烘干流程驱动 PTC：元件以 ptc_max_temp 为目标（已校准用 ntc-PID，否则 bang-bang） */
static uint8_t temp_pid_ntc_power(void)
{
    if (g_sys.pid_calibrated) {
        static float t_int = 0, t_prev = 0;
        static uint32_t t_tick = 0;
        uint32_t now = SystemTime_Millis();
        float err = (float)g_sys.params.ptc_max_temp - g_sys.ptc_temp;
        float dt = (t_tick == 0U) ? 0.2f : (float)(now - t_tick) / 1000.0f;
        if (dt <= 0.0f || dt > 1.0f) dt = 0.2f;
        float der = (t_tick == 0U) ? 0.0f : (err - t_prev) / dt;
        t_int += err * dt;
        if (t_int > 50.0f) t_int = 50.0f; else if (t_int < -50.0f) t_int = -50.0f;
        t_prev = err; t_tick = now;
        float out = g_sys.params.pid_ntc_kp * err + g_sys.params.pid_ntc_ki * t_int
                  + g_sys.params.pid_ntc_kd * der;
        if (out > 100.0f) out = 100.0f; else if (out < 0.0f) out = 0.0f;
        return (uint8_t)out;
    }
    return (g_sys.ptc_temp < (float)g_sys.params.ptc_max_temp - 2.0f) ? 100U : 0U;
}

uint8_t PTC_TempPID_AutotuneProcess(void)
{
    if (!temp_pid_running || temp_pid_done) return 0;

/* 安全：PTC 元件达硬件上限 160℃ 中止（元件需高于空气目标以传热）；
 * 中止前若已有均值数据，先落盘避免整轮白跑 */
        if (tp_stage == 0 && g_sys.ptc_temp >= (float)PTC_TEMP_MAX) {
            if (tp_meas_cnt >= 2U) {
                float kp, ki, kd;
                tuner_conservative(tp_sum_period, tp_sum_amp, tp_meas_cnt, 0.0f, 0.0f, &kp, &ki, &kd);
                g_sys.params.pid_air_kp = kp;
                g_sys.params.pid_air_ki = ki;
                g_sys.params.pid_air_kd = kd;
                g_sys.pid_calibrated = 1;
                System_RequestSave();
            }
            temp_pid_done = 1; temp_pid_running = 0; temp_pid_progress = 100;
            PTC_SetPower(0); Fan_SetSpeed(0);
            return 1;
        }

        float temp = g_sys.current_temp;
        uint32_t now = SystemTime_Millis();

        /* item#4: 空气自整定的元件侧保护依赖 g_sys.ptc_temp, 而 NTC 开路/短路时
         * read_sensors 会冻结该缓存 → 上方 160 闸失效(元件实际温度不可信)。
         * 每拍独立直读 NTC 判域, 越界立即断电+风扇满速+失败(不落库)。 */
        {
            float ntc_now = (float)NTC_GetTemperature() / 10.0f;
            if (ntc_now < (float)NTC_READ_LO_10C / 10.0f ||
                ntc_now > (float)NTC_READ_HI_10C / 10.0f) {
                PTC_SetPower(0);
                Fan_SetSpeed(100);
                temp_pid_done = 1; temp_pid_running = 0; temp_pid_progress = 100;
                return 1;
            }
        }

        /* 阶段机：0=测量（每峰 +20% 实时刷新）→ 第5峰 99% 提交 → 100% 驻留后结束 */
        if (tp_stage == 0) {
            temp_pid_progress = (uint8_t)(temp_pid_peak_count * 20);
            if (temp_pid_progress > 96) temp_pid_progress = 96;
            if (temp_pid_peak_count >= AT_TARGET_PEAKS) {
                tp_stage = 1;
                temp_pid_progress = 99;
            }
        }

        if (tp_stage == 1) {
            /* 用均值计算保守 PID 并落库（阻塞式同步写，返回即已写入参数文件） */
            float kp, ki, kd;
            tuner_conservative(tp_sum_period, tp_sum_amp, tp_meas_cnt,
                     temp_pid_period, temp_pid_amplitude, &kp, &ki, &kd);
            g_sys.params.pid_air_kp = kp;
            g_sys.params.pid_air_ki = ki;
            g_sys.params.pid_air_kd = kd;
            g_sys.pid_calibrated = 1;
            System_RequestSave();
            PTC_SetPower(0);
            Fan_SetSpeed(0);
            temp_pid_progress = 100;     /* 值已刷新后才显示 100 */
            tp_stage = 2;
            tp_hold_ms = now;
        }

        if (tp_stage == 2 && (now - tp_hold_ms) > 1200U) {
            temp_pid_done = 1;
            temp_pid_running = 0;
        }

        /* 提交/驻留阶段(tp_stage>=1)保持断电，仅等待进度驻留结束 */
        if (tp_stage >= 1U) {
            PTC_SetPower(0);
        } else if (temp_pid_phase == 0) {
            /* 加热相位：元件按 ptc_max_temp 受控（烘干流程式），空气持续升温 */
            PTC_SetPower(temp_pid_ntc_power());
            if (temp >= temp_pid_target + 2.0f) {   /* 空气升到目标+2℃ -> 断电降温（振荡中心=目标） */
                PTC_SetPower(0);
                temp_pid_phase = 1;
            }
        } else if (temp_pid_phase == 1) {
            PTC_SetPower(0);
            /* 降温阶段风扇保持 100%：风机一停，元件余热把空气继续顶温、
             * 自然冷却又极慢（>摆幅死区），峰迟迟不形成 → 进度 50 后超时消失、参数不落 */
            Fan_SetSpeed(100);
            if (temp <= temp_pid_target - 2.0f) {   /* 降温到目标-2℃ -> 再升温（4℃摆幅，中心对齐目标） */
                temp_pid_phase = 0;
            }
        }

        /* 峰/谷检测：幅值=峰谷半幅（恒为正），周期=相邻峰时间
         * 滞回死区 0.3℃ + 极值锁存；状态文件级 tp_*，Start 清零 */
        {
            const float T_HYST = 0.3f;

            if (tp_last_temp < -999.0f) {
                tp_last_temp = temp;
            } else if (tp_rising) {
                if (temp > tp_last_temp) {
                    tp_last_temp = temp;
                } else if (temp < tp_last_temp - T_HYST) {
                    if (tp_have_trough) {
                        if (tp_last_peak_time != 0U)
                            temp_pid_period = (float)(now - tp_last_peak_time) / 1000.0f;
                        tp_last_peak_time = now;
                        temp_pid_amplitude = (tp_last_temp - tp_last_trough) * 0.5f;
                        if (temp_pid_amplitude < 0.0f) temp_pid_amplitude = -temp_pid_amplitude;
                        temp_pid_peak_count++;
                        tuner_accum(&tp_meas_cnt, &tp_sum_period, &tp_sum_amp,
                                    temp_pid_period, temp_pid_amplitude);
                        tp_have_trough = 0;
                    }
                    tp_rising = 0;
                    tp_last_temp = temp;
                }
            } else {
                if (temp < tp_last_temp) {
                    tp_last_temp = temp;
                } else if (temp > tp_last_temp + T_HYST) {
                    tp_last_trough = tp_last_temp;
                    tp_have_trough = 1;
                    tp_rising = 1;
                    tp_last_temp = temp;
                }
            }
        }

        /* 空气摆幅设计 4℃，AHT20 热惯性会磨平尖峰 → 用 tp_meas_cnt 累计的均值。
         * 完成由阶段机（tp_stage）负责；此处仅保留超时兜底。 */
        if (tp_stage == 0 && (now - temp_pid_start_ms) > 1800000U) {   /* 总超时30min：空气环升温+5轮摆幅本来就慢 */
            if (tp_meas_cnt >= 2U) {
                float kp, ki, kd;
                tuner_conservative(tp_sum_period, tp_sum_amp, tp_meas_cnt, 0.0f, 0.0f, &kp, &ki, &kd);
                g_sys.params.pid_air_kp = kp;
                g_sys.params.pid_air_ki = ki;
                g_sys.params.pid_air_kd = kd;
                g_sys.pid_calibrated = 1;
                System_RequestSave();
            }
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

