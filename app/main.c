#include "stm32f10x.h"

#include "board.h"
#include "bsp_w25q128.h"
#include "ota_display.h"
#include "platform_contract.h"
#include "system_time.h"
#include "system_config.h"
#include "bsp_tft_st7789.h"

#ifndef BOOTLOADER_BUILD
#include "bsp_aht20.h"
#include "bsp_buzzer.h"
#include "bsp_cs1237.h"
#include "bsp_encoder.h"
#include "bsp_fan.h"
#include "bsp_ntc.h"
#include "bsp_ptc.h"
#include "bsp_rgb_led.h"
#include "bsp_stepper.h"
#include "esp_at.h"
#include "esp_http_bridge.h"
#include "http_server.h"
#include "ota_http.h"
#include "ota_metadata_store.h"
#include "ota_update_controller.h"
#include "ota_upload.h"
#include "mod_ota.h"
#include "ui_manager.h"
#include "pin_config.h"
#include <string.h>
#include <stdio.h>
#endif

SystemState_t g_sys;

#ifndef BOOTLOADER_BUILD
static void refresh_api_data(void);
static void read_sensors(void);
static void safety_check(void);
static void control_update(void);
static void update_rgb(void);
static void trigger_safety(SafetyState_t state);
static void pid_reset(void);
#endif

