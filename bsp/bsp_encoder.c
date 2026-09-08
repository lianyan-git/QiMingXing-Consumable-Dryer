#include "bsp_encoder.h"
#include "bsp_buzzer.h"
#include "bsp_cs1237.h"
#include "bsp_ptc.h"
#include "bsp_stepper.h"
#include "bsp_rgb_led.h"
#include "system_config.h"
#include "pin_config.h"
#include "system_time.h"
#include "ui_manager.h"

#include "stm32f10x.h"

#ifndef BOOTLOADER_BUILD

extern void theme_apply(void);
extern void UI_DrawSettingsScreen(void);
extern void TFT_SetBrightness(uint8_t pct);

/* Weak stubs for bootloader build - overridden by strong definitions in APP build */
SystemState_t g_sys __attribute__((weak));
void Buzzer_Beep(uint16_t ms) __attribute__((weak));
void CS1237_Tare(void) __attribute__((weak));
uint32_t System_GetDeviceId(void) __attribute__((weak));
void System_SaveParams(void) __attribute__((weak));
void StartDrying(void) __attribute__((weak));
void StopDrying(void) __attribute__((weak));

void Buzzer_Beep(uint16_t ms) { (void)ms; }
void CS1237_Tare(void) { }
uint32_t System_GetDeviceId(void) { return 0; }
void System_SaveParams(void) { }
void StartDrying(void) { }
void StopDrying(void) { }

volatile uint32_t g_last_input_ms = 0;  /* 最后输入时间(旋转/按钮), 供 main 熄屏计时 */
static volatile int16_t enc_accum = 0;  /* ISR 累加：1kHz 采样相位增量，主循环阻塞也不丢 */
static volatile uint32_t btn_down_time = 0;
static volatile uint8_t btn_down = 0;
static volatile uint8_t btn_long_flag = 0;

