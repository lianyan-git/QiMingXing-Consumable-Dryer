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
#include <string.h>

#ifndef BOOTLOADER_BUILD

extern void theme_apply(void);
extern void UI_DrawSettingsScreen(void);
extern void UI_CanScroll(int dir);
extern void UI_RefreshCard(uint8_t item);
extern void CAN_Cluster_RequestSearch(void);
extern void EspLink_OnToggle(uint8_t on);
extern void EspLink_OpenConfig(void);
extern void EspLink_StartConfig(void);
extern void EspLink_MusicOpenAp(void);
extern void EspLink_MusicCloseAp(void);
extern void TFT_SetBrightness(uint8_t pct);
extern uint16_t MusicPlay_TrackCount(void);
extern uint8_t MusicPlay_IsPlaying(void);
extern uint16_t MusicPlay_CurTrack(void);
extern void MusicPlay_Stop(void);
extern void MusicStore_Wipe(void);
extern int  MusicPlay_Play(uint16_t track);
extern void EspLink_NotifyPresetsChanged(void);

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

volatile uint32_t g_last_input_ms = 0;  /* 鏈鍚庤緭鍏ユ椂闂(鏃嬭浆/鎸夐挳), 渚 main 鐔勫睆璁℃椂 */
static volatile int16_t enc_accum = 0;
static volatile uint32_t enc_boot_ms = 0;  /* ISR 绱鍔狅細1kHz 閲囨牱鐩镐綅澧為噺锛屼富寰鐜闃诲炰篃涓嶄涪 */
static volatile uint32_t btn_down_time = 0;
static volatile uint8_t btn_down = 0;
static volatile uint8_t btn_long_flag = 0;

/* EC11 鐩镐綅琛锛氱敱涓婁竴鐘舵佷笌鏈鐘舵佽仈鍚堟煡琛ㄥ緱 卤1锛堟湁鏁堟部锛夛紝鍏朵綑涓 0 */
static const int8_t enc_phase_table[16] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

/* 鏃嬭浆鍔犻熷凡鍏ㄥ眬鍏抽棴锛堢敤鎴疯佹眰锛夛細姣忔湁鏁堟部鍥哄畾 1 姝ワ紝蹇閫熸棆杞鑷鐒跺揩锛
 * 鍋滄㈠悗鍏夋爣鍋滃湪褰撳墠浣嶇疆锛屼笉鍋氫换浣曞彔鍔/鍔犻熴 */
static uint8_t enc_accel_step = 1;   /* tuning step: follows speed, synced with g_enc_cursor_step */
volatile uint8_t g_enc_cursor_step = 1;        /* cursor step: 1 (slow) .. ENC_STEP_MAX (fast) */
#define ENC_STEP_MAX 6U
static uint32_t enc_last_rot_time = 0;


uint8_t EncWrap(uint8_t count, int8_t dir, uint8_t cur)
{
    uint8_t s = g_enc_cursor_step;
    uint8_t i;
    if (count == 0U) return cur;
    if (s < 1U) s = 1U;
    if (dir > 0) cur = (uint8_t)((cur + s) % count);
    else { for (i = 0U; i < s; i++) cur = (cur == 0U) ? (uint8_t)(count - 1U) : (uint8_t)(cur - 1U); }
    return cur;
}