#ifndef BOOTLOADER_BUILD
int main(void)
{
    uint32_t ui_tick = 0;
    uint32_t now;

    /* ── 点亮背光(PB0 推挽高) ── */
    {
        GPIO_InitTypeDef g;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        g.GPIO_Pin = PIN_TFT_BL_PIN;
        g.GPIO_Mode = GPIO_Mode_Out_PP;
        g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(PIN_TFT_BL_PORT, &g);
        GPIO_SetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
    }

    /* g_sys 初始化（UI 需要） */
    g_sys.params.target_temp = TEMP_DEFAULT;
    g_sys.params.dry_time_sec = TIME_DEFAULT_SEC;
    g_sys.params.ptc_max_temp = PTC_TEMP_DEFAULT;
    g_sys.params.ptc_cooling_temp = PTC_COOLING_TEMP_DEFAULT;
    g_sys.params.pid_kp = 10.0f;
    g_sys.params.pid_ki = 0.5f;
    g_sys.params.pid_kd = 2.0f;
    g_sys.params.motor_enabled = 1;
    g_sys.params.motor_direction = 0;
    g_sys.params.motor_speed = 5;
    g_sys.params.motor_oscillate = 0;
    g_sys.params.motor_oscillate_angle = 30;
    g_sys.params.motor_driver = MOTOR_DRIVER_A4988;
    g_sys.params.motor_current = 2;
    g_sys.params.motor_stealthchop = 0;
    g_sys.params.rgb_enabled = 1;            /* RGB灯条默认开 */

    g_sys.current_temp = 25.0f;
    g_sys.current_humidity = 50.0f;
    g_sys.weight_g = 0.0f;
    g_sys.ptc_temp = 25.0f;
    g_sys.run_state = STATE_IDLE;
    g_sys.current_screen = SCREEN_MAIN;
    g_sys.selected_item = 0;
    g_sys.submenu_active = 0;
    g_sys.time_cursor = TIME_DIGIT_H1;
    g_sys.temp_edit_active = 0;
    g_sys.ptc_edit_active = 0;
    g_sys.pid_autotune_running = 0;
    g_sys.pid_autotune_progress = 0;
    g_sys.temp_pid_running = 0;
    g_sys.temp_pid_progress = 0;
    g_sys.complete_timer = 0;
    g_sys.device_id = 0;
    g_sys.safety_state = SAFETY_NONE;
    g_sys.fan_speed = 0;
    g_sys.drying_active = 0;
    g_sys.chamber_temp_last = 25.0f;

    /* 不用 Board_Init（含 NTC_Init 的 ADC 校准 while，未接传感器可能卡死）。
     * 只做安全引脚初始化（加热器关断 + SWJ 重映射）。 */
    Board_EarlyInit();
    Watchdog_Init();   /* App 自启看门狗，不依赖 Bootloader */

    /* 关键：从 RCC 读回实际时钟，纠正 SystemCoreClock（与 bootloader 一致）。
     * 否则 SysTick 周期错，SystemTime_Millis 不走。 */
    SystemCoreClockUpdate();
    SystemTime_Init(); /* 启动 SysTick，供 SystemTime_Millis/Encoder 计时 */

    /* Bootloader 在 BootloaderV2_JumpToApp() 跳转前调用了 __disable_irq()，
     * 而 App 的 SystemInit/main 从不重新开中断 → PRIMASK 保持 1 → SysTick
     * 永不触发 → SystemTime_Millis 冻结 → 编码器单击/长按计时全部失效
     * （旋转仍可用，因其只轮询 GPIO 不依赖中断）。此处必须重新开中断。 */
    __enable_irq();

    /* 上电长按编码器(约1s) → 强制进入 Bootloader 下载模式。
     * 直接轮询按钮引脚，不依赖 Encoder_Process()（它内部会消费事件，
     * 导致这里再调 Encoder_GetEvent() 永远拿不到 LONG_PRESS）。 */
    {
        Encoder_Init();
        int force_boot = 0;
        uint32_t t0 = SystemTime_Millis();
        uint32_t btn_t0 = 0;
        while ((int32_t)(SystemTime_Millis() - t0) < 3000) {
            Watchdog_Kick();
            if (GPIO_ReadInputDataBit(PIN_ENC_BTN_PORT, PIN_ENC_BTN_PIN) == 0) {
                if (btn_t0 == 0) btn_t0 = SystemTime_Millis();
                else if ((int32_t)(SystemTime_Millis() - btn_t0) >= 1000) {
                    force_boot = 1;
                    break;
                }
            } else {
                btn_t0 = 0;
            }
        }
        if (force_boot) {
            OTA_EnterBootloader();   /* 写 FORCE_BOOT 标志并复位，不会返回 */
        }
    }

    /* 屏幕初始化 + 主界面 */
    TFT_Init();
    Buzzer_Init();
    Backlight_Init();

    g_sys.buzzer_link = 1;
    g_sys.buzzer_vol = 5;
    g_sys.light_switch = 1;
    g_sys.backlight = 100;
    g_sys.theme = 1;  /* 默认暗色主题 #1E1E2E */
    g_sys.screen_off_timeout = 0;  /* 默认从不熄屏 */
    TFT_SetBrightness(g_sys.backlight);
    theme_apply();

    /* AHT20 温湿度传感器初始化（软件I2C，PB10=SCL / PB11=SDA）。
     * 重试3次应对上电时序；初始化失败不阻塞启动，界面仍显示默认值，
     * 周期性 read_sensors() 内继续尝试读取。 */
    {
        int aht_try;
        for (aht_try = 0; aht_try < 3; aht_try++) {
            if (AHT20_Init() == 0) break;
            { volatile uint32_t d = 0; while (d < 100000U) d++; }
        }
    }

    /* 加热/风扇/NTC 初始化（用户已接好对应硬件）：
     * PTC 晶闸管 PWM = TIM1_CH1(PA8)，风扇 PWM = TIM1_CH4(PA11)，
     * NTC 热敏 = ADC1(PA2)。NTC 已接，ADC 校准循环可安全完成。 */
    PTC_Init();
    Fan_Init();
    NTC_Init();
    RGB_Strip_Init();              /* WS2812 RGB 灯条（PB6=状态灯，PB7=7颗进度灯） */
    Stepper_Init();                /* 步进电机 GPIO（PB12-14）初始化，默认使能脚高电平=关 */

    UI_ShowBootScreen();        /* 进度条动画 + 跑完关背光(变暗) */
    UI_DrawMainScreen();        /* 背光暗时绘制主界面 */
    /* 背光渐变亮起，露出主界面（过渡动画） */
    {
        uint16_t b;
        for (b = 0U; b <= 100U; b += 5U) {
            TFT_SetBrightness(b);
            Watchdog_Kick();
            { volatile uint32_t d = 0; while (d < 200000U) d++; }  /* 约 14ms */
        }
        TFT_SetBrightness(100);
    }

    /* 编码器：Encoder_Process 内部处理旋转/单击，长按进菜单由下方独立检测。
     * 旋转后整屏重绘主界面（无白框残影，选中框随卡片高亮）。 */
    {
        uint32_t btn_press_ms = 0;
        uint8_t  btn_was_down = 0;
        uint8_t  btn_long_done = 0;
        Screen_t btn_press_screen = (Screen_t)0xFF;
        uint8_t  last_sel = 0xFF;
        uint32_t last_activity = SystemTime_Millis();  /* 熄屏计时 */
        uint32_t sensor_tick = 0;              /* 传感器/安全/控制 200ms 周期 */
        uint8_t  screen_off = 0;
        for (;;) {
            now = SystemTime_Millis();
            Watchdog_Kick();
            Stepper_Update();   /* 步进电机脉冲生成，每轮循环调用，内部按 step_period_us 计时 */

            /* 检测编码器输入(旋转/按钮)，更新活动时间，若已熄屏则唤醒 */
            if (g_last_input_ms != last_activity) {
                last_activity = g_last_input_ms;
                if (screen_off) {
                    screen_off = 0;
                    TFT_SetBrightness(g_sys.backlight);
                }
            }
            /* 熄屏超时检查: 0=从不 1-8 对应 1/5/10/20/30/60/120/300s */
            if (!screen_off && g_sys.screen_off_timeout > 0U) {
                static const uint16_t off_secs[9] = {0, 1, 5, 10, 20, 30, 60, 120, 300};
                uint16_t to = (g_sys.screen_off_timeout < 9U) ? off_secs[g_sys.screen_off_timeout] : 0U;
                if (to > 0U && (now - last_activity) > (uint32_t)to * 1000U) {
                    TFT_SetBrightness(0);
                    screen_off = 1;
                }
            }
            {
                uint8_t old_sel = last_sel;
                Encoder_Process();   /* 旋转/单击/长按由内部状态机处理 */
                if (g_sys.current_screen == SCREEN_MAIN && last_sel != 0xFF
                    && g_sys.selected_item != last_sel) {
                    UI_RefreshCard(old_sel);
                    UI_RefreshCard(g_sys.selected_item);
                }
                last_sel = g_sys.selected_item;
            }

            /* 独立长按检测：主界面/菜单之间切换（湿度卡上长按由编码器处理烘干，不进菜单） */
            if (GPIO_ReadInputDataBit(PIN_ENC_BTN_PORT, PIN_ENC_BTN_PIN) == 0) {
                if (!btn_was_down) {
                    btn_press_ms = now;
                    btn_was_down = 1;
                    btn_long_done = 0;
                    btn_press_screen = g_sys.current_screen;
                } else if (!btn_long_done && (int32_t)(now - btn_press_ms) >= 1000) {
                    btn_long_done = 1;
                    if (btn_press_screen == SCREEN_MAIN && g_sys.selected_item != 1) {
                        g_sys.current_screen = SCREEN_MENU;
                    } else if (btn_press_screen == SCREEN_MENU) {
                        g_sys.current_screen = SCREEN_MAIN;
                    }
                }
            } else {
                btn_was_down = 0;
            }

            /* 传感器/安全/控制周期任务（200ms，AHT20 读取阻塞约几十ms，看门狗4s窗口内安全） */
            if ((int32_t)(now - sensor_tick) >= (int32_t)200) {
                sensor_tick = now;
                read_sensors();
                safety_check();
                control_update();
                update_rgb();
            }

            if ((int32_t)(now - ui_tick) >= (int32_t)50) {
                ui_tick = now;
                UI_Update();
            }
        }
    }
}
#endif /* BOOTLOADER_BUILD */