/* EC11 相位表：由上一状态与本状态联合查表得 ±1（有效沿），其余为 0 */
static const int8_t enc_phase_table[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

/* 旋转加速步进：综合"事件间隔(轮询快时按时间递增)"与"单次轮询相位累计量
 * (轮询被阻塞时编码器转过若干格→累计量大→大步进)"，上限10。与重绘/阻塞无关。 */
static uint8_t enc_accel_step = 1;
static uint32_t enc_last_rot_time = 0;

static void enc_update_accel(int16_t mag)
{
    uint32_t now = SystemTime_Millis();
    g_last_input_ms = now;
    uint32_t dt = now - enc_last_rot_time;
    enc_last_rot_time = now;

    uint8_t step = enc_accel_step;
    if (dt < 100)        { if (step < 10) step++; }   /* 轮询快 + 快转：按时间递增 */
    else if (dt > 300)   { step = 1; }                 /* 停顿：复位 */
    /* 100..300ms：保持 */

    if (mag < 1) mag = 1;
    if (step < (uint8_t)mag) step = (uint8_t)mag;      /* 轮询被阻塞时按累计量取大 */
    if (step > 10) step = 10;
    enc_accel_step = step;
}

void Encoder_Init(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = PIN_ENC_A_PIN | PIN_ENC_B_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(PIN_ENC_A_PORT, &g);
    g.GPIO_Pin = PIN_ENC_BTN_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(PIN_ENC_BTN_PORT, &g);
    enc_accum = 0;
    btn_down_time = 0; btn_down = 0; btn_long_flag = 0;
}

/* 1kHz SysTick 采样：每次记录相位沿增量到 enc_accum。
 * 只要 SysTick 不被长时间屏蔽，无论主循环多慢都不会丢失旋转计数。 */
void Encoder_TickISR(void)
{
    static uint8_t last_ab = 0xFF;
    uint8_t ab = (uint8_t)((GPIO_ReadInputDataBit(PIN_ENC_A_PORT, PIN_ENC_A_PIN) << 1)
                  | GPIO_ReadInputDataBit(PIN_ENC_B_PORT, PIN_ENC_B_PIN));
    if (last_ab != 0xFF) {
        enc_accum += enc_phase_table[(last_ab << 2) | ab];
        if (enc_accum > 60) enc_accum = 60;
        else if (enc_accum < -60) enc_accum = -60;
    }
    last_ab = ab;
}

EncoderEvent_t Encoder_GetEvent(void)
{
    int16_t mag;

    /* 旋转：每满 2 个相位计数出 1 个事件，且只扣 2（余量留在累加器里）。
     * 注意：不能再"每次调用清零 enc_accum"——主循环轮询远快于相位速度时，
     * 单个计数(=1)会被立刻抹掉，永远攒不到阈值 2 → 旋转几乎无事件而按键正常。 */
    __disable_irq();
    int16_t acc = enc_accum;

    if (acc >= 2) {
        mag = acc / 2;
        if (mag > 10) mag = 10;
        enc_accum = acc - 2;
        __enable_irq();
        enc_update_accel(mag); return ENC_EVT_CW;
    }
    if (acc <= -2) {
        mag = (-acc) / 2;
        if (mag > 10) mag = 10;
        enc_accum = acc + 2;
        __enable_irq();
        enc_update_accel(mag); return ENC_EVT_CCW;
    }
    __enable_irq();

    if (!btn_down && GPIO_ReadInputDataBit(PIN_ENC_BTN_PORT, PIN_ENC_BTN_PIN) == 0) {
        btn_down = 1; btn_down_time = SystemTime_Millis(); g_last_input_ms = btn_down_time;
    }
    if (btn_long_flag) { btn_long_flag = 0; return ENC_EVT_LONG_PRESS; }
    if (btn_down && GPIO_ReadInputDataBit(PIN_ENC_BTN_PORT, PIN_ENC_BTN_PIN) == 1) {
        uint32_t dur = SystemTime_Millis() - btn_down_time;
        btn_down = 0;
        if (dur > 1000) return ENC_EVT_LONG_PRESS;
        if (dur > 50) return ENC_EVT_CLICK;
    }
    if (btn_down && (SystemTime_Millis() - btn_down_time > 1000) && !btn_long_flag) {
        btn_long_flag = 1;
    }
    return ENC_EVT_NONE;
}

static void motor_edit_set(int param_index, int up)
{
    Params_t *p = &g_sys.params;
    int slot = param_index;
    uint8_t is_tmc = (p->motor_driver == MOTOR_DRIVER_TMC2208 ||
                      p->motor_driver == MOTOR_DRIVER_TMC2209);
    if (!is_tmc && slot >= 6) slot += 1;   /* A4988 无电流项：索引6→次数,7→休息 */
    switch (slot) {
    case 0: /* 鐑樺共鑱斿姩 */
        p->motor_enabled = up ? 1 : 0;
        break;
    case 1: /* 杞姩鏂瑰悜 */
        p->motor_direction = up ? 1 : 0;
        break;
    case 2: /* 杞姩閫熷害 1-50 */
        if (up) { int16_t v = (int16_t)p->motor_speed + (int16_t)enc_accel_step; if (v > 50) v = 50; p->motor_speed = (uint8_t)v; }
        else    { int16_t v = (int16_t)p->motor_speed - (int16_t)enc_accel_step; if (v < 1) v = 1;  p->motor_speed = (uint8_t)v; }
        break;
    case 3: /* 杈圭儤杈规墦 */
        p->motor_oscillate = up ? 1 : 0;
        break;
    case 4: /* 鎽嗗姩瑙掑害 1-30 (灏侀《30) */
        if (up) { int16_t v = (int16_t)p->motor_oscillate_angle + (int16_t)enc_accel_step; if (v > 360) v = 360; p->motor_oscillate_angle = (uint16_t)v; }
        else    { int16_t v = (int16_t)p->motor_oscillate_angle - (int16_t)enc_accel_step; if (v < 1) v = 1;  p->motor_oscillate_angle = (uint16_t)v; }
        break;
    case 5: /* 椹卞姩閫夋嫨: A4988->TMC2208->TMC2209 */
        {
            uint8_t d = p->motor_driver;
            d = (uint8_t)(up ? ((d + 1) % 3) : ((d + 2) % 3));
            p->motor_driver = d;
        }
        break;
    case 6: /* 椹卞姩鐢垫祦 0.2-0.6 姝ヨ繘0.1 */
        if (up) { if (p->motor_current < 6) p->motor_current++; }
        else    { if (p->motor_current > 2) p->motor_current--; }
        break;
    case 7: /* 宸ヤ綔娆℃暟 0-1000 (0=涓€鐩村伐浣?) */
        {
            int8_t step = (int8_t)enc_accel_step;
            int32_t v = (int32_t)p->motor_work_count + (up ? step : -step);
            if (v < 0) v = 0; else if (v > 1000) v = 1000;
            p->motor_work_count = (uint16_t)v;
        }
        break;
    case 8: /* 休息时间 0-600s (0=不休息) */
        {
            int8_t step = (int8_t)enc_accel_step;
            int32_t v = (int32_t)p->motor_rest_sec + (up ? step : -step);
            if (v < 0) v = 0; else if (v > 600) v = 600;
            p->motor_rest_sec = (uint16_t)v;
        }
        break;
    case 9:
        p->motor_stealthchop = up ? 1 : 0;
        /* 触发滑块滑动动画：1-6 关→开，7-12 开→关 */
        g_sys.mute_anim = (uint8_t)(up ? 1 : 7);
        break;
    default: break;
    }
}

/* 设置页参数编辑（同电机页交互：单击进入/旋转修改/单击退出+保存，实时刷新由UI_Update完成） */
static void settings_edit_set(int idx, int up)
{
    switch (idx) {
    case 0: /* 蜂鸣器联动 */
        g_sys.buzzer_link = up ? 1 : 0;
        break;
    case 1: /* 蜂鸣器音量 0-10 单步1 */
        {
            int16_t v = (int16_t)g_sys.buzzer_vol + (up ? 1 : -1);
            if (v < 0) v = 0; else if (v > 10) v = 10;
            g_sys.buzzer_vol = (uint8_t)v;
        }
        break;
    case 2: /* 灯光开关 */
        {
            uint8_t nv = up ? 1 : 0;
            if (nv != g_sys.light_switch) {
                g_sys.light_switch = nv;
                if (!nv) { RGB_AllOff(); }
            }
        }
        break;
    case 3: /* 背光 0-100 带加速 */
        {
            int8_t step = (int8_t)enc_accel_step;
            int16_t v = (int16_t)g_sys.backlight + (up ? step : -step);
            if (v < 0) v = 0; else if (v > 100) v = 100;
            g_sys.backlight = (uint8_t)v;
            TFT_SetBrightness(g_sys.backlight);
        }
        break;
    case 4: /* 主题 */
        {
            uint8_t nv = up ? 1 : 0;
            if (nv != g_sys.theme) { g_sys.theme = nv; theme_apply(); UI_DrawSettingsScreen(); }
        }
        break;
    case 5: /* 熄屏超时 0-8 */
        {
            int16_t v = (int16_t)g_sys.screen_off_timeout + (up ? 1 : -1);
            if (v < 0) v = 0; else if (v > 8) v = 8;
            g_sys.screen_off_timeout = (uint8_t)v;
        }
        break;
    default:
        break;
    }
}

/* 从 dry_time_sec 初始化六位数字 */
static void time_init_digits(void)
{
    uint32_t h = g_sys.params.dry_time_sec / 3600;
    uint32_t m = (g_sys.params.dry_time_sec % 3600) / 60;
    uint32_t s = g_sys.params.dry_time_sec % 60;
    g_sys.time_digits[0] = (uint8_t)(h / 10);
    g_sys.time_digits[1] = (uint8_t)(h % 10);
    g_sys.time_digits[2] = (uint8_t)(m / 10);
    g_sys.time_digits[3] = (uint8_t)(m % 10);
    g_sys.time_digits[4] = (uint8_t)(s / 10);
    g_sys.time_digits[5] = (uint8_t)(s % 10);
}

/* 提交六位数字到 dry_time_sec 并保存 */
static void time_commit(void)
{
    uint32_t h = g_sys.time_digits[0] * 10 + g_sys.time_digits[1];
    uint32_t m = g_sys.time_digits[2] * 10 + g_sys.time_digits[3];
    uint32_t s = g_sys.time_digits[4] * 10 + g_sys.time_digits[5];
    g_sys.params.dry_time_sec = h * 3600 + m * 60 + s;
    System_RequestSave();
}

void Encoder_Process(void)
{
    static Screen_t last_proc_screen = (Screen_t)0xFF;
    EncoderEvent_t evt = Encoder_GetEvent();
    if (evt == ENC_EVT_NONE) return;
    /* 息屏状态：编码器输入仅用于唤醒屏幕（开背光），不产生菜单/光标动作 */
    if (g_sys.screen_off) {
        g_sys.screen_off = 0;
        TFT_SetBrightness(g_sys.backlight);
        return;
    }
    /* 蜂鸣器联动：长按时只响一声（用 btn_down 边沿去重） */
    if (g_sys.buzzer_link) {
        static uint8_t last_beep_btn = 0;
        if (evt == ENC_EVT_LONG_PRESS) {
            if (btn_down != last_beep_btn) {
                last_beep_btn = btn_down;
                Buzzer_Beep(20);
            }
        } else {
            if (btn_down == 0) last_beep_btn = 0;
            Buzzer_Beep(20);
        }
    }

    if (g_sys.current_screen != last_proc_screen) {
        g_sys.prev_screen = last_proc_screen;
        last_proc_screen = g_sys.current_screen;
    }

    /* 全局长按：除特殊页面外，长按一律退回主界面 */
if (evt == ENC_EVT_LONG_PRESS) {
        switch (g_sys.current_screen) {
            case SCREEN_MAIN:
            case SCREEN_MENU:
            case SCREEN_TIME_ADJUST:
            case SCREEN_SAFETY_ALERT:
            case SCREEN_PRESET:
            case SCREEN_PRESET_LIST:
            case SCREEN_PRESET_EDIT:
                break;  /* 这些页面有各自的长按处理 */
            case SCREEN_MOTOR_ADJUST:
                g_sys.motor_edit_active = 0;
                System_RequestSave();          /* 长按退出前保存电机参数 */
                g_sys.current_screen = SCREEN_MAIN;
                g_sys.selected_item = 0;
                return;
            case SCREEN_SETTINGS:
                g_sys.settings_edit_active = 0;
                g_sys.rgb_bright_popup = 0;
                g_sys.rgb_bright_edit = 0;
                System_RequestSave();          /* 长按退出前保存设置 */
                g_sys.current_screen = SCREEN_MAIN;
                g_sys.selected_item = 0;
                return;
            case SCREEN_ABOUT:
                g_sys.current_screen = SCREEN_MENU;
                g_sys.selected_item = 0;
                return;
            default:
                g_sys.current_screen = SCREEN_MAIN;
                g_sys.selected_item = 0;
                return;
        }
    }

    switch (g_sys.current_screen) {

case SCREEN_MAIN:
        if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 5;
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 4 : g_sys.selected_item - 1;
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_TEMP_ADJUST; }
            else if (g_sys.selected_item == 1) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_PRESET; }
            else if (g_sys.selected_item == 2) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_WEIGHT; }
            else if (g_sys.selected_item == 3) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_PTC_ADJUST; }
            else if (g_sys.selected_item == 4) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_TIME_ADJUST; }
        }
        else if (evt == ENC_EVT_LONG_PRESS && g_sys.selected_item == 1) {
            /* 湿度卡：长按切换开始/停止（仅一次/每次按压，btn_down 复位时自动重置） */
            static uint8_t hum_last = 0;
            if (btn_down == 0) hum_last = 0;
            if (btn_down != hum_last) {
                hum_last = btn_down;
                if (g_sys.drying_active) StopDrying();
                else StartDrying();
            }
        }
        break;

    case SCREEN_WEIGHT:
        if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 2;
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 1 : 0;
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { CS1237_Tare(); g_sys.weight_g = 0; }  /* 强制重校准：当前AD即为新零点 */
            else { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN; }
        }
        break;

    case SCREEN_TEMP_ADJUST:
        if (g_sys.temp_edit_active) {
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                int8_t step = (enc_accel_step > 5) ? 5 : (int8_t)enc_accel_step;
                int16_t v = (int16_t)g_sys.params.target_temp + ((evt == ENC_EVT_CW) ? step : -step);
                if (v < TEMP_MIN) v = TEMP_MIN; else if (v > TEMP_MAX) v = TEMP_MAX;
                g_sys.params.target_temp = (uint16_t)v;
                /* 同步到当前预设温度 */
                if (g_sys.params.current_preset < g_sys.params.preset_count)
                    g_sys.params.presets[g_sys.params.current_preset].temp = (uint8_t)v;
            } else if (evt == ENC_EVT_CLICK) { g_sys.temp_edit_active = 0; System_RequestSave(); }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 4;
            else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 3 : g_sys.selected_item - 1;
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 0) { g_sys.temp_edit_active = 1; }
                else if (g_sys.selected_item == 1) {
                    /* PID调整：未校准先填三个相同默认值 */
                    if (!g_sys.pid_calibrated) {
                        g_sys.params.pid_air_kp = PID_MANUAL_DEFAULT;
                        g_sys.params.pid_air_ki = PID_MANUAL_DEFAULT;
                        g_sys.params.pid_air_kd = PID_MANUAL_DEFAULT;
                        g_sys.pid_calibrated = 1;
                    }
                    g_sys.pid_edit_active = 0;
                    g_sys.pid_return_screen = 0;
                    g_sys.current_screen = SCREEN_PID_ADJUST;
                }
                else if (g_sys.selected_item == 2) {
                    /* 进入自动校准页（单击后再启动，防误触） */
                    g_sys.current_screen = SCREEN_TEMP_PID;
                }
                else { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN; }
            }
        }
        break;

    case SCREEN_TEMP_PID:
        /* 防误触：未启动→单击开始校准(后台)；校准中→单击返回上级；长按回主界面 */
        if (evt == ENC_EVT_CLICK) {
            if (!g_sys.temp_pid_running) {
                g_sys.temp_pid_running = 1;
                g_sys.temp_pid_progress = 0;
                PTC_TempPID_AutotuneStart((float)g_sys.params.target_temp);
            } else {
                g_sys.current_screen = SCREEN_TEMP_ADJUST;
            }
        } else if (evt == ENC_EVT_LONG_PRESS) {
            g_sys.selected_item = 0;
            g_sys.current_screen = SCREEN_MAIN;
        }
        break;

    case SCREEN_TIME_ADJUST:
        if (g_sys.time_edit_active) {   /* 编辑态：旋转改当前位数字，单击退出编辑 */
            static const uint8_t max_d[6] = {4, 7, 5, 9, 5, 9};
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                int8_t delta = (evt == ENC_EVT_CW) ? 1 : -1;
                int16_t new_val = (int16_t)g_sys.time_digits[g_sys.time_cursor] + delta;
                if (new_val < 0) new_val = max_d[g_sys.time_cursor];
                else if (new_val > (int16_t)max_d[g_sys.time_cursor]) new_val = 0;
                g_sys.time_digits[g_sys.time_cursor] = (uint8_t)new_val;
            } else if (evt == ENC_EVT_CLICK) {
                g_sys.time_edit_active = 0;   /* 再次单击：退出编辑 */
            } else if (evt == ENC_EVT_LONG_PRESS) {  /* 长按：提交并退回主界面 */
                time_commit();
                g_sys.time_edit_active = 0;
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_MAIN;
            }
            break;
        }
        if (evt == ENC_EVT_CW) {
              g_sys.time_cursor = (TimeField_t)((g_sys.time_cursor + 1) % TIME_DIGIT_COUNT);
        } else if (evt == ENC_EVT_CCW) {
              g_sys.time_cursor = (TimeField_t)((g_sys.time_cursor == 0) ? (TIME_DIGIT_COUNT - 1) : (g_sys.time_cursor - 1));
        } else if (evt == ENC_EVT_CLICK) {  /* 单击：进入当前位编辑 */
            time_init_digits();
            g_sys.time_edit_active = 1;
        } else if (evt == ENC_EVT_LONG_PRESS) {  /* 长按：提交并退回主界面 */
            time_commit();
            g_sys.selected_item = 0;
            g_sys.current_screen = SCREEN_MAIN;
        }
        break;

    case SCREEN_PTC_ADJUST:
        if (g_sys.ptc_edit_active) {
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                int8_t step = (enc_accel_step > 5) ? 5 : (int8_t)enc_accel_step;
                int8_t delta = (evt == ENC_EVT_CW) ? step : -step;
                if (g_sys.selected_item == 0) {
                    int16_t v = (int16_t)g_sys.params.ptc_max_temp + delta;
                    if (v < PTC_TEMP_MIN) v = PTC_TEMP_MIN; else if (v > PTC_TEMP_MAX) v = PTC_TEMP_MAX;
                    g_sys.params.ptc_max_temp = (uint16_t)v;
                } else if (g_sys.selected_item == 1) {
                    int16_t v = (int16_t)g_sys.params.ptc_cooling_temp + delta;
                    if (v < 30) v = 30; else if (v > (int16_t)g_sys.params.ptc_max_temp) v = (int16_t)g_sys.params.ptc_max_temp;
                    g_sys.params.ptc_cooling_temp = (uint16_t)v;
                }
            } else if (evt == ENC_EVT_CLICK) { g_sys.ptc_edit_active = 0; System_RequestSave(); }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 5;
            else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 4 : g_sys.selected_item - 1;
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 0) { g_sys.ptc_edit_active = 1; }
                else if (g_sys.selected_item == 1) { g_sys.ptc_edit_active = 1; }
                else if (g_sys.selected_item == 2) {
                    /* PID调整：未校准先填三个相同默认值 */
                    if (!g_sys.pid_calibrated) {
                        g_sys.params.pid_air_kp = PID_MANUAL_DEFAULT;
                        g_sys.params.pid_air_ki = PID_MANUAL_DEFAULT;
                        g_sys.params.pid_air_kd = PID_MANUAL_DEFAULT;
                        g_sys.pid_calibrated = 1;
                    }
                    g_sys.pid_edit_active = 0;
                    g_sys.pid_return_screen = 1;
                    g_sys.current_screen = SCREEN_PID_ADJUST;
                }
                else if (g_sys.selected_item == 3) {
                    /* 进入自动校准页（单击后再启动，防误触） */
                    g_sys.current_screen = SCREEN_PID_AUTOTUNE;
                }
                else { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN; }
            }
        }
        break;

    case SCREEN_PTC_EDIT:
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            int8_t step = (enc_accel_step > 5) ? 5 : (int8_t)enc_accel_step;
            int16_t v = (int16_t)g_sys.params.ptc_max_temp + ((evt == ENC_EVT_CW) ? step : -step);
            if (v < PTC_TEMP_MIN) v = PTC_TEMP_MIN; else if (v > PTC_TEMP_MAX) v = PTC_TEMP_MAX;
            g_sys.params.ptc_max_temp = (uint16_t)v;
        } else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { System_RequestSave(); g_sys.current_screen = SCREEN_PTC_ADJUST; }
            else g_sys.current_screen = SCREEN_PTC_ADJUST;
        }
        break;

    case SCREEN_PTC_COOLING_EDIT:
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            int8_t step = (enc_accel_step > 5) ? 5 : (int8_t)enc_accel_step;
            int16_t v = (int16_t)g_sys.params.ptc_cooling_temp + ((evt == ENC_EVT_CW) ? step : -step);
            if (v < 30) v = 30; else if (v > (int16_t)g_sys.params.ptc_max_temp) v = (int16_t)g_sys.params.ptc_max_temp;
            g_sys.params.ptc_cooling_temp = (uint16_t)v;
        } else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { System_RequestSave(); g_sys.current_screen = SCREEN_PTC_ADJUST; }
            else g_sys.current_screen = SCREEN_PTC_ADJUST;
        }
        break;

    case SCREEN_PID_AUTOTUNE:
        /* 防误触：未启动→单击开始校准(后台)；校准中→单击返回上级；长按回主界面 */
        if (evt == ENC_EVT_CLICK) {
            if (!g_sys.pid_autotune_running) {
                g_sys.pid_autotune_running = 1;
                g_sys.pid_autotune_progress = 0;
                PTC_PID_AutotuneStart();
            } else {
                g_sys.current_screen = SCREEN_PTC_ADJUST;
            }
        } else if (evt == ENC_EVT_LONG_PRESS) {
            g_sys.selected_item = 0;
            g_sys.current_screen = SCREEN_MAIN;
        }
        break;

    case SCREEN_PID_ADJUST:
        if (g_sys.pid_edit_active) {
            /* 编辑中：旋转以 0.1×加速 修改当前 PID 值，实时刷新 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                int8_t step = (int8_t)enc_accel_step;
                float delta = (evt == ENC_EVT_CW) ? 0.1f * (float)step : -0.1f * (float)step;
                float *v;
                if (g_sys.pid_return_screen) {   /* PTC 页进入 → 编辑元件 PID */
                    v = (g_sys.pid_edit_active == 1) ? &g_sys.params.pid_ntc_kp :
                        (g_sys.pid_edit_active == 2) ? &g_sys.params.pid_ntc_ki : &g_sys.params.pid_ntc_kd;
                } else {                          /* 温度页进入 → 编辑空气 PID */
                    v = (g_sys.pid_edit_active == 1) ? &g_sys.params.pid_air_kp :
                        (g_sys.pid_edit_active == 2) ? &g_sys.params.pid_air_ki : &g_sys.params.pid_air_kd;
                }
                *v += delta;
                if (*v < 0.0f) *v = 0.0f;
                else if (*v > 1000.0f) *v = 1000.0f;   /* 与 params_valid 上限一致，防止把自整定结果钳回100 */
            } else if (evt == ENC_EVT_CLICK) {
                g_sys.pid_edit_active = 0;   /* 单击退出当前项 → 后台保存 */
                System_RequestSave();
            }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 4;
            else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 3 : g_sys.selected_item - 1;
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 3) {   /* 返回 */
                    g_sys.selected_item = 0;
                    g_sys.current_screen = g_sys.pid_return_screen ? SCREEN_PTC_ADJUST : SCREEN_TEMP_ADJUST;
                } else {
                    g_sys.pid_edit_active = (uint8_t)(g_sys.selected_item + 1);
                }
            }
        }
        break;

    case SCREEN_PRESET: {   /* 主菜单：编辑预设 / 新增预设 / 删除预设 / 退出 */
        if (evt == ENC_EVT_CW) g_sys.selected_item = (uint8_t)((g_sys.selected_item + 1) % 4);
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 3 : (uint8_t)(g_sys.selected_item - 1);
        else if (evt == ENC_EVT_CLICK) {
            switch (g_sys.selected_item) {
            case 0:   /* 编辑预设：二级列表（编辑模式） */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0; g_sys.pixel_offset = 0; g_sys.preset_confirm = 0;
                g_sys.current_screen = SCREEN_PRESET_LIST;
                break;
            case 1: { /* 新增预设 */
                Preset_t *np;
                if (g_sys.params.preset_count >= PRESET_MAX) break;   /* 已满：不响应 */
                np = &g_sys.params.presets[g_sys.params.preset_count];
                np->name[0] = 'A'; np->name[1] = 0;
                np->temp = 60; np->time_sec = 12U * 3600U;
                g_sys.preset_edit_new = 1;
                g_sys.preset_edit_idx = g_sys.params.preset_count;
                g_sys.preset_scratch = *np;
                g_sys.preset_row = 0; g_sys.preset_row_edit = 0; g_sys.preset_name_cur = 0;
                g_sys.preset_time_cur = 0; g_sys.preset_time_edit = 0;
                g_sys.current_screen = SCREEN_PRESET_EDIT;
                break; }
            case 2:   /* 删除预设：二级列表（删除模式） */
                g_sys.preset_del_mode = 1;
                g_sys.selected_item = 0; g_sys.pixel_offset = 0; g_sys.preset_confirm = 0;
                g_sys.current_screen = SCREEN_PRESET_LIST;
                break;
            default:  /* 退出 */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_MAIN;
                break;
            }
        }
        break;
    }

    case SCREEN_PRESET_LIST: {   /* 预设二级列表：normal=单击进编辑/长按切换；del=单击删除确认 */
        uint8_t cnt = (uint8_t)(g_sys.params.preset_count + 1);   /* 预设行 + 退出 */
        if (g_sys.preset_confirm == 1) {   /* 删除确认弹窗 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) g_sys.preset_confirm_yes = !g_sys.preset_confirm_yes;
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.preset_confirm_yes && g_sys.preset_edit_idx < g_sys.params.preset_count) {
                    uint8_t i, idx = g_sys.preset_edit_idx;
                    if (g_sys.params.preset_count > 1) {
                        for (i = idx; i + 1 < g_sys.params.preset_count; i++) g_sys.params.presets[i] = g_sys.params.presets[i + 1];
                        g_sys.params.preset_count--;
                        if (g_sys.params.current_preset == idx) g_sys.params.current_preset = 0;
                        else if (g_sys.params.current_preset > idx) g_sys.params.current_preset--;
                        g_sys.params.target_temp = g_sys.params.presets[g_sys.params.current_preset].temp;
                        g_sys.params.dry_time_sec = g_sys.params.presets[g_sys.params.current_preset].time_sec;
                        System_RequestSave();
                        if (g_sys.selected_item >= g_sys.params.preset_count) {
                            g_sys.selected_item = (g_sys.params.preset_count > 0) ? (uint8_t)(g_sys.params.preset_count - 1) : 0;
                        }
                    }
                }
                g_sys.preset_confirm = 0;
                UI_DrawPreset();
            }
            break;
        }
        if (evt == ENC_EVT_CW) g_sys.selected_item = (uint8_t)((g_sys.selected_item + 1) % cnt);
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? (uint8_t)(cnt - 1) : (uint8_t)(g_sys.selected_item - 1);
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            /* 像素滚动保持选中项可见（超过一页 5 行出滚动条） */
            int16_t target = (int16_t)g_sys.selected_item * 20;
            int16_t max_off = (int16_t)(cnt * 20 - 100);
            if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
            else if (target + 20 > g_sys.pixel_offset + 100) g_sys.pixel_offset = (int16_t)(target + 20 - 100);
            if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
            if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
        }
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item >= g_sys.params.preset_count) {   /* 退出 → 上一级主菜单 */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_PRESET;
            } else if (g_sys.preset_del_mode) {   /* 删除模式：单击 → 确认 */
                g_sys.preset_edit_idx = g_sys.selected_item;
                g_sys.preset_confirm = 1;
                g_sys.preset_confirm_yes = 0;
            } else {   /* 进入编辑；名称锁定 → 光标落在温度行 */
                g_sys.preset_edit_new = 0;
                g_sys.preset_edit_idx = g_sys.selected_item;
                g_sys.preset_scratch = g_sys.params.presets[g_sys.selected_item];
                g_sys.preset_row = 1; g_sys.preset_row_edit = 0; g_sys.preset_name_cur = 0;
                g_sys.preset_time_cur = 0; g_sys.preset_time_edit = 0;
                g_sys.current_screen = SCREEN_PRESET_EDIT;
            }
        } else if (evt == ENC_EVT_LONG_PRESS) {
            if (!g_sys.preset_del_mode && g_sys.selected_item < g_sys.params.preset_count) {
                /* 长按预设 = 切换为当前预设（应用其温度/时间并保存） */
                g_sys.params.current_preset = g_sys.selected_item;
                g_sys.params.target_temp = g_sys.params.presets[g_sys.selected_item].temp;
                g_sys.params.dry_time_sec = g_sys.params.presets[g_sys.selected_item].time_sec;
                System_RequestSave();
            } else {   /* 删除模式长按 或 光标在退出行：返回主菜单 */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_PRESET;
            }
        }
        break;
    }

    case SCREEN_PRESET_EDIT: {
        Preset_t *p = &g_sys.params.presets[g_sys.preset_edit_idx];
        if (g_sys.preset_confirm == 2) {   /* 退出时是否保存确认 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) g_sys.preset_confirm_yes = !g_sys.preset_confirm_yes;
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.preset_confirm_yes) {   /* 是：保存并追加/更新 */
                    if (g_sys.preset_edit_new) g_sys.params.preset_count++;
                    g_sys.params.current_preset = g_sys.preset_edit_idx;
                    g_sys.params.target_temp = p->temp;
                    g_sys.params.dry_time_sec = p->time_sec;
                    System_RequestSave();
                } else {   /* 否：还原备份 */
                    g_sys.params.presets[g_sys.preset_edit_idx] = g_sys.preset_scratch;
                }
                g_sys.preset_edit_new = 0;
                g_sys.preset_confirm = 0;
                /* 保存/放弃后回到预设列表（光标停在刚处理的行） */
                g_sys.selected_item = (g_sys.preset_edit_idx < g_sys.params.preset_count)
                                    ? g_sys.preset_edit_idx : 0;
                g_sys.current_screen = SCREEN_PRESET_LIST;
            }
            break;
        }
        if (g_sys.preset_row_edit == 0) {
            /* 名称是唯一身份，仅新建可编辑；已保存预设跳过名称行（光标 0=名称 1=温度 2=时间 3=退出） */
            uint8_t name_locked = !g_sys.preset_edit_new;
            if (evt == ENC_EVT_CW) {
                g_sys.preset_row = (uint8_t)((g_sys.preset_row + 1) % 4);
                if (name_locked && g_sys.preset_row == 0) g_sys.preset_row = 1;
            } else if (evt == ENC_EVT_CCW) {
                g_sys.preset_row = (g_sys.preset_row == 0) ? 3 : (uint8_t)(g_sys.preset_row - 1);
                if (name_locked && g_sys.preset_row == 0) g_sys.preset_row = 3;
            }
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.preset_row == 3) {   /* 退出：有改动→询问是否保存；无改动→直接返回 */
                    if (g_sys.preset_edit_new ||
                        memcmp(p, &g_sys.preset_scratch, sizeof(Preset_t)) != 0) {
                        g_sys.preset_confirm = 2;
                        g_sys.preset_confirm_yes = 0;
                    } else {
                        g_sys.selected_item = (g_sys.preset_edit_idx < g_sys.params.preset_count)
                                            ? g_sys.preset_edit_idx : 0;
                        g_sys.current_screen = SCREEN_PRESET_LIST;
                    }
                } else {
                    /* 名称唯一身份：仅新建模式可编辑，其余不可进入 */
                    if (g_sys.preset_row == 0 && !g_sys.preset_edit_new) break;
                    g_sys.preset_row_edit = (uint8_t)(g_sys.preset_row + 1);
                    g_sys.preset_name_cur = 0;
                    g_sys.preset_time_cur = 0;
                    g_sys.preset_time_edit = 0;
                    if (g_sys.preset_row == 2) {   /* 进入时间弹窗：打开一次即初始化六位数字 */
                        uint32_t t = p->time_sec;
                        uint32_t hh = t / 3600U, mm = (t % 3600U) / 60U, ss = t % 60U;
                        g_sys.time_digits[0] = (uint8_t)(hh / 10U);
                        g_sys.time_digits[1] = (uint8_t)(hh % 10U);
                        g_sys.time_digits[2] = (uint8_t)(mm / 10U);
                        g_sys.time_digits[3] = (uint8_t)(mm % 10U);
                        g_sys.time_digits[4] = (uint8_t)(ss / 10U);
                        g_sys.time_digits[5] = (uint8_t)(ss % 10U);
                    }
                }
            }
        } else {
            if (g_sys.preset_row_edit == 1) {   /* 名称：A-Z 空格，click 前进 */
                if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                    char c = p->name[g_sys.preset_name_cur];
                    if (c == 0) c = 'A';
                    if (evt == ENC_EVT_CW) { c = (c == ' ') ? 'A' : (c == 'Z') ? ' ' : (char)(c + 1); }
                    else { c = (c == ' ') ? 'Z' : (c == 'A') ? ' ' : (char)(c - 1); }
                    p->name[g_sys.preset_name_cur] = c;
                    p->name[PRESET_NAME_MAX] = 0;
                } else if (evt == ENC_EVT_CLICK) {
                    g_sys.preset_name_cur++;
                    if (g_sys.preset_name_cur >= PRESET_NAME_MAX) { g_sys.preset_name_cur = 0; g_sys.preset_row_edit = 0; }
                    else if (p->name[g_sys.preset_name_cur] == 0) p->name[g_sys.preset_name_cur] = 'A';
                } else if (evt == ENC_EVT_LONG_PRESS) {   /* 长按：退出名称编辑回行选择 */
                    g_sys.preset_name_cur = 0;
                    g_sys.preset_row_edit = 0;
                }
            } else if (g_sys.preset_row_edit == 2) {  /* 温度 30-80 */
                if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                    int16_t v = (int16_t)p->temp + ((evt == ENC_EVT_CW) ? 1 : -1);
                    if (v < 30) v = 30; else if (v > 80) v = 80;
                    p->temp = (uint8_t)v;
                } else if (evt == ENC_EVT_CLICK) { g_sys.preset_row_edit = 0; }
                else if (evt == ENC_EVT_LONG_PRESS) { g_sys.preset_row_edit = 0; }   /* 长按：退出温度弹窗 */
            } else if (g_sys.preset_row_edit == 3) {  /* 时间弹窗：光标选位→单击编辑→旋转改数→再单击退出 */
                if (g_sys.preset_time_edit) {   /* 编辑态：旋转改当前位 */
                    if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                        int16_t v = (int16_t)g_sys.time_digits[g_sys.preset_time_cur] + ((evt == ENC_EVT_CW) ? 1 : -1);
                        if (v < 0) v = 9; else if (v > 9) v = 0;
                        g_sys.time_digits[g_sys.preset_time_cur] = (uint8_t)v;
                    } else if (evt == ENC_EVT_CLICK) {
                        g_sys.preset_time_edit = 0;   /* 再单击：退出编辑 */
                    } else if (evt == ENC_EVT_LONG_PRESS) {  /* 长按：提交并关闭弹窗 */
                        p->time_sec = (uint32_t)(((g_sys.time_digits[0]*10 + g_sys.time_digits[1])*60 +
                                          (g_sys.time_digits[2]*10 + g_sys.time_digits[3]))*60 +
                                          (g_sys.time_digits[4]*10 + g_sys.time_digits[5]));
                        g_sys.preset_time_cur = 0;
                        g_sys.preset_time_edit = 0;
                        g_sys.preset_row_edit = 0;
                    }
                } else {   /* 选位态：旋转移动光标，单击进入编辑 */
                    if (evt == ENC_EVT_CW) g_sys.preset_time_cur = (uint8_t)((g_sys.preset_time_cur + 1) % 6);
                    else if (evt == ENC_EVT_CCW) g_sys.preset_time_cur = (uint8_t)((g_sys.preset_time_cur == 0) ? 5 : g_sys.preset_time_cur - 1);
                    else if (evt == ENC_EVT_CLICK) {
                        g_sys.preset_time_edit = 1;
                    } else if (evt == ENC_EVT_LONG_PRESS) {  /* 长按：提交并关闭弹窗 */
                        p->time_sec = (uint32_t)(((g_sys.time_digits[0]*10 + g_sys.time_digits[1])*60 +
                                          (g_sys.time_digits[2]*10 + g_sys.time_digits[3]))*60 +
                                          (g_sys.time_digits[4]*10 + g_sys.time_digits[5]));
                        g_sys.preset_time_cur = 0;
                        g_sys.preset_time_edit = 0;
                        g_sys.preset_row_edit = 0;
                    }
                }
            }
        }
        break;
    }

    case SCREEN_MENU:
        if (evt == ENC_EVT_CW) UI_MenuScroll(1);
        else if (evt == ENC_EVT_CCW) UI_MenuScroll(-1);
        if (evt == ENC_EVT_CLICK) {
            /* 进入子页前保存菜单位置，返回时恢复 */
            g_sys.menu_selected = g_sys.selected_item;
            g_sys.menu_pixel_offset = g_sys.pixel_offset;
            if (g_sys.selected_item == 0) g_sys.current_screen = SCREEN_WIFI;
            else if (g_sys.selected_item == 1) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MOTOR_ADJUST; }
            else if (g_sys.selected_item == 2) { g_sys.device_id = System_GetDeviceId(); g_sys.current_screen = SCREEN_ABOUT; }
            else if (g_sys.selected_item == 3) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_SETTINGS; }
            else if (g_sys.selected_item == 4) { System_FlushSave(); NVIC_SystemReset(); }  /* 重启前先刷完保存 */
            else if (g_sys.selected_item == 5) {   /* 恢复出厂设置：清flash+默认值，不重启 */
                System_FactoryReset();
                theme_apply();
                TFT_SetBrightness(g_sys.backlight);
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_MAIN;
            }
            else { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN; }
        }
        break;

    case SCREEN_MOTOR_ADJUST:
    {
uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
        uint8_t count = is_tmc ? 11 : 9;
        if (g_sys.motor_edit_active) {
            /* 编辑中：旋转直接改值（实时刷新由 UI_Update 逐行重绘），
             * 单击退出当前项 → 后台保存 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                motor_edit_set(g_sys.selected_item, (evt == ENC_EVT_CW));
            } else if (evt == ENC_EVT_CLICK) {
                g_sys.motor_edit_active = 0;
                System_RequestSave();
            }
        } else {
            if (evt == ENC_EVT_CW) UI_MotorScroll(1);
            else if (evt == ENC_EVT_CCW) UI_MotorScroll(-1);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == count - 1) {
                    System_RequestSave();      /* 退出电机页前保存（含未单击退出的修改） */
                    g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MENU;
                }
                else { g_sys.motor_edit_active = 1; }   /* 单击选中 → 进入该项编辑 */
            }
        }
        break;
    }

    case SCREEN_ABOUT:
        if (evt == ENC_EVT_CLICK) g_sys.current_screen = SCREEN_MENU;
        break;

    case SCREEN_WIFI:
        if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 2;
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 1 : 0;
        else if (evt == ENC_EVT_CLICK) { if (g_sys.selected_item == 0) g_sys.current_screen = SCREEN_MENU; else { g_sys.wifi_enabled = !g_sys.wifi_enabled; System_RequestSave(); } }
        break;

    case SCREEN_OTA:
        if (evt == ENC_EVT_CLICK) g_sys.current_screen = SCREEN_MENU;
        break;

    case SCREEN_SAFETY_ALERT:
        if (evt == ENC_EVT_CLICK || evt == ENC_EVT_LONG_PRESS) {
            g_sys.safety_state = SAFETY_NONE;
            g_sys.run_state = STATE_IDLE;
            g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN;
        }
        break;

    case SCREEN_SETTINGS:
        if (g_sys.settings_edit_active) {
            /* 编辑中：旋转直接改值（实时刷新由 UI_Update 逐行重绘），单击退出→保存 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                if (g_sys.rgb_bright_popup) {   /* RGB 亮度弹窗 */
                    if (g_sys.rgb_bright_edit) {   /* 编辑中：旋转直接调整数值（带加速） */
                        if (g_sys.rgb_bright_sel <= 1) {
                            uint8_t *pv = g_sys.rgb_bright_sel ? &g_sys.params.rgb_strip_bright : &g_sys.params.rgb_led_bright;
                            int16_t v = (int16_t)*pv + ((evt == ENC_EVT_CW) ? (int16_t)enc_accel_step : -(int16_t)enc_accel_step);
                            if (v < 0) v = 0; else if (v > 100) v = 100;
                            *pv = (uint8_t)v;
                        }
                    } else {   /* 未选中参数：旋转移动光标（0指示灯 1灯条 2完成） */
                        if (evt == ENC_EVT_CW) g_sys.rgb_bright_sel = (g_sys.rgb_bright_sel + 1) % 3;
                        else g_sys.rgb_bright_sel = (g_sys.rgb_bright_sel == 0) ? 2 : (uint8_t)(g_sys.rgb_bright_sel - 1);
                    }
                } else {
                    settings_edit_set(g_sys.selected_item, (evt == ENC_EVT_CW));
                }
            } else if (evt == ENC_EVT_CLICK) {
                if (g_sys.rgb_bright_popup) {
                    if (g_sys.rgb_bright_edit) {   /* 编辑数值中：再次单击退出参数 */
                        g_sys.rgb_bright_edit = 0;
                    } else if (g_sys.rgb_bright_sel == 2) {   /* 完成：退出并保存 */
                        g_sys.settings_edit_active = 0;
                        g_sys.rgb_bright_popup = 0;
                        g_sys.rgb_bright_edit = 0;
                        System_RequestSave();
                    } else {   /* 选中参数：进入数值编辑 */
                        g_sys.rgb_bright_edit = 1;
                    }
                } else {
                    g_sys.settings_edit_active = 0;
                    System_RequestSave();
                }
            } else if (evt == ENC_EVT_LONG_PRESS) {   /* 长按退出弹窗并保存 */
                g_sys.settings_edit_active = 0;
                g_sys.rgb_bright_popup = 0;
                g_sys.rgb_bright_edit = 0;
                System_RequestSave();
            }
        } else {
            if (evt == ENC_EVT_CW) UI_SettingsScroll(1);
            else if (evt == ENC_EVT_CCW) UI_SettingsScroll(-1);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item >= 7) g_sys.current_screen = SCREEN_MENU;  /* 退出 */
                else {
                    g_sys.settings_edit_active = 1;
                    if (g_sys.selected_item == 6) { g_sys.rgb_bright_popup = 1; g_sys.rgb_bright_sel = 0; g_sys.rgb_bright_edit = 0; }
                }
            }
        }
        break;

default:
        break;
}
}
#endif /* BOOTLOADER_BUILD */