static void enc_update_accel(int16_t mag)
{
    (void)mag;
    enc_last_rot_time = SystemTime_Millis();
    g_last_input_ms = enc_last_rot_time;
    /* tuning step is set alongside g_enc_cursor_step in Encoder_GetEvent (speed based) */
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

/* 1kHz SysTick 閲囨牱锛氭瘡娆¤板綍鐩镐綅娌垮為噺鍒 enc_accum銆
 * 鍙瑕 SysTick 涓嶈闀挎椂闂村睆钄斤紝鏃犺轰富寰鐜澶氭參閮戒笉浼氫涪澶辨棆杞璁℃暟銆 */
void Encoder_TickISR(void)
{
    static uint8_t last_ab = 0xFF;
    uint8_t ab = (uint8_t)((GPIO_ReadInputDataBit(PIN_ENC_A_PORT, PIN_ENC_A_PIN) << 1)
                  | GPIO_ReadInputDataBit(PIN_ENC_B_PORT, PIN_ENC_B_PIN));
    if (enc_boot_ms == 0) enc_boot_ms = SystemTime_Millis();
    if ((uint32_t)(SystemTime_Millis() - enc_boot_ms) < 600U) {
        /* 上电 600ms 内引脚/上拉未稳定，只同步相位不计数（消除上电幽灵步进） */
        last_ab = ab; enc_accum = 0;
        return;
    }
    if (last_ab != 0xFF) {
        enc_accum += enc_phase_table[(last_ab << 2) | ab];
        if (enc_accum > 60) enc_accum = 60;
        else if (enc_accum < -60) enc_accum = -60;
    }
    last_ab = ab;
}

EncoderEvent_t Encoder_GetEvent(void)
{
    (void)0;

    /* 鏃嬭浆锛氭瘡婊 2 涓鐩镐綅璁℃暟鍑 1 涓浜嬩欢锛屼笖鍙鎵 2锛堜綑閲忕暀鍦ㄧ疮鍔犲櫒閲岋級銆
     * 娉ㄦ剰锛氫笉鑳藉啀"姣忔¤皟鐢ㄦ竻闆 enc_accum"鈥斺斾富寰鐜杞璇㈣繙蹇浜庣浉浣嶉熷害鏃讹紝
     * 鍗曚釜璁℃暟(=1)浼氳绔嬪埢鎶规帀锛屾案杩滄敀涓嶅埌闃堝 2 鈫 鏃嬭浆鍑犱箮鏃犱簨浠惰屾寜閿姝ｅ父銆 */
    __disable_irq();
    int16_t acc = enc_accum;
    /* 只消耗整格（2 单位=1格），半格余数保留在累加器里：慢转绝不丢格；
       快转一次 poll 积攒多格=一步跳多格（加速）；停手即停（无积压队列）。 */
    int16_t whole = acc - (int16_t)(acc & 1);
    int16_t lim   = (int16_t)(2 * ENC_STEP_MAX);
    if (whole > lim)  whole = lim;
    else if (whole < -lim) whole = -lim;
    enc_accum = acc - whole;           /* 余数(0/±1)与超出上限的部分保留，下次继续消化 */
    __enable_irq();

    if (whole >= 2) {
        g_enc_cursor_step = (uint8_t)(whole >> 1);
        enc_accel_step = g_enc_cursor_step;
        enc_update_accel(1);
        return ENC_EVT_CW;
    }
    if (whole <= -2) {
        g_enc_cursor_step = (uint8_t)((-whole) >> 1);
        enc_accel_step = g_enc_cursor_step;
        enc_update_accel(1);
        return ENC_EVT_CCW;
    }
    g_enc_cursor_step = 1U;
    enc_accel_step = 1U;

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
    if (!is_tmc && slot >= 8) slot += 1;   /* A4988 无电流项: 跳过 slot 8 */
    switch (slot) {
    case 0: /* 联动 */
        p->motor_enabled = up ? 1 : 0;
        break;
    case 1: /* 方向 */
        p->motor_direction = up ? 1 : 0;
        break;
    case 2: /* 速度 1-50 */
        if (up) { int16_t v = (int16_t)p->motor_speed + (int16_t)enc_accel_step; if (v > 50) v = 50; p->motor_speed = (uint8_t)v; }
        else    { int16_t v = (int16_t)p->motor_speed - (int16_t)enc_accel_step; if (v < 1) v = 1;  p->motor_speed = (uint8_t)v; }
        break;
    case 3: /* 摆动 */
        p->motor_oscillate = up ? 1 : 0;
        break;
    case 4: /* 角度 1-360 */
        if (up) { int16_t v = (int16_t)p->motor_oscillate_angle + (int16_t)enc_accel_step; if (v > 360) v = 360; p->motor_oscillate_angle = (uint16_t)v; }
        else    { int16_t v = (int16_t)p->motor_oscillate_angle - (int16_t)enc_accel_step; if (v < 1) v = 1;  p->motor_oscillate_angle = (uint16_t)v; }
        break;
    case 5: /* 次数 0-1000 (0=一直工作) */
        {
            int8_t step = (int8_t)enc_accel_step;
            int32_t v = (int32_t)p->motor_work_count + (up ? step : -step);
            if (v < 0) v = 0; else if (v > 1000) v = 1000;
            p->motor_work_count = (uint16_t)v;
        }
        break;
    case 6: /* 休息时间 0-600s (0=不休息) */
        {
            int8_t step = (int8_t)enc_accel_step;
            int32_t v = (int32_t)p->motor_rest_sec + (up ? step : -step);
            if (v < 0) v = 0; else if (v > 600) v = 600;
            p->motor_rest_sec = (uint16_t)v;
        }
        break;
    case 7: /* 驱动: A4988->TMC2208->TMC2209 */
        {
            uint8_t d = p->motor_driver;
            d = (uint8_t)(up ? ((d + 1) % 3) : ((d + 2) % 3));
            p->motor_driver = d;
        }
        break;
    case 8: /* 电流 0.2-0.6 步进0.1 */
        if (up) { if (p->motor_current < 6) p->motor_current++; }
        else    { if (p->motor_current > 2) p->motor_current--; }
        break;
    case 9: /* 静音 */
        p->motor_stealthchop = up ? 1 : 0;
        Stepper_SetSilent(p->motor_stealthchop);  /* 立即写 TMC CHOPCONF bit30 */
        break;
    default: break;
    }
}

/* 璁剧疆椤靛弬鏁扮紪杈戯紙鍚岀數鏈洪〉浜や簰锛氬崟鍑昏繘鍏/鏃嬭浆淇鏀/鍗曞嚮閫鍑+淇濆瓨锛屽疄鏃跺埛鏂扮敱UI_Update瀹屾垚锛 */
static void settings_edit_set(int idx, int up)
{
    switch (idx) {
    case 0: /* 铚傞福鍣ㄨ仈鍔 */
        g_sys.buzzer_link = up ? 1 : 0;
        break;
    case 1: /* 铚傞福鍣ㄩ煶閲 0-10 鍗曟1 */
        {
            int16_t v = (int16_t)g_sys.buzzer_vol + (up ? 1 : -1);
            if (v < 0) v = 0; else if (v > 10) v = 10;
            g_sys.buzzer_vol = (uint8_t)v;
        }
        break;
    case 2: /* 鐏鍏夊紑鍏 */
        {
            uint8_t nv = up ? 1 : 0;
            if (nv != g_sys.light_switch) {
                g_sys.light_switch = nv;
                if (!nv) { RGB_AllOff(); }
            }
        }
        break;
    case 3: /* 鑳屽厜 0-100 甯﹀姞閫 */
        {
            int8_t step = (int8_t)enc_accel_step;
            int16_t v = (int16_t)g_sys.backlight + (up ? step : -step);
            if (v < 0) v = 0; else if (v > 100) v = 100;
            g_sys.backlight = (uint8_t)v;
            TFT_SetBrightness(g_sys.backlight);
        }
        break;
    case 4: /* 涓婚 */
        {
            uint8_t nv = up ? 1 : 0;
            if (nv != g_sys.theme) { g_sys.theme = nv; theme_apply(); UI_DrawSettingsScreen(); }
        }
        break;
    case 5: /* 鐔勫睆瓒呮椂 0-8 */
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

/* 浠 dry_time_sec 鍒濆嬪寲鍏浣嶆暟瀛 */
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

/* 鎻愪氦鍏浣嶆暟瀛楀埌 dry_time_sec 骞朵繚瀛 */
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
    /* 鎭灞忕姸鎬侊細缂栫爜鍣ㄨ緭鍏ヤ粎鐢ㄤ簬鍞ら啋灞忓箷锛堝紑鑳屽厜锛夛紝涓嶄骇鐢熻彍鍗/鍏夋爣鍔ㄤ綔 */
    if (g_sys.screen_off) {
        g_sys.screen_off = 0;
        g_last_input_ms = SystemTime_Millis();   /* 鍞ら啋鍗冲埛鏂拌緭鍏ユ椂闂达紝閲嶆柊璁℃椂 */
        TFT_SetBrightness(g_sys.backlight);
        return;
    }
    /* 铚傞福鍣ㄨ仈鍔锛氶暱鎸夋椂鍙鍝嶄竴澹帮紙鐢 btn_down 杈规部鍘婚噸锛 */
    if (g_sys.buzzer_link && !MusicPlay_IsPlaying()) { /* TIM3 shared with music */
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

    /* 鍏ㄥ眬闀挎寜锛氶櫎鐗规畩椤甸潰澶栵紝闀挎寜涓寰嬮鍥炰富鐣岄潰 */
if (evt == ENC_EVT_LONG_PRESS) {
        switch (g_sys.current_screen) {
            case SCREEN_MAIN:
            case SCREEN_MENU:
            case SCREEN_TIME_ADJUST:
            case SCREEN_SAFETY_ALERT:
            case SCREEN_PRESET:
            case SCREEN_PRESET_LIST:
            case SCREEN_PRESET_EDIT:
                break;  /* 杩欎簺椤甸潰鏈夊悇鑷鐨勯暱鎸夊勭悊 */
            case SCREEN_MOTOR_ADJUST:
                g_sys.motor_edit_active = 0;
                System_RequestSave();          /* 闀挎寜閫鍑哄墠淇濆瓨鐢垫満鍙傛暟 */
                g_sys.current_screen = SCREEN_MAIN;
                g_sys.selected_item = 0;
                return;
            case SCREEN_SETTINGS:
                g_sys.settings_edit_active = 0;
                g_sys.rgb_bright_popup = 0;
                g_sys.rgb_bright_edit = 0;
                System_RequestSave();          /* 闀挎寜閫鍑哄墠淇濆瓨璁剧疆 */
                g_sys.current_screen = SCREEN_MAIN;
                g_sys.selected_item = 0;
                return;
            case SCREEN_CAN:
                g_sys.can_edit_active = 0;
                g_sys.can_search_tick = 0;
                System_RequestSave();          /* 闀挎寜閫鍑哄墠淇濆瓨 CAN 鍙傛暟 */
                g_sys.current_screen = SCREEN_MAIN;
                g_sys.selected_item = 0;
                return;
            case SCREEN_WIFI:
                g_sys.wifi_edit_active = 0;
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
        if (evt == ENC_EVT_CW) {
            uint8_t old = g_sys.selected_item;
            g_sys.selected_item = EncWrap(5, 1, g_sys.selected_item);
            if (old != g_sys.selected_item) { UI_RefreshCard(old); UI_RefreshCard(g_sys.selected_item); }
        }

        else if (evt == ENC_EVT_CCW) {
            uint8_t old = g_sys.selected_item;
            g_sys.selected_item = EncWrap(5, (int8_t)-1, g_sys.selected_item);
            if (old != g_sys.selected_item) { UI_RefreshCard(old); UI_RefreshCard(g_sys.selected_item); }
        }
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_TEMP_ADJUST; }
            else if (g_sys.selected_item == 1) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_PRESET; }
            else if (g_sys.selected_item == 2) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_WEIGHT; }
            else if (g_sys.selected_item == 3) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_PTC_ADJUST; }
            else if (g_sys.selected_item == 4) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_TIME_ADJUST; }
        }
        else if (evt == ENC_EVT_LONG_PRESS && g_sys.selected_item == 1) {
            /* 婀垮害鍗★細闀挎寜鍒囨崲寮濮/鍋滄锛堜粎涓娆/姣忔℃寜鍘嬶紝btn_down 澶嶄綅鏃惰嚜鍔ㄩ噸缃锛 */
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
        if (evt == ENC_EVT_CW) g_sys.selected_item = EncWrap(2, 1, g_sys.selected_item);
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = EncWrap(2U, (int8_t)-1, g_sys.selected_item);
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) { CS1237_Tare(); g_sys.weight_g = 0; }  /* 寮哄埗閲嶆牎鍑嗭細褰撳墠AD鍗充负鏂伴浂鐐 */
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
                /* 鍚屾ュ埌褰撳墠棰勮炬俯搴 */
                if (g_sys.params.current_preset < g_sys.params.preset_count)
                    g_sys.params.presets[g_sys.params.current_preset].temp = (uint8_t)v;
            } else if (evt == ENC_EVT_CLICK) { g_sys.temp_edit_active = 0; System_RequestSave(); }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = EncWrap(4, 1, g_sys.selected_item);

            else if (evt == ENC_EVT_CCW) g_sys.selected_item = EncWrap(4, (int8_t)-1, g_sys.selected_item);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 0) { g_sys.temp_edit_active = 1; }
                else if (g_sys.selected_item == 1) {
                    /* PID璋冩暣锛氭湭鏍″噯鍏堝～涓変釜鐩稿悓榛樿ゅ */
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
                    /* 杩涘叆鑷鍔ㄦ牎鍑嗛〉锛堝崟鍑诲悗鍐嶅惎鍔锛岄槻璇瑙︼級 */
                    g_sys.current_screen = SCREEN_TEMP_PID;
                }
                else { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MAIN; }
            }
        }
        break;

    case SCREEN_TEMP_PID:
        /* 闃茶瑙︼細鏈鍚鍔ㄢ啋鍗曞嚮寮濮嬫牎鍑(鍚庡彴)锛涙牎鍑嗕腑鈫掑崟鍑昏繑鍥炰笂绾э紱闀挎寜鍥炰富鐣岄潰 */
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
        if (g_sys.time_edit_active) {   /* 缂栬緫鎬侊細鏃嬭浆鏀瑰綋鍓嶄綅鏁板瓧锛屽崟鍑婚鍑虹紪杈 */
            static const uint8_t max_d[6] = {4, 7, 5, 9, 5, 9};
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                int8_t delta = (evt == ENC_EVT_CW) ? 1 : -1;
                int16_t new_val = (int16_t)g_sys.time_digits[g_sys.time_cursor] + delta;
                if (new_val < 0) new_val = max_d[g_sys.time_cursor];
                else if (new_val > (int16_t)max_d[g_sys.time_cursor]) new_val = 0;
                g_sys.time_digits[g_sys.time_cursor] = (uint8_t)new_val;
            } else if (evt == ENC_EVT_CLICK) {
                g_sys.time_edit_active = 0;   /* 鍐嶆″崟鍑伙細閫鍑虹紪杈 */
            } else if (evt == ENC_EVT_LONG_PRESS) {  /* 闀挎寜锛氭彁浜ゅ苟閫鍥炰富鐣岄潰 */
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
        } else if (evt == ENC_EVT_CLICK) {  /* 鍗曞嚮锛氳繘鍏ュ綋鍓嶄綅缂栬緫 */
            time_init_digits();
            g_sys.time_edit_active = 1;
        } else if (evt == ENC_EVT_LONG_PRESS) {  /* 闀挎寜锛氭彁浜ゅ苟閫鍥炰富鐣岄潰 */
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
            if (evt == ENC_EVT_CW) g_sys.selected_item = EncWrap(5, 1, g_sys.selected_item);

            else if (evt == ENC_EVT_CCW) g_sys.selected_item = EncWrap(5, (int8_t)-1, g_sys.selected_item);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 0) { g_sys.ptc_edit_active = 1; }
                else if (g_sys.selected_item == 1) { g_sys.ptc_edit_active = 1; }
                else if (g_sys.selected_item == 2) {
                    /* PID璋冩暣锛氭湭鏍″噯鍏堝～涓変釜鐩稿悓榛樿ゅ */
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
                    /* 杩涘叆鑷鍔ㄦ牎鍑嗛〉锛堝崟鍑诲悗鍐嶅惎鍔锛岄槻璇瑙︼級 */
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
        /* 闃茶瑙︼細鏈鍚鍔ㄢ啋鍗曞嚮寮濮嬫牎鍑(鍚庡彴)锛涙牎鍑嗕腑鈫掑崟鍑昏繑鍥炰笂绾э紱闀挎寜鍥炰富鐣岄潰 */
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
            /* 缂栬緫涓锛氭棆杞浠 0.1脳鍔犻 淇鏀瑰綋鍓 PID 鍊硷紝瀹炴椂鍒锋柊 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                int8_t step = (int8_t)enc_accel_step;
                float delta = (evt == ENC_EVT_CW) ? 0.1f * (float)step : -0.1f * (float)step;
                float *v;
                if (g_sys.pid_return_screen) {   /* PTC 椤佃繘鍏 鈫 缂栬緫鍏冧欢 PID */
                    v = (g_sys.pid_edit_active == 1) ? &g_sys.params.pid_ntc_kp :
                        (g_sys.pid_edit_active == 2) ? &g_sys.params.pid_ntc_ki : &g_sys.params.pid_ntc_kd;
                } else {                          /* 娓╁害椤佃繘鍏 鈫 缂栬緫绌烘皵 PID */
                    v = (g_sys.pid_edit_active == 1) ? &g_sys.params.pid_air_kp :
                        (g_sys.pid_edit_active == 2) ? &g_sys.params.pid_air_ki : &g_sys.params.pid_air_kd;
                }
                *v += delta;
                if (*v < 0.0f) *v = 0.0f;
                else if (*v > 1000.0f) *v = 1000.0f;   /* 涓 params_valid 涓婇檺涓鑷达紝闃叉㈡妸鑷鏁村畾缁撴灉閽冲洖100 */
            } else if (evt == ENC_EVT_CLICK) {
                g_sys.pid_edit_active = 0;   /* 鍗曞嚮閫鍑哄綋鍓嶉」 鈫 鍚庡彴淇濆瓨 */
                System_RequestSave();
            }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = EncWrap(4, 1, g_sys.selected_item);

            else if (evt == ENC_EVT_CCW) g_sys.selected_item = EncWrap(4, (int8_t)-1, g_sys.selected_item);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 3) {   /* 杩斿洖 */
                    g_sys.selected_item = 0;
                    g_sys.current_screen = g_sys.pid_return_screen ? SCREEN_PTC_ADJUST : SCREEN_TEMP_ADJUST;
                } else {
                    g_sys.pid_edit_active = (uint8_t)(g_sys.selected_item + 1);
                }
            }
        }
        break;

    case SCREEN_PRESET: {   /* 涓昏彍鍗曪細缂栬緫棰勮 / 鏂板為勮 / 鍒犻櫎棰勮 / 閫鍑 */
        if (evt == ENC_EVT_CW) g_sys.selected_item = EncWrap(4U, 1, g_sys.selected_item);
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = EncWrap(4U, (int8_t)-1, g_sys.selected_item);
        else if (evt == ENC_EVT_CLICK) {
            switch (g_sys.selected_item) {
            case 0:   /* 缂栬緫棰勮撅細浜岀骇鍒楄〃锛堢紪杈戞ā寮忥級 */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0; g_sys.pixel_offset = 0; g_sys.preset_confirm = 0;
                g_sys.current_screen = SCREEN_PRESET_LIST;
                break;
            case 1: { /* 鏂板為勮 */
                Preset_t *np;
                if (g_sys.params.preset_count >= PRESET_MAX) break;   /* 宸叉弧锛氫笉鍝嶅簲 */
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
            case 2:   /* 鍒犻櫎棰勮撅細浜岀骇鍒楄〃锛堝垹闄ゆā寮忥級 */
                g_sys.preset_del_mode = 1;
                g_sys.selected_item = 0; g_sys.pixel_offset = 0; g_sys.preset_confirm = 0;
                g_sys.current_screen = SCREEN_PRESET_LIST;
                break;
            default:  /* 閫鍑 */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_MAIN;
                break;
            }
        }
        break;
    }

    case SCREEN_PRESET_LIST: {   /* 棰勮句簩绾у垪琛锛歯ormal=鍗曞嚮杩涚紪杈/闀挎寜鍒囨崲锛沝el=鍗曞嚮鍒犻櫎纭璁 */
        uint8_t cnt = (uint8_t)(g_sys.params.preset_count + 1);   /* 棰勮捐 + 閫鍑 */
        if (g_sys.preset_confirm == 1) {   /* 鍒犻櫎纭璁ゅ脊绐 */
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
                        EspLink_NotifyPresetsChanged();
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
        if (evt == ENC_EVT_CW) g_sys.selected_item = EncWrap(cnt, 1, g_sys.selected_item);
        else if (evt == ENC_EVT_CCW) g_sys.selected_item = EncWrap(cnt, (int8_t)-1, g_sys.selected_item);
        if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
            /* 鍍忕礌婊氬姩淇濇寔閫変腑椤瑰彲瑙侊紙瓒呰繃涓椤 5 琛屽嚭婊氬姩鏉★級 */
            int16_t target = (int16_t)g_sys.selected_item * 20;
            int16_t max_off = (int16_t)(cnt * 20 - 100);
            if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
            else if (target + 20 > g_sys.pixel_offset + 100) g_sys.pixel_offset = (int16_t)(target + 20 - 100);
            if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
            if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
        }
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item >= g_sys.params.preset_count) {   /* 閫鍑 鈫 涓婁竴绾т富鑿滃崟 */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_PRESET;
            } else if (g_sys.preset_del_mode) {   /* 鍒犻櫎妯″紡锛氬崟鍑 鈫 纭璁 */
                g_sys.preset_edit_idx = g_sys.selected_item;
                g_sys.preset_confirm = 1;
                g_sys.preset_confirm_yes = 0;
            } else {   /* 杩涘叆缂栬緫锛涘悕绉伴攣瀹 鈫 鍏夋爣钀藉湪娓╁害琛 */
                g_sys.preset_edit_new = 0;
                g_sys.preset_edit_idx = g_sys.selected_item;
                g_sys.preset_scratch = g_sys.params.presets[g_sys.selected_item];
                g_sys.preset_row = 1; g_sys.preset_row_edit = 0; g_sys.preset_name_cur = 0;
                g_sys.preset_time_cur = 0; g_sys.preset_time_edit = 0;
                g_sys.current_screen = SCREEN_PRESET_EDIT;
            }
        } else if (evt == ENC_EVT_LONG_PRESS) {
            if (!g_sys.preset_del_mode && g_sys.selected_item < g_sys.params.preset_count) {
                /* 闀挎寜棰勮 = 鍒囨崲涓哄綋鍓嶉勮撅紙搴旂敤鍏舵俯搴/鏃堕棿骞朵繚瀛橈級 */
                g_sys.params.current_preset = g_sys.selected_item;
                g_sys.params.target_temp = g_sys.params.presets[g_sys.selected_item].temp;
                g_sys.params.dry_time_sec = g_sys.params.presets[g_sys.selected_item].time_sec;
                System_RequestSave();
                EspLink_NotifyPresetsChanged();
            } else {   /* 鍒犻櫎妯″紡闀挎寜 鎴 鍏夋爣鍦ㄩ鍑鸿岋細杩斿洖涓昏彍鍗 */
                g_sys.preset_del_mode = 0;
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_PRESET;
            }
        }
        break;
    }

    case SCREEN_PRESET_EDIT: {
        Preset_t *p = &g_sys.params.presets[g_sys.preset_edit_idx];
        if (g_sys.preset_confirm == 2) {   /* 閫鍑烘椂鏄鍚︿繚瀛樼‘璁 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) g_sys.preset_confirm_yes = !g_sys.preset_confirm_yes;
            else if (evt == ENC_EVT_CLICK) {
                uint8_t was_new = g_sys.preset_edit_new;
                if (g_sys.preset_confirm_yes) {   /* 鏄锛氫繚瀛樺苟杩藉姞/鏇存柊 */
                    if (was_new) g_sys.params.preset_count++;   /* 浠呯‘璁や繚瀛樻墠杩藉姞鏂伴勮 */
                    g_sys.params.current_preset = g_sys.preset_edit_idx;
                    g_sys.params.target_temp = p->temp;
                    g_sys.params.dry_time_sec = p->time_sec;
                    System_RequestSave();
                    EspLink_NotifyPresetsChanged();   /* 网页预设表实时刷新 */
                } else {   /* 鍚︼細杩樺師澶囦唤锛堟柊澧炲垯鐩存帴涓㈠純锛屼笉鍗犻勮句綅锛 */
                    g_sys.params.presets[g_sys.preset_edit_idx] = g_sys.preset_scratch;
                }
                g_sys.preset_edit_new = 0;
                g_sys.preset_confirm = 0;
                /* 鏂板炲畬鎴愬悗鐩存帴閫鍥炵儤骞查勮句富鑿滃崟锛涚紪杈戝凡鏈夐勮鹃鍥炲垪琛ㄥ師浣 */
                if (was_new) {
                    g_sys.selected_item = 1;   /* 鍋滅暀鍦"鏂板為勮"涓 */
                    g_sys.current_screen = SCREEN_PRESET;
                } else {
                    g_sys.selected_item = (g_sys.preset_edit_idx < g_sys.params.preset_count)
                                        ? g_sys.preset_edit_idx : 0;
                    g_sys.current_screen = SCREEN_PRESET_LIST;
                }
            }
            break;
        }
        if (g_sys.preset_row_edit == 0) {
            /* 鍚嶇О鏄鍞涓韬浠斤紝浠呮柊寤哄彲缂栬緫锛涘凡淇濆瓨棰勮捐烦杩囧悕绉拌岋紙鍏夋爣 0=鍚嶇О 1=娓╁害 2=鏃堕棿 3=閫鍑猴級 */
            uint8_t name_locked = !g_sys.preset_edit_new;
            if (evt == ENC_EVT_CW) {
                g_sys.preset_row = (uint8_t)((g_sys.preset_row + 1) % 4);
                if (name_locked && g_sys.preset_row == 0) g_sys.preset_row = 1;
            } else if (evt == ENC_EVT_CCW) {
                g_sys.preset_row = (g_sys.preset_row == 0) ? 3 : (uint8_t)(g_sys.preset_row - 1);
                if (name_locked && g_sys.preset_row == 0) g_sys.preset_row = 3;
            }
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.preset_row == 3) {   /* 閫鍑猴細鏈夋敼鍔ㄢ啋璇㈤棶鏄鍚︿繚瀛橈紱鏃犳敼鍔ㄢ啋鐩存帴杩斿洖 */
                    if (g_sys.preset_edit_new ||
                        memcmp(p, &g_sys.preset_scratch, sizeof(Preset_t)) != 0) {
                        g_sys.preset_confirm = 2;
                        g_sys.preset_confirm_yes = 0;
                    } else {
                        /* 鏃犳敼鍔ㄧ洿鎺ラ鍑猴細鏂板為勮句笖鏈淇濆瓨 鈫 涓㈠純骞跺洖涓昏彍鍗 */
                        uint8_t was_new = g_sys.preset_edit_new;
                        g_sys.preset_edit_new = 0;
                        g_sys.selected_item = (g_sys.preset_edit_idx < g_sys.params.preset_count)
                                            ? g_sys.preset_edit_idx : 0;
                        g_sys.current_screen = was_new ? SCREEN_PRESET : SCREEN_PRESET_LIST;
                    }
                } else {
                    /* 鍚嶇О鍞涓韬浠斤細浠呮柊寤烘ā寮忓彲缂栬緫锛屽叾浣欎笉鍙杩涘叆 */
                    if (g_sys.preset_row == 0 && !g_sys.preset_edit_new) break;
                    g_sys.preset_row_edit = (uint8_t)(g_sys.preset_row + 1);
                    g_sys.preset_name_cur = 0;
                    g_sys.preset_time_cur = 0;
                    g_sys.preset_time_edit = 0;
                    if (g_sys.preset_row == 2) {   /* 杩涘叆鏃堕棿寮圭獥锛氭墦寮涓娆″嵆鍒濆嬪寲鍏浣嶆暟瀛 */
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
            if (g_sys.preset_row_edit == 1) {   /* 鍚嶇О锛欰-Z 绌烘牸锛宑lick 鍓嶈繘 */
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
                } else if (evt == ENC_EVT_LONG_PRESS) {   /* 闀挎寜锛氶鍑哄悕绉扮紪杈戝洖琛岄夋嫨 */
                    g_sys.preset_name_cur = 0;
                    g_sys.preset_row_edit = 0;
                }
            } else if (g_sys.preset_row_edit == 2) {  /* 娓╁害 30-80 */
                if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                    int16_t v = (int16_t)p->temp + ((evt == ENC_EVT_CW) ? 1 : -1);
                    if (v < 30) v = 30; else if (v > 80) v = 80;
                    p->temp = (uint8_t)v;
                } else if (evt == ENC_EVT_CLICK) { g_sys.preset_row_edit = 0; }
                else if (evt == ENC_EVT_LONG_PRESS) { g_sys.preset_row_edit = 0; }   /* 闀挎寜锛氶鍑烘俯搴﹀脊绐 */
            } else if (g_sys.preset_row_edit == 3) {  /* 鏃堕棿寮圭獥锛氬厜鏍囬変綅鈫掑崟鍑荤紪杈戔啋鏃嬭浆鏀规暟鈫掑啀鍗曞嚮閫鍑 */
                if (g_sys.preset_time_edit) {   /* 缂栬緫鎬侊細鏃嬭浆鏀瑰綋鍓嶄綅 */
                    if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                        int16_t v = (int16_t)g_sys.time_digits[g_sys.preset_time_cur] + ((evt == ENC_EVT_CW) ? 1 : -1);
                        if (v < 0) v = 9; else if (v > 9) v = 0;
                        g_sys.time_digits[g_sys.preset_time_cur] = (uint8_t)v;
                    } else if (evt == ENC_EVT_CLICK) {
                        g_sys.preset_time_edit = 0;   /* 鍐嶅崟鍑伙細閫鍑虹紪杈 */
                    } else if (evt == ENC_EVT_LONG_PRESS) {  /* 闀挎寜锛氭彁浜ゅ苟鍏抽棴寮圭獥 */
                        p->time_sec = (uint32_t)(((g_sys.time_digits[0]*10 + g_sys.time_digits[1])*60 +
                                          (g_sys.time_digits[2]*10 + g_sys.time_digits[3]))*60 +
                                          (g_sys.time_digits[4]*10 + g_sys.time_digits[5]));
                        g_sys.preset_time_cur = 0;
                        g_sys.preset_time_edit = 0;
                        g_sys.preset_row_edit = 0;
                    }
                } else {   /* 閫変綅鎬侊細鏃嬭浆绉诲姩鍏夋爣锛屽崟鍑昏繘鍏ョ紪杈 */
                    if (evt == ENC_EVT_CW) g_sys.preset_time_cur = (uint8_t)((g_sys.preset_time_cur + 1) % 6);
                    else if (evt == ENC_EVT_CCW) g_sys.preset_time_cur = (uint8_t)((g_sys.preset_time_cur == 0) ? 5 : g_sys.preset_time_cur - 1);
                    else if (evt == ENC_EVT_CLICK) {
                        g_sys.preset_time_edit = 1;
                    } else if (evt == ENC_EVT_LONG_PRESS) {  /* 闀挎寜锛氭彁浜ゅ苟鍏抽棴寮圭獥 */
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
            /* 杩涘叆瀛愰〉鍓嶄繚瀛樿彍鍗曚綅缃锛岃繑鍥炴椂鎭㈠ */
            g_sys.menu_selected = g_sys.selected_item;
            g_sys.menu_pixel_offset = g_sys.pixel_offset;
            if (g_sys.selected_item == 0) g_sys.current_screen = SCREEN_WIFI;
            else if (g_sys.selected_item == 1) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MOTOR_ADJUST; }
            else if (g_sys.selected_item == 2) { g_sys.device_id = System_GetDeviceId(); g_sys.current_screen = SCREEN_ABOUT; }
            else if (g_sys.selected_item == 3) { g_sys.selected_item = 0; g_sys.pixel_offset = 0; g_sys.can_edit_active = 0; g_sys.current_screen = SCREEN_CAN; }
            else if (g_sys.selected_item == 4) { g_sys.selected_item = 0; g_sys.current_screen = SCREEN_SETTINGS; }
            else if (g_sys.selected_item == 5) { g_sys.selected_item = 0; g_sys.pixel_offset = 0; g_sys.music_popup = 0; g_sys.current_screen = SCREEN_MUSIC; }
            else if (g_sys.selected_item == 6) { System_FlushSave(); NVIC_SystemReset(); }  /* 閲嶅惎鍓嶅厛鍒峰畬淇濆瓨 */
            else if (g_sys.selected_item == 7) {   /* 鎭㈠嶅嚭鍘傝剧疆锛氭竻flash+榛樿ゅ硷紝涓嶉噸鍚 */
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
            /* 缂栬緫涓锛氭棆杞鐩存帴鏀瑰硷紙瀹炴椂鍒锋柊鐢 UI_Update 閫愯岄噸缁橈級锛
             * 鍗曞嚮閫鍑哄綋鍓嶉」 鈫 鍚庡彴淇濆瓨 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                uint8_t drv_before = g_sys.params.motor_driver;
                motor_edit_set(g_sys.selected_item, (evt == ENC_EVT_CW));
                if (g_sys.selected_item == 7 && drv_before != g_sys.params.motor_driver) {
                    g_sys.ui_force_redraw = 1;   /* 驱动切换: 行数/内容变化, 触发一次重绘 */
                }
            } else if (evt == ENC_EVT_CLICK) {
                g_sys.motor_edit_active = 0;
                System_RequestSave();
                g_sys.ui_force_redraw = 1;   /* 退出编辑: 确保驱动变化后选项立即刷新 */
            }
        } else {
            if (evt == ENC_EVT_CW) UI_MotorScroll(1);
            else if (evt == ENC_EVT_CCW) UI_MotorScroll(-1);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == count - 1) {
                    System_RequestSave();      /* 閫鍑虹數鏈洪〉鍓嶄繚瀛橈紙鍚鏈鍗曞嚮閫鍑虹殑淇鏀癸級 */
                    g_sys.selected_item = 0; g_sys.current_screen = SCREEN_MENU;
                }
                else { g_sys.motor_edit_active = 1; }   /* 鍗曞嚮閫変腑 鈫 杩涘叆璇ラ」缂栬緫 */
            }
        }
        break;
    }

    case SCREEN_ABOUT:
        if (evt == ENC_EVT_CLICK) g_sys.current_screen = SCREEN_MENU;
        break;

    case SCREEN_WIFI:
        if (g_sys.wifi_edit_active) {
            /* WiFi 寮鍏崇紪杈戞侊紙鍚岀數鏈洪〉浜や簰锛氬崟鍑婚変腑鈫掓棆杞璋冭妭鈫掑啀鍗曞嚮閫鍑猴級锛
             * 鏃嬭浆鍒囨崲 寮/鍏筹紙瀹炴椂鏄剧ず锛孍SP 灏氫笉鎿嶄綔锛夛紱鍐嶅崟鍑绘彁浜ゅ苟閫鍑恒 */
            if (g_sys.selected_item != 0) {
                /* 鍏夋爣绂诲紑寮鍏宠岋紙寮傚父淇濋櫓锛夛細鍙栨秷骞惰繕鍘 */
                g_sys.wifi_enabled = g_sys.wifi_edit_orig;
                g_sys.wifi_edit_active = 0;
            } else if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                g_sys.wifi_enabled = !g_sys.wifi_enabled;
            } else if (evt == ENC_EVT_CLICK) {
                if (g_sys.wifi_enabled != g_sys.wifi_edit_orig) {
                    EspLink_OnToggle(g_sys.wifi_enabled);
                    System_RequestSave();
                }
                g_sys.wifi_edit_active = 0;
            }
        } else {
            if (evt == ENC_EVT_CW) g_sys.selected_item = EncWrap(4, 1, g_sys.selected_item);

            else if (evt == ENC_EVT_CCW) g_sys.selected_item = EncWrap(4, (int8_t)-1, g_sys.selected_item);
            else if (evt == ENC_EVT_CLICK) {
                uint8_t row = g_sys.selected_item;
                if (row == 0) {
                    /* WiFi 寮鍏筹細棣栧嚮鍙閫変腑锛堟欐嗘彁绀猴級锛屼笉鐩存帴鍒囨崲锛岄槻璇瑙 */
                    g_sys.wifi_edit_active = 1;
                    g_sys.wifi_edit_orig = g_sys.wifi_enabled;
                } else if (row == 1) {
                    /* 鎵嬪姩閰嶇綉锛氬紑閰嶇綉鐑鐐癸紙涓嶆敼寮鍏崇姸鎬侊級 */
                    if (g_sys.wifi_enabled) EspLink_OpenConfig();
                    else { g_sys.wifi_enabled = 1; EspLink_OnToggle(1); EspLink_OpenConfig(); }
                    System_RequestSave();
                } else if (row == 2) {
                    /* 閲嶆柊閰嶇綉锛氭竻5缁勫瓨鍙 + 寮閰嶇綉鐑鐐 */
                    if (g_sys.wifi_enabled) EspLink_StartConfig();
                    else { g_sys.wifi_enabled = 1; EspLink_OnToggle(1); EspLink_StartConfig(); }
                    System_RequestSave();
                } else {
                    /* 閫鍑 */
                    g_sys.wifi_edit_active = 0;
                    g_sys.current_screen = SCREEN_MENU;
                }
            }
        }
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
            /* 缂栬緫涓锛氭棆杞鐩存帴鏀瑰硷紙瀹炴椂鍒锋柊鐢 UI_Update 閫愯岄噸缁橈級锛屽崟鍑婚鍑衡啋淇濆瓨 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                if (g_sys.rgb_bright_popup) {   /* RGB 浜搴﹀脊绐 */
                    if (g_sys.rgb_bright_edit) {   /* 缂栬緫涓锛氭棆杞鐩存帴璋冩暣鏁板硷紙甯﹀姞閫燂級 */
                        if (g_sys.rgb_bright_sel <= 1) {
                            uint8_t *pv = g_sys.rgb_bright_sel ? &g_sys.params.rgb_strip_bright : &g_sys.params.rgb_led_bright;
                            int16_t v = (int16_t)*pv + ((evt == ENC_EVT_CW) ? (int16_t)enc_accel_step : -(int16_t)enc_accel_step);
                            if (v < 0) v = 0; else if (v > 100) v = 100;
                            *pv = (uint8_t)v;
                        }
                    } else {   /* 鏈閫変腑鍙傛暟锛氭棆杞绉诲姩鍏夋爣锛0鎸囩ず鐏 1鐏鏉 2瀹屾垚锛 */
                        if (evt == ENC_EVT_CW) g_sys.rgb_bright_sel = (g_sys.rgb_bright_sel + 1) % 3;
                        else g_sys.rgb_bright_sel = (g_sys.rgb_bright_sel == 0) ? 2 : (uint8_t)(g_sys.rgb_bright_sel - 1);
                    }
                } else {
                    settings_edit_set(g_sys.selected_item, (evt == ENC_EVT_CW));
                }
            } else if (evt == ENC_EVT_CLICK) {
                if (g_sys.rgb_bright_popup) {
                    if (g_sys.rgb_bright_edit) {   /* 缂栬緫鏁板间腑锛氬啀娆″崟鍑婚鍑哄弬鏁 */
                        g_sys.rgb_bright_edit = 0;
                    } else if (g_sys.rgb_bright_sel == 2) {   /* 瀹屾垚锛氶鍑哄苟淇濆瓨 */
                        g_sys.settings_edit_active = 0;
                        g_sys.rgb_bright_popup = 0;
                        g_sys.rgb_bright_edit = 0;
                        System_RequestSave();
                    } else {   /* 閫変腑鍙傛暟锛氳繘鍏ユ暟鍊肩紪杈 */
                        g_sys.rgb_bright_edit = 1;
                    }
                } else {
                    g_sys.settings_edit_active = 0;
                    System_RequestSave();
                }
            } else if (evt == ENC_EVT_LONG_PRESS) {   /* 闀挎寜閫鍑哄脊绐楀苟淇濆瓨 */
                g_sys.settings_edit_active = 0;
                g_sys.rgb_bright_popup = 0;
                g_sys.rgb_bright_edit = 0;
                System_RequestSave();
            }
        } else {
            if (evt == ENC_EVT_CW) UI_SettingsScroll(1);
            else if (evt == ENC_EVT_CCW) UI_SettingsScroll(-1);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item >= 7) g_sys.current_screen = SCREEN_MENU;  /* 閫鍑 */
                else {
                    g_sys.settings_edit_active = 1;
                    if (g_sys.selected_item == 6) { g_sys.rgb_bright_popup = 1; g_sys.rgb_bright_sel = 0; g_sys.rgb_bright_edit = 0; }
                }
            }
        }
        break;

    case SCREEN_CAN:
        if (g_sys.can_edit_active) {
            /* 缂栬緫涓锛氭棆杞鍒囨崲寮鍏/涓讳粠锛屽崟鍑婚鍑衡啋淇濆瓨 */
            if (evt == ENC_EVT_CW || evt == ENC_EVT_CCW) {
                if (g_sys.selected_item == 0) g_sys.params.can_enabled ^= 1;
                else if (g_sys.selected_item == 1) g_sys.params.can_role = (uint8_t)(1 - g_sys.params.can_role);
            } else if (evt == ENC_EVT_CLICK) {
                g_sys.can_edit_active = 0;
                System_RequestSave();
                g_sys.ui_force_redraw = 1;   /* 从机/主机切换后行2/3显示状态改变, 整屏重绘一次 */
            }
        } else {
            if (evt == ENC_EVT_CW) UI_CanScroll(1);
            else if (evt == ENC_EVT_CCW) UI_CanScroll(-1);
            else if (evt == ENC_EVT_CLICK) {
                if (g_sys.selected_item == 4) {   /* 閫鍑 */
                    g_sys.selected_item = 0;
                    g_sys.current_screen = SCREEN_MENU;
                } else if (g_sys.selected_item == 2) {   /* 搜索设备 */
                    if (g_sys.params.can_enabled && g_sys.params.can_role == 0 && g_sys.can_search_state == 0) {
                        g_sys.can_search_state = 1;          /* 搜索中 */
                        g_sys.can_search_cnt0 = g_sys.can_connected;
                        g_sys.can_search_t0 = SystemTime_Millis();
                        g_sys.can_search_last = g_sys.can_search_t0;
                        CAN_Cluster_RequestSearch();
                    }
                } else if (g_sys.selected_item <= 1) {
                    g_sys.can_edit_active = 1;
                }
            }
        }
        break;