#ifndef BOOTLOADER_BUILD
#define APP_VERSION_TEXT        "0.1.0"
#define BOOTLOADER_VERSION_TEXT "0.1.0"
#define SAFETY_DROP_THRESHOLD   2.0f
#define SAFETY_STUCK_MINUTES    2

static void refresh_api_data(void)
{
    HttpApiData_t data;
    OtaMetadata_t metadata;
    memset(&data, 0, sizeof(data));
    strcpy(data.app_version, APP_VERSION_TEXT);
    strcpy(data.bootloader_version, BOOTLOADER_VERSION_TEXT);
    data.ota_state = OTA_STATE_IDLE;
    if (OtaMetadataStore_Load(&metadata, 0) == OTA_METADATA_STORE_OK) {
        data.ota_state = (OtaState_t)metadata.state;
        if ((metadata.state == (uint32_t)OTA_STATE_RECEIVING) && (OtaUpload_GetState() == OTA_UPLOAD_STATE_IDLE) && OtaUpload_IsStoragePrepared()) data.ota_state = OTA_STATE_IDLE;
        data.staged_size = metadata.image_size;
        data.staged_crc32 = metadata.image_crc32;
        data.staged_crc_valid = (metadata.state == (uint32_t)OTA_STATE_READY) || (metadata.state == (uint32_t)OTA_STATE_APPLYING) || (metadata.state == (uint32_t)OTA_STATE_APPLIED);
    }
    HttpServer_SetApiData(&data);
}

