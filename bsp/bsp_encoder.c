#include "bsp_encoder.h"
#include "bsp_buzzer.h"
#include "bsp_cs1237.h"
#include "bsp_ptc.h"
#include "bsp_stepper.h"
#include "bsp_rgb_led.h"
#include "system_config.h"
#include "pin_config.h"
#include "system_time.h"

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
static uint8_t enc_last_ab = 0;
static int8_t enc_accum = 0;
static volatile uint32_t btn_down_time = 0;
static volatile uint8_t btn_down = 0;
static volatile uint8_t btn_long_flag = 0;

/* 旋转加速步进：综合"事件间隔(轮询快时按时间递增)"与"单次轮询相位累计量
 * (轮询被阻塞时编码器转过若干格→累计量大→大步进)"，上限10。与重绘/阻塞无关。 */
static uint8_t enc_accel_step = 1;
static uint32_t enc_last_rot_time = 0;

static void enc_update_accel(int8_t mag)
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
    enc_last_ab = (uint8_t)((GPIO_ReadInputDataBit(PIN_ENC_A_PORT, PIN_ENC_A_PIN) << 1)
                  | GPIO_ReadInputDataBit(PIN_ENC_B_PORT, PIN_ENC_B_PIN));
}

EncoderEvent_t Encoder_GetEvent(void)
{
    static const int8_t table[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    uint8_t ab = (uint8_t)((GPIO_ReadInputDataBit(PIN_ENC_A_PORT, PIN_ENC_A_PIN) << 1)
                 | GPIO_ReadInputDataBit(PIN_ENC_B_PORT, PIN_ENC_B_PIN));
    enc_accum += table[(enc_last_ab << 2) | ab];
    enc_last_ab = ab;
    /* EC11 涓€鏍?= 4 涓浉浣嶇姸鎬併€備负璁?杞竴鏍煎氨鍝嶅簲涓€娆?鏇寸伒鏁忥紝
     * 绱 卤2 鍗宠Е鍙戯紙绾﹀崐鏍硷級锛屽揩閫熸棆杞篃涓嶆紡銆?*/
    if (enc_accum >= 2) {
        int8_t mag = (int8_t)(enc_accum / 2);
        if (mag > 10) mag = 10;
        enc_accum = 0; enc_update_accel(mag); return ENC_EVT_CW;
    }
    if (enc_accum <= -2) {
        int8_t mag = (int8_t)((-enc_accum) / 2);
        if (mag > 10) mag = 10;
        enc_accum = 0; enc_update_accel(mag); return ENC_EVT_CCW;
    }
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
    switch (param_index) {
    case 0: /* 鐑樺共鑱斿姩 */
        p->motor_enabled = up ? 1 : 0;
        break;
    case 1: /* 杞姩鏂瑰悜 */
        p->motor_direction = up ? 1 : 0;
        break;
    case 2: /* 杞姩閫熷害 1-50 */
        if (up) { if (p->motor_speed < 50) p->motor_speed++; }
        else    { if (p->motor_speed > 1) p->motor_speed--; }
        break;
    case 3: /* 杈圭儤杈规墦 */
        p->motor_oscillate = up ? 1 : 0;
        break;
    case 4: /* 鎽嗗姩瑙掑害 1-30 (灏侀《30) */
        if (up) { if (p->motor_oscillate_angle < 30) p->motor_oscillate_angle++; }
        else    { if (p->motor_oscillate_angle > 1) p->motor_oscillate_angle--; }
        break;
    case 5: /* 椹卞姩閫夋嫨 */
        p->motor_driver = up ? MOTOR_DRIVER_TMC2209 : MOTOR_DRIVER_A4988;
        break;
    case 6: /* 椹卞姩鐢垫祦 0.2-0.6 姝ヨ繘0.1 */
        if (up) { if (p->motor_current < 6) p->motor_current++; }
        else    { if (p->motor_current > 2) p->motor_current--; }
        break;
    case 7: /* 闈欓煶 */
        p->motor_stealthchop = up ? 1 : 0;
        break;
    default: break;
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
    System_SaveParams();
}

void Encoder_Process(void)
{
    static Screen_t last_proc_screen = (Screen_t)0xFF;
    EncoderEvent_t evt = Encoder_GetEvent();
    if (evt == ENC_EVT_NONE) return;
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
            case SCREEN_TIME_EDIT:
            case SCREEN_SAFETY_ALERT:
                break;  /* 这些页面有各自的长按处理 */
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
            if (g_sys.selected_item == 0) g_sys.current_screen = SCREEN_TEMP_ADJUST;
            else if (g_sys.selected_item == 2) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_WEIGHT; }
            else if (g_sys.selected_item == 3) g_sys.current_screen = SCREEN_PTC_ADJUST;
            else if (g_sys.selected_item == 4) g_sys.current_screen = SCREEN_TIME_ADJUST;
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
            if (g_sys.selected_item == 0) { CS1237_Tare(); g_sys.weight_g = 0.0f; }
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
            } else if (evt == ENC_EVT_CLICK) { g_sys.temp_edit_active = 0; }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 3;
            else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 2 : g_sys.selected_item - 1;
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 0) { g_sys.temp_edit_active = 1; }
                else if (g_sys.selected_item == 1) {
                    g_sys.temp_pid_running = 1; g_sys.temp_pid_progress = 0;
                    PTC_TempPID_AutotuneStart((float)g_sys.params.target_temp);
                    g_sys.current_screen = SCREEN_TEMP_PID;
                }
                else { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN; }
            }
        }
        break;

    case SCREEN_TEMP_EDIT:
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            int8_t step = (enc_accel_step > 5) ? 5 : (int8_t)enc_accel_step;
            int16_t v = (int16_t)g_sys.params.target_temp + ((evt == ENC_EVT_CW) ? step : -step);
            if (v < TEMP_MIN) v = TEMP_MIN; else if (v > TEMP_MAX) v = TEMP_MAX;
            g_sys.params.target_temp = (uint16_t)v;
        } else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { System_SaveParams(); g_sys.current_screen = SCREEN_TEMP_ADJUST; }
            else g_sys.current_screen = SCREEN_TEMP_ADJUST;
        }
        break;

    case SCREEN_TEMP_PID:
        if (evt == ENC_EVT_CLICK) g_sys.current_screen = SCREEN_TEMP_ADJUST;
        break;

    case SCREEN_TIME_ADJUST:
        if (evt == ENC_EVT_CW) {
            g_sys.time_cursor = (g_sys.time_cursor + 1) % TIME_DIGIT_COUNT;
        } else if (evt == ENC_EVT_CCW) {
            g_sys.time_cursor = (g_sys.time_cursor == 0) ? (TIME_DIGIT_COUNT - 1) : (g_sys.time_cursor - 1);
        } else if (evt == ENC_EVT_CLICK) {  /* 单击：进入当前位编辑 */
            time_init_digits();
            g_sys.current_screen = SCREEN_TIME_EDIT;
        } else if (evt == ENC_EVT_LONG_PRESS) {  /* 长按：退回主界面，不保存 */
            g_sys.selected_item = 0;
            g_sys.current_screen = SCREEN_MAIN;
        }
        break;

    case SCREEN_TIME_EDIT: {
        static const uint8_t max_d[6] = {4, 7, 5, 9, 5, 9};
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            int8_t delta = (evt == ENC_EVT_CW) ? 1 : -1;
            uint8_t cur = g_sys.time_cursor;
            int16_t new_val = (int16_t)g_sys.time_digits[cur] + delta;
            if (new_val < 0) new_val = max_d[cur];
            else if (new_val > (int16_t)max_d[cur]) new_val = 0;
            g_sys.time_digits[cur] = (uint8_t)new_val;
        } else if (evt == ENC_EVT_CLICK) {  /* 单击：保存并退出当前参数，回调整页 */
            time_commit();
            g_sys.current_screen = SCREEN_TIME_ADJUST;
        } else if (evt == ENC_EVT_LONG_PRESS) {  /* 长按：退回主界面，修改不保存 */
            g_sys.selected_item = 0;
            g_sys.current_screen = SCREEN_MAIN;
        }
        break;
    }

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
            } else if (evt == ENC_EVT_CLICK) { g_sys.ptc_edit_active = 0; }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 4;
            else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 3 : g_sys.selected_item - 1;
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 0) { g_sys.ptc_edit_active = 1; }
                else if (g_sys.selected_item == 1) { g_sys.ptc_edit_active = 1; }
                else if (g_sys.selected_item == 2) {
                    g_sys.pid_autotune_running = 1; g_sys.pid_autotune_progress = 0;
                    PTC_PID_AutotuneStart();
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
            if (g_sys.selected_item == 0) { System_SaveParams(); g_sys.current_screen = SCREEN_PTC_ADJUST; }
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
            if (g_sys.selected_item == 0) { System_SaveParams(); g_sys.current_screen = SCREEN_PTC_ADJUST; }
            else g_sys.current_screen = SCREEN_PTC_ADJUST;
        }
        break;

    case SCREEN_PID_AUTOTUNE:
        if (evt == ENC_EVT_CLICK) g_sys.current_screen = SCREEN_PTC_ADJUST;
        break;

    case SCREEN_MENU:
        if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 6;
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 5 : g_sys.selected_item - 1;
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            uint8_t pp = 5;
            if (g_sys.selected_item >= g_sys.scroll_offset + pp) g_sys.scroll_offset = g_sys.selected_item - pp + 1;
            if (g_sys.selected_item < g_sys.scroll_offset) g_sys.scroll_offset = g_sys.selected_item;
        } else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) g_sys.current_screen = SCREEN_WIFI;
            else if (g_sys.selected_item == 1) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MOTOR_ADJUST; }
            else if (g_sys.selected_item == 2) { g_sys.device_id = System_GetDeviceId(); g_sys.current_screen = SCREEN_ABOUT; }
            else if (g_sys.selected_item == 3) { g_sys.current_screen = SCREEN_SETTINGS; }
            else if (g_sys.selected_item == 4) NVIC_SystemReset();
            else { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN; }
        }
        break;

    case SCREEN_MOTOR_ADJUST:
    {
        uint8_t count = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209) ? 9 : 7;
        if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % count;
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? (count - 1) : g_sys.selected_item - 1;
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            uint8_t pp = 5;
            if (g_sys.selected_item >= g_sys.scroll_offset + pp) g_sys.scroll_offset = g_sys.selected_item - pp + 1;
            if (g_sys.selected_item < g_sys.scroll_offset) g_sys.scroll_offset = g_sys.selected_item;
        } else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == count - 1) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MENU; }
            else { g_sys.submenu_active = g_sys.selected_item; g_sys.current_screen = SCREEN_MOTOR_EDIT; }
        }
        break;
    }

    case SCREEN_MOTOR_EDIT:
        if (evt == ENC_EVT_CW) motor_edit_set(g_sys.submenu_active, 1);
        else if (evt == ENC_EVT_CCW) motor_edit_set(g_sys.submenu_active, 0);
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { System_SaveParams(); g_sys.current_screen = SCREEN_MOTOR_ADJUST; }
            else g_sys.current_screen = SCREEN_MOTOR_ADJUST;
        }
        break;

    case SCREEN_ABOUT:
        if (evt == ENC_EVT_CLICK) g_sys.current_screen = SCREEN_MENU;
        break;

    case SCREEN_WIFI:
        if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % 2;
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? 1 : 0;
        else if (evt == ENC_EVT_CLICK) { if (g_sys.selected_item == 0) g_sys.current_screen = SCREEN_MENU; else g_sys.wifi_enabled = !g_sys.wifi_enabled; }
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

    case SCREEN_SETTINGS: {
        uint8_t cnt = 7;
        static uint8_t s_edit = 0;
        if (s_edit) {
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                int8_t delta = (evt == ENC_EVT_CW) ? 1 : -1;
                if (g_sys.selected_item == 1) {
                    /* 蜂鸣器音量不加速，步进 1 */
                    int16_t v = (int16_t)g_sys.buzzer_vol + delta;
                    if (v < 0) v = 0; else if (v > 10) v = 10;
                    g_sys.buzzer_vol = (uint8_t)v;
                } else if (g_sys.selected_item == 3) {
                    /* 屏幕亮度加速，上限 10 */
                    int8_t step = (int8_t)enc_accel_step;
                    int16_t v = (int16_t)g_sys.backlight + delta * step;
                    if (v < 0) v = 0; else if (v > 100) v = 100;
                    g_sys.backlight = (uint8_t)v;
                    TFT_SetBrightness(g_sys.backlight);
                } else if (g_sys.selected_item == 5) {
                    /* 熄屏超时 0=从不 1=1s 2=5s 3=10s 4=20s 5=30s 6=60s 7=120s 8=300s */
                    int16_t v = (int16_t)g_sys.screen_off_timeout + delta;
                    if (v < 0) v = 0; else if (v > 8) v = 8;
                    g_sys.screen_off_timeout = (uint8_t)v;
                }
            } else if (evt == ENC_EVT_CLICK) { s_edit = 0; }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = (g_sys.selected_item + 1) % cnt;
            else if (evt == ENC_EVT_CCW) g_sys.selected_item = (g_sys.selected_item == 0) ? (cnt - 1) : (g_sys.selected_item - 1);
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                uint8_t pp = 5;
                if (g_sys.selected_item >= g_sys.scroll_offset + pp) g_sys.scroll_offset = g_sys.selected_item - pp + 1;
                if (g_sys.selected_item < g_sys.scroll_offset) g_sys.scroll_offset = g_sys.selected_item;
            } else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item >= 6) g_sys.current_screen = SCREEN_MENU;
                else if (g_sys.selected_item == 1 || g_sys.selected_item == 3 || g_sys.selected_item == 5) { s_edit = 1; }
                else {
                    switch (g_sys.selected_item) {
                    case 0: g_sys.buzzer_link = !g_sys.buzzer_link; break;
                    case 2:
                        g_sys.light_switch = !g_sys.light_switch;
                        if (!g_sys.light_switch) {
                            /* 关灯：立即发 24bit 全零到两个灯条 */
                            RGB_Status_Off();
                            uint8_t black[3] = {0, 0, 0};
                            RGB_Strip3_SetPixels(black, 1);
                        }
                        break;
                    case 4: g_sys.theme = !g_sys.theme; theme_apply(); UI_DrawSettingsScreen(); break;
                    default: break;
                    }
                }
            }
        }
        break;
    }

default:
        break;
}
}
#endif /* BOOTLOADER_BUILD */