case SCREEN_MUSIC:
        /* 闊充箰涓婚〉锛氬脊绐楁墦寮鏃舵棆杞鏃犳晥锛堜笉绉诲姩鍏夋爣锛夛紝浠呭崟鍑绘寜闃舵靛勭悊 */
        if (g_sys.music_popup) {
            if (evt == ENC_EVT_CLICK && g_sys.selected_item == 0) {
                if (g_sys.music_popup == 1) {
                    EspLink_MusicOpenAp();            /* 寮鍚涓婁紶 AP */
                } else if (g_sys.music_popup == 2) {
                    if (g_sys.wifi_ap_mode) {          /* AP 已开: 再点关闭 */
                        EspLink_MusicCloseAp();
                        g_sys.music_popup = 0;
                        g_sys.wifi_ap_mode = 0;
                    } else {                            /* AP 没起来(冷启动中/失败): 再点重试冷启动 */
                        EspLink_MusicOpenAp();
                    }
                }
                /* 3=涓婁紶涓 4=瀹屾垚 5=澶辫触锛氬崟鍑讳笉鎵撴柇 */
            }
            break;
        }
        if (evt == ENC_EVT_CW) { enc_accel_step = 1; UI_MusicScroll(1); }
        else if (evt == ENC_EVT_CCW) { enc_accel_step = 1; UI_MusicScroll(-1); }
        else if (evt == ENC_EVT_CLICK) {
            if (g_sys.selected_item == 0) {           /* 涓婁紶闊充箰 */
                EspLink_MusicOpenAp();               /* single click: open AP popup=2 */
            } else if (g_sys.selected_item == 1) {    /* 闊充箰鍒楄〃 */
                g_sys.selected_item = 0;
                g_sys.pixel_offset = 0;
                g_sys.music_marquee = 0;
                g_sys.current_screen = SCREEN_MUSIC_LIST;
            } else {                                  /* 閫鍑 */
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_MENU;
            }
        }
        break;

    case SCREEN_MUSIC_LIST:
        if (evt == ENC_EVT_CW) UI_MusicListScroll(1);
        else if (evt == ENC_EVT_CCW) UI_MusicListScroll(-1);
        else if (evt == ENC_EVT_LONG_PRESS) {
            /* 长按清空音乐分区(外部Flash): 列表内容只存外部Flash, 重烧App不会清除, 需手动清 */
            MusicStore_Wipe();
            MusicPlay_Stop();
            g_sys.selected_item = 0;
            g_sys.pixel_offset = 0;
            g_sys.music_marquee = 0;
            g_sys.ui_force_redraw = 1;
        }
        else if (evt == ENC_EVT_CLICK) {
            uint16_t cnt = MusicPlay_TrackCount();
            if (g_sys.selected_item >= cnt) {         /* 閫鍑鸿 */
                g_sys.selected_item = 0;
                g_sys.current_screen = SCREEN_MUSIC;
            } else {                                  /* 鎾鏀/鍋滄㈡煇鏇 */
                if (MusicPlay_IsPlaying() && MusicPlay_CurTrack() == g_sys.selected_item) {
                    MusicPlay_Stop();                 /* 鍐嶆″崟鍑诲仠姝 */
                } else {
                    MusicPlay_Play(g_sys.selected_item); /* 鍗曞嚮涓娆″彧鎾涓閬 */
                }
            }
        }
        break;

default:
        break;
    }
}
#endif /* BOOTLOADER_BUILD */