static void read_sensors(void)
{
    float temp, hum, weight;
    int16_t ptc_raw;

    /* AHT20 读取失败：保持上次温度，但若正在烘干则触发安全保护，
     * 避免"传感器假 25°C → 永远认为没到温 → 持续加热"的致命场景 */
    if (AHT20_Read(&temp, &hum) != 0) {
        if (g_sys.drying_active && g_sys.safety_state == SAFETY_NONE) {
            trigger_safety(SAFETY_BOX_BROKEN);
        }
    } else {
        g_sys.current_temp = temp;
        g_sys.current_humidity = hum;
    }

    weight = CS1237_ReadWeight();
    if (weight >= 0.0f) g_sys.weight_g = weight;

    ptc_raw = NTC_GetTemperature();
    g_sys.ptc_temp = (float)ptc_raw / 10.0f;

    /* NTC 开路/短路异常（映射到极端值）也触发安全保护
     * 【测试期注销】未接实际加热片，先测晶闸管能否正常打开；测试后恢复：
     * if (ptc_raw <= -100 || ptc_raw >= 2000) {
     *     if (g_sys.drying_active && g_sys.safety_state == SAFETY_NONE) {
     *         trigger_safety(SAFETY_BOX_BROKEN);
     *     }
     * }
     */
}

static void update_rgb(void)
{
    /* 灯光总开关：关则直接灭两个灯条 */
    if (!g_sys.light_switch) {
        RGB_Status_Off();
        uint8_t black[3] = {0,0,0};
        RGB_Strip3_SetPixels(black, 1);
        return;
    }
    static uint8_t rgb_tick = 0;
    static uint32_t complete_start = 0;
    rgb_tick++;
    if (g_sys.safety_state != SAFETY_NONE) {
        RGB_Status_Red();
        RGB_Progress_ColorWheel(rgb_tick);
        return;
    }
    if (g_sys.run_state == STATE_HEATING || g_sys.run_state == STATE_DRYING) {
        RGB_Status_Red();
        uint8_t pct = 0;
        if (g_sys.params.dry_time_sec > 0)
            pct = (uint8_t)(g_sys.remaining_sec * 100U / g_sys.params.dry_time_sec);
        if (pct > 100) pct = 100;
        RGB_Progress_DryingBar(pct);
        complete_start = 0;
    } else if (g_sys.run_state == STATE_COMPLETE) {
        if (complete_start == 0) complete_start = SystemTime_Millis();
        if (SystemTime_Millis() - complete_start < 30000) {
            RGB_Status_Green();
        } else {
            RGB_Status_Off();
        }
        RGB_Progress_Rainbow();
    } else {
        /* 空闲：PB6 灭，PB7 彩虹流动 */
        RGB_Status_Off();
        RGB_Progress_Rainbow();
        complete_start = 0;
    }
}

static void trigger_safety(SafetyState_t state)
{
    if (g_sys.safety_state != SAFETY_NONE) return;
    pid_reset();
    g_sys.safety_state = state;
    g_sys.run_state = STATE_IDLE;
    g_sys.drying_active = 0;
    PTC_SetPower(0);
    Fan_SetSpeed(100);
    Stepper_Enable(0);
    Buzzer_Beep(200);
    Buzzer_Beep(300);
    Buzzer_Beep(200);
    g_sys.prev_screen = g_sys.current_screen;
    g_sys.current_screen = SCREEN_SAFETY_ALERT;
}

void StartDrying(void)
{
    pid_reset();
    g_sys.drying_active = 1;
    g_sys.run_state = STATE_HEATING;
    g_sys.remaining_sec = g_sys.params.dry_time_sec;
    g_sys.temp_stuck_start = SystemTime_Millis();
    g_sys.safety_state = SAFETY_NONE;
    g_sys.chamber_temp_last = g_sys.current_temp;
    PTC_SetPower(100);
    Fan_SetSpeed(100);                       /* 风扇恒全功率 */
    if (g_sys.params.motor_enabled) {
        Stepper_Enable(1);
        Stepper_SetSpeed(g_sys.params.motor_speed * 200);
        if (g_sys.params.motor_oscillate) Stepper_SetOscillate(g_sys.params.motor_oscillate_angle * 10);
        else Stepper_Move(g_sys.params.motor_direction ? -100000 : 100000);
    }
}

void StopDrying(void)
{
    pid_reset();
    g_sys.drying_active = 0;
    g_sys.run_state = STATE_COOLING;
    PTC_SetPower(0);
    Fan_SetSpeed(100);
    Stepper_Enable(0);
}

void PauseDrying(void)
{
    if (g_sys.run_state != STATE_HEATING && g_sys.run_state != STATE_DRYING) return;
    pid_reset();
    g_sys.run_state = STATE_PAUSED;
    PTC_SetPower(0);
    Fan_SetSpeed(100);
    Stepper_Enable(0);
}

void ResumeDrying(void)
{
    if (g_sys.run_state != STATE_PAUSED) return;
    pid_reset();
    g_sys.drying_active = 1;
    g_sys.temp_stuck_start = SystemTime_Millis();
    g_sys.chamber_temp_last = g_sys.current_temp;
    g_sys.run_state = (g_sys.current_temp < (float)g_sys.params.target_temp - 1.0f)
                      ? STATE_HEATING : STATE_DRYING;
    Fan_SetSpeed(100);
    if (g_sys.params.motor_enabled) {
        Stepper_Enable(1);
        Stepper_SetSpeed(g_sys.params.motor_speed * 200);
        if (g_sys.params.motor_oscillate) Stepper_SetOscillate(g_sys.params.motor_oscillate_angle * 10);
        else Stepper_Move(g_sys.params.motor_direction ? -100000 : 100000);
    }
}

static void safety_check(void)
{
    uint32_t now = SystemTime_Millis();
    if (g_sys.safety_state != SAFETY_NONE || !g_sys.drying_active) return;
    if (g_sys.run_state != STATE_HEATING && g_sys.run_state != STATE_DRYING) return;
    if (g_sys.chamber_temp_last - g_sys.current_temp > SAFETY_DROP_THRESHOLD) { trigger_safety(SAFETY_BOX_BROKEN); return; }
    if (g_sys.temp_stuck_start == 0) { g_sys.temp_stuck_start = now; }
    if ((int32_t)(now - g_sys.temp_stuck_start) >= (int32_t)(SAFETY_STUCK_MINUTES * 60000)) {
        if (g_sys.current_temp - g_sys.chamber_temp_last < 1.0f) { trigger_safety(SAFETY_LID_OPEN); return; }
        g_sys.temp_stuck_start = now;
    }
    g_sys.chamber_temp_last = g_sys.current_temp;
}

/* 双 PID：空气温度 PID（主控，反馈 AHT20）+ 加热器温度 PID（保护，反馈 NTC） */
static float pid_air_int = 0.0f, pid_air_prev = 0.0f;
static uint32_t pid_air_tick = 0;
static float pid_ntc_int = 0.0f, pid_ntc_prev = 0.0f;
static uint32_t pid_ntc_tick = 0;

static void pid_reset(void)
{
    pid_air_int = 0.0f; pid_air_prev = 0.0f; pid_air_tick = 0U;
    pid_ntc_int = 0.0f; pid_ntc_prev = 0.0f; pid_ntc_tick = 0U;
}

/* 通用 PID 步进：输出 0-100 功率。setpoint=目标温度，measure=反馈温度 */
static uint8_t pid_step(float *integral, float *prev, uint32_t *tick,
                        uint32_t now, float setpoint, float measure)
{
    uint8_t first = (*tick == 0U);
    float dt = first ? 0.2f : (float)(now - *tick) / 1000.0f;
    if (dt <= 0.0f || dt > 1.0f) dt = 0.2f;
    float err = setpoint - measure;
    float der = first ? 0.0f : (err - *prev) / dt;   /* 首帧不微分，防尖峰 */
    *integral += err * dt;
    if (*integral > 50.0f) *integral = 50.0f;
    if (*integral < -50.0f) *integral = -50.0f;
    *prev = err;
    *tick = now;
    float out = g_sys.params.pid_kp * err
              + g_sys.params.pid_ki * (*integral)
              + g_sys.params.pid_kd * der;
    if (out > 100.0f) out = 100.0f;
    if (out < 0.0f)   out = 0.0f;
    return (uint8_t)out;
}

static void control_update(void)
{
    float target = (float)g_sys.params.target_temp;      /* 空气目标温度 */
    float ntc_max = (float)g_sys.params.ptc_max_temp;    /* 加热器上限温度(NTC) */
    static uint32_t last_tick = 0;
    uint32_t now = SystemTime_Millis();

    /* 独立硬过温保护：PTC 温度超限立即切断加热，不依赖控制状态。
     * 【测试期注销】未接实际加热片，先测晶闸管能否正常打开——
     * 避免 NTC 短暂误读(冷态/悬空)就把 PTC 切断并弹告警屏。测试后恢复：
     * if (g_sys.drying_active && NTC_IsOverTemp()) {
     *     PTC_SetPower(0);
     *     trigger_safety(SAFETY_BOX_BROKEN);
     *     return;
     * }
     */

    if (g_sys.safety_state != SAFETY_NONE) { PTC_SetPower(0); return; }

    switch (g_sys.run_state) {
    case STATE_HEATING:
    case STATE_DRYING: {
        /* 空气 PID：把 AHT20 空气温度拉到目标（误差大→100% 快速升温）。
         * NTC PID：把加热器温度限制在 ptc_max_temp 以内（NTC 到上限→压功率）。
         * PTC 取两者较小值：气没到目标就满功率，加热器接近上限就限功。 */
        uint8_t p_air = pid_step(&pid_air_int, &pid_air_prev, &pid_air_tick,
                                 now, target, g_sys.current_temp);
        uint8_t p_ntc = pid_step(&pid_ntc_int, &pid_ntc_prev, &pid_ntc_tick,
                                 now, ntc_max, g_sys.ptc_temp);
        PTC_SetPower(p_air < p_ntc ? p_air : p_ntc);

        if (g_sys.run_state == STATE_HEATING && g_sys.current_temp >= target) {
            g_sys.run_state = STATE_DRYING;   /* 空气达目标 → 开始恒温倒计时 */
            last_tick = now;
        }
        if (g_sys.run_state == STATE_DRYING) {
            /* 倒计时：每 200ms 调用一次，累计满 1 秒才减 1 秒 */
            if (last_tick == 0) last_tick = now;
            if ((int32_t)(now - last_tick) >= (int32_t)1000) {
                last_tick = now;
                if (g_sys.remaining_sec > 0) {
                    g_sys.remaining_sec--;
                } else {
                    StopDrying();
                }
            }
        }
        break;
    }
    case STATE_COOLING:
        Fan_SetSpeed(100);
        if (g_sys.ptc_temp <= (float)g_sys.params.ptc_cooling_temp) { g_sys.run_state = STATE_COMPLETE; Fan_Off(); }
        break;
    case STATE_PAUSED:
        PTC_SetPower(0);
        break;
    default:
        /* 空闲/完成状态：持续温度安全——只检测 NTC 超过冷却温度 */
        PTC_SetPower(0);
        if (g_sys.ptc_temp > (float)g_sys.params.ptc_cooling_temp) {
            Fan_SetSpeed(100);
        } else {
            Fan_Off();
        }
        break;
    }
}
#endif /* BOOTLOADER_BUILD */