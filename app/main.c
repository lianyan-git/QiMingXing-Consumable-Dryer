#include "stm32f10x.h"

#include "board.h"
#include "bsp_w25q128.h"
#include "ota_display.h"
#include "platform_contract.h"
#include "system_time.h"
#include "system_config.h"
#include "bsp_tft_st7789.h"
#include "sfud_flash.h"

#ifndef BOOTLOADER_BUILD
#include "bsp_sht40.h"
#include "bsp_buzzer.h"
#include "bsp_can.h"
#include "bsp_cs1237.h"
#include "bsp_encoder.h"
#include "bsp_fan.h"
#include "bsp_ntc.h"
#include "bsp_ptc.h"
#include "bsp_rgb_led.h"
#include "bsp_stepper.h"
#include "esp_link.h"
#include "bsp_font_store.h"
#include "can_cluster.h"
#include "music_play.h"
#include "music_store.h"
#include "music_ota.h"
#include "lang_ota.h"
#include "mod_ota.h"
#include "ui_manager.h"
#include "pin_config.h"
#include <string.h>
#include <stdio.h>
#endif

#define SWING_BASE_STEPS_PER_DEG  4545U  /* base steps/deg*100, then x motor_swing_cal/100; 满摆180度@300%时60度约45.45步/度 */
#define SWING_CAL_DEFAULT         100   /* swing cal default %; adjust via UI to match real swing */

SystemState_t g_sys;

#ifndef BOOTLOADER_BUILD
static void read_sensors(void);
static uint8_t s_rgb_ready = 0;   /* 屏渐亮完成后才允许 RGB 灯效（上电熄灯待命） */
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

    /* 先点亮背光（TFT_BL=PB0 推挽输出）防止后续屏幕全黑 */
    {
        GPIO_InitTypeDef g;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        g.GPIO_Pin = PIN_TFT_BL_PIN;
        g.GPIO_Mode = GPIO_Mode_Out_PP;
        g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(PIN_TFT_BL_PORT, &g);
        GPIO_SetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
    }

    /* g_sys.params 默认参数（外部 Flash 无保存时用这些） */
    g_sys.params.target_temp = TEMP_DEFAULT;
    g_sys.params.dry_time_sec = TIME_DEFAULT_SEC;
    g_sys.params.ptc_max_temp = PTC_TEMP_DEFAULT;
    g_sys.params.ptc_cooling_temp = PTC_COOLING_TEMP_DEFAULT;
    g_sys.params.pid_air_kp = 10.0f;
    g_sys.params.pid_air_ki = 0.5f;
    g_sys.params.pid_air_kd = 2.0f;
    g_sys.params.pid_ntc_kp = 10.0f;
    g_sys.params.pid_ntc_ki = 0.5f;
    g_sys.params.pid_ntc_kd = 2.0f;
    g_sys.params.motor_enabled = 1;
    g_sys.params.motor_direction = 0;
    g_sys.params.motor_speed = 5;
    g_sys.params.motor_oscillate = 0;
    g_sys.params.motor_oscillate_angle = 60;  /* 摆动角度（默认度数值）1-360 */
    g_sys.params.motor_driver = MOTOR_DRIVER_A4988;
    g_sys.params.motor_current = 2;
    g_sys.params.motor_stealthchop = 0;
    g_sys.params.motor_work_count = 0;      /* 0=work forever, N=rest every N cycles */
    g_sys.params.motor_rest_sec = 0;        /* 0=不休息, N=每N周期后暂停N秒 */
    g_sys.params.motor_swing_cal = SWING_CAL_DEFAULT;
    g_sys.params.rgb_enabled = 1;
    g_sys.params.rgb_led_bright = 100;
    g_sys.params.rgb_strip_bright = 100;
    g_sys.params.can_enabled = 0;   /* CAN 默认关闭 */
    g_sys.params.can_role = 0;      /* 默认主机 */

    /* drying presets: 4 built-ins, current = PETG */
    {
        static const char def_names[PRESET_BUILTIN][9] = {"PLA", "PETG", "PETG CF", "ABS"};
        static const uint8_t def_temps[PRESET_BUILTIN] = {45, 60, 65, 80};
        static const uint32_t def_times[PRESET_BUILTIN] = {12U*3600U, 12U*3600U, 12U*3600U, 8U*3600U};
        uint8_t i;
        for (i = 0; i < PRESET_BUILTIN; i++) {
            strncpy(g_sys.params.presets[i].name, def_names[i], PRESET_NAME_MAX);
            g_sys.params.presets[i].temp = def_temps[i];
            g_sys.params.presets[i].time_sec = def_times[i];
        }
        g_sys.params.preset_count = PRESET_BUILTIN;
        g_sys.params.current_preset = 1;   /* PETG */
    }            /* 默认预设完成 */

    g_sys.current_temp = 25.0f;
    g_sys.current_humidity = 50.0f;
    g_sys.weight_g = 0;
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

    /* 板级早期初始化：GPIO 与复位时钟（含 NTC/ADC 引脚）等；
     * 若在此前长时间 while 等待会造成复位看门狗喂不进，需尽早完成初始化 */
    Board_EarlyInit();
    Watchdog_Init();   /* 看门狗初始化：App 复位后不被 Bootloader 遗留状态干扰 */

    /* 更新时钟后校正 SystemCoreClock 等，与 Bootloader 时钟配置保持一致 */
    SystemCoreClockUpdate();
    SystemTime_Init(); /* 启动 SysTick 计时，供 SystemTime_Millis/Encoder 使用 */

    /* Bootloader 经 BootloaderV2_JumpToApp() 跳转至此前会调用 __disable_irq()；
     * App 从 SystemInit/main 重新初始化，需要恢复 PRIMASK=0，否则 SysTick
     * 中断仍被屏蔽，SystemTime_Millis 无法推进；开中断后再做后续初始化 */
    __enable_irq();

    /* 等待数秒(s) 检测按键进入 Bootloader 升级模式；
     * 按下超过 1s 即进入，否则继续跑 Encoder_Process()；
     * 该等待期间只识别 Encoder_GetEvent() 的长按 LONG_PRESS */
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
            OTA_EnterBootloader();   /* 长按进入 Bootloader 升级模式并软复位 */
        }
    }

    /* 初始化显示、蜂鸣器、背光等外设 */
    TFT_Init();
    Buzzer_Init();
    Backlight_Init();

    g_sys.buzzer_link = 1;
    g_sys.buzzer_vol = 5;
    g_sys.light_switch = 1;
    g_sys.backlight = 100;
    g_sys.theme = 1;  /* 默认浅色主题，强调色 #1E1E2E */
    g_sys.screen_off_timeout = 0;  /* 默认不自动熄屏 */
    g_sys.wifi_enabled = 1;
    g_sys.pid_calibrated = 0;
    SfudFlash_Init();   /* 外部Flash探测/清写保护(须早于 LangInit; 参数读取后移到有字库分支, 引导页用默认值不熄屏) */
    EspLink_Init();                /* ESP 控制 GPIO（B12-14）等外设初始化，负责与 ESP01S 通信 */
    /* 引导语言页: 外部 Flash 无有效字库 → 开 language AP 显示下载页 */
    if (LangInit() != 0) {
        g_sys.lang_ap_active = 1;
        g_sys.current_screen = SCREEN_LANG_LOAD;
        /* 本分支跳过了正常启动的亮度渐变，需手动点亮背光，否则引导页在全黑下不可见 */
        TFT_SetBrightness(30);
        { uint16_t bi; for (bi = 30U; bi <= 100U; bi += 10U) {
            TFT_SetBrightness(bi);
            Watchdog_Kick();
            { volatile uint32_t d = 0; while (d < 120000U) d++; }
        } }
        TFT_SetBrightness(100);
        EspLink_LangOpenAp();
        g_sys.ui_force_redraw = 1;   /* 首帧画引导页(SSID/密码/进度/诊断) */
    }
    if (!g_sys.lang_ap_active) {
        /* lang guide page has no fonts: skip param load + sensor init so it never sleeps */
        System_LoadParams();
        TFT_SetBrightness(g_sys.backlight);
        theme_apply();
        {
            int sht_try;
            for (sht_try = 0; sht_try < 3; sht_try++) {
                if (SHT40_Init() == 0) break;
                { volatile uint32_t d = 0; while (d < 100000U) d++; }
            }
        }
        PTC_Init();
        Fan_Init();
        NTC_Init();
        CS1237_Init();
        { volatile uint32_t d = 0; while (d < 3000000U) d++; }
        RGB_Strip_Init();
        Stepper_Init();
        CAN_Cluster_Init();
        UI_ShowBootScreen();   /* boot progress screen + sensor read */
    
        /* 音乐：外部固件存储 / 上传接收 / 播放（TIM3 借用） */
        MusicStore_Init();
        MusicOta_Init();
        MusicPlay_Init();
        UI_DrawMainScreen();        /* 绘制主界面（四卡片+状态） */
        /* 完成主界面绘制后开启动效灯带，随亮度渐亮逐步点亮 */
        s_rgb_ready = 1;               /* 涓庡睆骞曟笎浜鍚屾ワ細姝ゅ埢璧风伅鏉℃墠寮鍚鍔ㄦ晥 */
        {
            uint16_t b;
            for (b = 0U; b <= 100U; b += 5U) {
                TFT_SetBrightness(b);
                Watchdog_Kick();
                { volatile uint32_t d = 0; while (d < 200000U) d++; }  /* 每级约 14ms */
            }
            TFT_SetBrightness(100);
        }
    }

    /* 主循环：Encoder_Process 统一处理旋转/按键；
     * 休眠时任意输入会唤醒（内部刷新 screen_off），
     * 正常处理旋转/单击/长按事件，每次输入刷新 g_last_input_ms */
    {
        uint32_t btn_press_ms = 0;
        uint8_t  btn_was_down = 0;
        uint8_t  btn_long_done = 0;
        Screen_t btn_press_screen = (Screen_t)0xFF;
        uint8_t  last_sel = 0xFF;
        uint32_t sensor_tick = 0;
        for (;;) {
        now = SystemTime_Millis();
        Watchdog_Kick();
        Stepper_Update();
        System_PollSave();
        MusicOta_Poll();   /* 音乐上传落盘推进 + ACK 补发 + 超时（含 MusicStore_Poll） */
        LangOta_Poll();    /* 字库上传落盘推进 + ACK 补发 + 超时: 不能只靠 EspLink 内部调用(状态卡住则写盘停) */
        MusicPlay_Poll();    /* 音乐播放音符推进（背景持续） */
        CAN_Cluster_Process();
        EspLink_Process();

        /* 编码器/按键输入由 Encoder_Process 统一处理，
         * 休眠时任意输入唤醒（内部刷新 g_sys.screen_off），
         * 正常处理旋转/单击/长按事件，每次输入都会刷新 g_last_input_ms。 */
        Encoder_Process();

        /* 熄屏判定：无输入超时（旋转/按键都会更新 g_last_input_ms）才熄屏。
         * 注意这里要用"当前新取的时间"而非循环顶部的 now：Encoder_Process 可能刚把
         * g_last_input_ms 刷新到比 now 更新的值，用 now-旧值会回绕成巨数→误熄屏（旋转即黑屏）。 */
        if (!g_sys.screen_off && g_sys.screen_off_timeout > 0U) {
            static const uint16_t off_secs[9] = {0, 1, 5, 10, 20, 30, 60, 120, 300};
            uint16_t to = (g_sys.screen_off_timeout < 9U) ? off_secs[g_sys.screen_off_timeout] : 0U;
            if (to > 0U && (SystemTime_Millis() - g_last_input_ms) > (uint32_t)to * 1000U) {
                TFT_SetBrightness(0);
                g_sys.screen_off = 1;
            }
        }
        {
            uint8_t old_sel = last_sel;
            if (g_sys.current_screen == SCREEN_MAIN && last_sel != 0xFF
                && g_sys.selected_item != last_sel) {
                UI_RefreshCard(old_sel);
                UI_RefreshCard(g_sys.selected_item);
            }
            last_sel = g_sys.selected_item;
        }



            /* 长按切换：主界面非湿度卡→菜单，菜单→主界面 */
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

    /* sensor safety/control task: 100ms window (faster temp refresh; SHT40 blocks ~16ms)
             * 仅真正的首启引导页(SCREEN_LANG_LOAD, 外设未初始化)跳过; 设置页弹窗等正常模式
             * 始终运行——lang_ap_active 残留(语言上传失败等)不得冻结传感器/加热 */
            if (g_sys.current_screen != SCREEN_LANG_LOAD &&
                (int32_t)(now - sensor_tick) >= (int32_t)100) {
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
#define TEMP_SAFETY_ANCHOR_CAP    35.0f     /* 鍒濆嬮敋鐐逛笂闄 */
#define TEMP_RISE_MIN             0.2f      /* 冷启动 1 分钟温升阈值(℃) */
#define TEMP_RISE_WINDOW_MS       60000U    /* 冷启动检测窗口(1min) */
#define TEMP_RISE_HOT_MIN         1.0f      /* 热启动(初始>35℃) 2 分钟温升阈值(℃) */
#define TEMP_RISE_HOT_WINDOW_MS   120000U   /* 热启动检测窗口(2min) */
#define TEMP_DROP_DEBOUNCE        5         /* 低于锚点(裕度外)连续 N 次(≈500ms)才报警，抗抖动 */
#define TEMP_DROP_MARGIN          3.0f      /* 冷启动：跌破初始锚点 3℃ 判作箱体破损（ >3℃ 触发） */
#define TEMP_HOT_DROP_DELTA       3.0f      /* 鐑鎬侊細宄板煎洖钀借秴杩 3鈩 鍒ょ变綋鐮存崯 */

static void read_sensors(void)
{
    float temp, hum, weight;
    int16_t ptc_raw;

    /* SHT40 温湿度读取失败时沿用上次温度，连续失败若干次后进入安全告警；
     * 偶发一次失败属正常（如"传感器忙 25°C 系列"），连续多次才判定传感器故障 */
    {
        static uint8_t sht_fail = 0;
        int ar = SHT40_Read(&temp, &hum);
        if (ar == -2) {
            /* 等待转换完成（触发后/未到 80ms）：属正常节拍，不计失败 */
        } else if (ar != 0) {
            /* 连续失败(约2s)才报安全：SHT40 偶发忙读取失败属正常 */
            if (++sht_fail >= 20) {
                sht_fail = 0;
                if (g_sys.drying_active && g_sys.safety_state == SAFETY_NONE) {
                    trigger_safety(SAFETY_BOX_BROKEN);
                }
            }
        } else {
            sht_fail = 0;
            g_sys.current_temp = temp;
            g_sys.current_humidity = hum;
        }
    }

    weight = CS1237_ReadWeight();
    if (weight > -1000.0f) {   /* -9999=转换未就绪，整帧跳过（显示保持上一有效值） */
        g_sys.weight_g = (int32_t)(weight + ((weight >= 0.0f) ? 0.5f : -0.5f));

/* 自动去皮仅在开机建立窗口（6~25s、空闲状态）执行：
 * 烘干中电桥温漂会缓慢越过阈值，若不限时间段，烘干到一半会
 * 把盘内真实物料（如 240g）连同漂移一起"清零"。 */
        {
            static uint8_t neg_cnt = 0, huge_cnt = 0;
            static uint32_t last_auto_tare = 0;
            uint32_t t_ms = SystemTime_Millis();
            int do_tare = 0;

            if (t_ms > 6000U && t_ms < 25000U && !g_sys.drying_active &&
                (int32_t)(t_ms - last_auto_tare) >= 5000) {
                if (weight < -20.0f) { if (++neg_cnt >= 3) do_tare = 1; }
                else neg_cnt = 0;
                if (weight > 6000.0f) { if (++huge_cnt >= 3) do_tare = 1; }
                else huge_cnt = 0;
            }
            if (do_tare) {
                neg_cnt = 0; huge_cnt = 0;
                last_auto_tare = t_ms;
                CS1237_Tare();
                g_sys.weight_g = 0;
            }
        }
    }
    /* 无效读数(-9999=转换未就绪)丢弃：保持上一显示值，不触发去皮 */

    ptc_raw = NTC_GetTemperature();
    if (ptc_raw >= NTC_READ_LO_10C && ptc_raw <= NTC_READ_HI_10C)
        g_sys.ptc_temp = (float)ptc_raw / 10.0f;  /* 测量域外(含开/短路哨兵)不进状态(保持上一有效) */

    /* 称重温度分段补偿：电桥温漂使重量随温度漂移。以 25℃ 为基准，
     * 按当前 NTC(烘干舱/机体)温度查分段系数对重量修正。
     * 系数需实测标定：每段 = 每℃ 的重量修正比例（可正可负）。
     * 例：0℃~15℃ 系数 -0.0004 → 10℃ 时修正 weight*(1-0.0004*(25-10))=0.994倍 */
    {
        float tc;
        float t = g_sys.ptc_temp;
        if (t < 15.0f)       tc = -0.0004f;   /* 浣庢俯娈 */
        else if (t < 35.0f)  tc = 0.0f;       /* 常温段（基准） */
        else                 tc = 0.0004f;    /* 楂樻俯娈 */
        if (g_sys.weight_g != 0) {
            float comp = (float)g_sys.weight_g * (1.0f + tc * (t - 25.0f));
            g_sys.weight_g = (int32_t)(comp + ((comp >= 0.0f) ? 0.5f : -0.5f));
        }
    }

    /* NTC 失效检查（与 SHT40 连续失败保护同构）：开路→NTC_ERR_OPEN、短路→NTC_ERR_SHORT
     * 均为测量域外哨兵，与真实温度(含 120~160℃)永不混叠。
     * 两级反应: ① g_sys.ntc_valid=0 → control_update 当拍即断加热+风扇满速
     *            (不再依赖冻结的 ptc_temp 缓存);
     *          ② 烘干中持续约 2s → trigger_safety 升级成正式安全故障。 */
    {
        static uint8_t ntc_fail = 0;
        g_sys.ntc_valid = (ptc_raw >= NTC_READ_LO_10C && ptc_raw <= NTC_READ_HI_10C) ? 1U : 0U;
        if (!g_sys.ntc_valid) {
            if (g_sys.drying_active && ++ntc_fail >= 20) {
                ntc_fail = 0;
                trigger_safety(SAFETY_BOX_BROKEN);  /* 传感器故障无法可信控温, 走加热失效安全停机 */
            }
        } else {
            ntc_fail = 0;
        }
    }
}

static void update_rgb(void)
{
/* 上电开屏前保持灯条熄灭：背光渐亮（s_rgb_ready=1）后才开始走灯效，
 * 避免"屏幕还黑着灯条就先亮起"像突然通电一样。 */
    if (!s_rgb_ready) return;    /* 背光渐亮前灯条保持熄灭 */
    /* WS2812 位带码发送全程关中断(1颗~120us, 7~8颗~350-410us): 115200bps 下行字节
     * 每 87us 一个, 音乐/字库上传的字节流必然随机撞进窗口 → 两字节撞同一 RXNE
     * 产生 ORE 丢 1 字节。丢在帧体里 FSM 只会干等(所以历史表现为"零 NAK、盲重发、
     * 卡11%/14%/有概率失败"——正是这个签名)。会话期间冻结灯效(灯停在最后一帧),
     * 上传结束/失败即恢复; 溢出中止仍保留作兜底。
     * 条件含弹窗 2/3(热点等待/上传中): 握手 0x11 字节到达时会话尚未激活,
     * 只判 Active 会漏掉握手瞬间——那时灯效若正在跑同样会吃掉握手帧。 */
    if (MusicOta_Active() || LangOta_Active() ||
        g_sys.music_popup == 2 || g_sys.music_popup == 3 ||
        g_sys.lang_popup == 2 || g_sys.lang_popup == 3) return;
    if (!g_sys.light_switch) {
        RGB_AllOff();
        return;
    }
    /* 音乐播放中: 按音高点亮灯带(中间起步, 越往两侧音越高), 覆盖其他灯效 */
    if (MusicPlay_IsPlaying()) {
        RGB_MusicPitch(MusicPlay_CurFreq());
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
        /* 烘干完成度：进度满 14% 点亮一颗进度灯珠，随进度逐颗点亮 */
        uint8_t pct = 0;
        if (g_sys.params.dry_time_sec > 0)
            pct = (uint8_t)(100U - g_sys.remaining_sec * 100U / g_sys.params.dry_time_sec);
        if (pct > 100) pct = 100;
        RGB_Progress_DryingBar(pct);
        complete_start = 0;
    } else if (g_sys.run_state == STATE_COOLING || g_sys.run_state == STATE_COMPLETE) {
        /* 烘干计时结束（进入冷却）即视为完成亮绿，冷却到位后保持 */
        if (complete_start == 0) complete_start = SystemTime_Millis();
        if (SystemTime_Millis() - complete_start < 30000) {
            RGB_Status_Green();
        } else {
            RGB_Status_Off();
        }
        RGB_Progress_Rainbow();
    } else {
        /* 空闲：PB6 灯珠当呼吸灯显示（亮灭循环），PB7 彩色流水效果 */
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

/* 停止保护窗: 刚 Stop/Pause/安全触发后的 1.5s 内拒绝任何 start
 * (吞掉重复按键事件/网页重复 RUN 指令/CAN 重发, 防止"关烘干→又变加热") */
static uint32_t s_stop_guard_ms = 0;
static void stop_guard_arm(void) { s_stop_guard_ms = SystemTime_Millis(); }

void StartDrying(void)
{
    /* 安全告警未确认清除时禁止启动烘干, 杜绝"关闭反而加热"的 toggle 反转 */
    if (g_sys.safety_state != SAFETY_NONE) return;
    if (s_stop_guard_ms != 0U &&
        (uint32_t)(SystemTime_Millis() - s_stop_guard_ms) < 1500U) return;
    s_stop_guard_ms = 0;
    pid_reset();
    g_sys.drying_active = 1;
    g_sys.run_state = STATE_HEATING;
    g_sys.remaining_sec = g_sys.params.dry_time_sec;
    g_sys.temp_stuck_start = SystemTime_Millis();
    g_sys.safety_state = SAFETY_NONE;
    g_sys.chamber_temp_last = g_sys.current_temp;
    PTC_SetPower(100);
    Fan_SetSpeed(100);                       /* 风扇满速：辅助散热带出热量 */
    if (g_sys.params.motor_enabled) {
        Stepper_Enable(1);
        Stepper_SetSpeed(g_sys.params.motor_speed * 200);
        if (g_sys.params.motor_oscillate) Stepper_SetOscillate((uint32_t)g_sys.params.motor_oscillate_angle * SWING_BASE_STEPS_PER_DEG * (uint32_t)g_sys.params.motor_swing_cal / 10000U);
        else Stepper_Move(g_sys.params.motor_direction ? -100000 : 100000);
    }
}

void StopDrying(void)
{
    pid_reset();
    stop_guard_arm();
    g_sys.drying_active = 0;
    g_sys.run_state = STATE_COOLING;
    g_sys.safety_state = SAFETY_NONE;        /* 手动停止视为用户主动取消, 清除残留安全状态 */
    g_sys.remaining_sec = g_sys.params.dry_time_sec;   /* 停止后剩余时长归整为设定时长，避免网页/界面残留倒计时 */
    PTC_SetPower(0);
    Fan_SetSpeed(100);
    Stepper_Enable(0);
}

void PauseDrying(void)
{
    if (g_sys.run_state != STATE_HEATING && g_sys.run_state != STATE_DRYING) return;
    pid_reset();
    stop_guard_arm();
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
    g_sys.run_state = (g_sys.current_temp < (float)g_sys.params.target_temp - 0.5f)
                      ? STATE_HEATING : STATE_DRYING;
    Fan_SetSpeed(100);
    if (g_sys.params.motor_enabled) {
        Stepper_Enable(1);
        Stepper_SetSpeed(g_sys.params.motor_speed * 200);
        if (g_sys.params.motor_oscillate) Stepper_SetOscillate((uint32_t)g_sys.params.motor_oscillate_angle * SWING_BASE_STEPS_PER_DEG * (uint32_t)g_sys.params.motor_swing_cal / 10000U);
        else Stepper_Move(g_sys.params.motor_direction ? -100000 : 100000);
    }
}

/* 箱体温升安全监测（对比 SHT40 空气温度 current_temp）：
 * 锚定初始点 anchor = min(初始温度, 35℃)。
 * 冷启动(初始≤35)：监测窗口 1 分钟，须温升 ≥0.2℃ 才判加热正常；
 * 热启动(初始>35)：说明箱内余温继续烘干，只需监测温升，2 分钟内须温升 ≥1.0℃。
 * 温升监测值取最大值锁存（只高不低），滤除 SHT40 0.1~0.2℃ 的读数回跳。
 * 冷启动全程：温度跌破初始锚点（连拍防抖）→ 判故障（加热失效/传感器异常）。 */
static uint8_t  rise_mon_active = 0;   /* 本次烘干周期监测是否已初始化 */
static float    rise_initial = 0.0f;   /* 初始温度（实际读数，作温升基准） */
static float    rise_anchor  = 0.0f;   /* 锚点 = min(初始温度, 35) */
static float    rise_peak    = 0.0f;   /* 最大值锁存：只高不低 */
static uint32_t rise_start_ms = 0;
static uint8_t  rise_hot = 0;          /* 初始温度 > 35℃ 热启动 */
static uint8_t  rise_confirmed = 0;    /* 窗口内温升达标，确认加热有效 */
static uint8_t  rise_drop_cnt = 0;

static void rise_monitor_start(void)
{
    rise_mon_active = 1;
    rise_initial = g_sys.current_temp;
    rise_anchor  = (g_sys.current_temp < 35.0f) ? g_sys.current_temp : 35.0f;
    rise_peak    = g_sys.current_temp;
    rise_start_ms  = SystemTime_Millis();
    rise_hot       = (g_sys.current_temp > 35.0f) ? 1 : 0;
    rise_confirmed = 0;
    /* 已处于目标附近(<3℃差距)：温升门槛失去意义（空气会在设定点附近缓慢逼近，
     * 摆动 ±0.3℃），此时按"第1次检测已通过"处理，仅保留回落保护，避免最后 1℃
     * 维持过慢被误判"长时间不上升→安全警告"。 */
    if (g_sys.current_temp + 3.0f >= (float)g_sys.params.target_temp) {
        rise_confirmed = 1;
    }
    rise_drop_cnt  = 0;
}

static void safety_check(void)
{
    uint32_t now = SystemTime_Millis();
    if (g_sys.safety_state != SAFETY_NONE || !g_sys.drying_active) { rise_mon_active = 0; return; }
    if (g_sys.run_state != STATE_HEATING && g_sys.run_state != STATE_DRYING) { rise_mon_active = 0; return; }

    /* 首次进入本周期：锚定初始点 */
    if (!rise_mon_active) { rise_monitor_start(); return; }

    /* 检测值最大值锁存（只高不低），滤传感器回跳 */
    if (g_sys.current_temp > rise_peak) rise_peak = g_sys.current_temp;

    if (rise_hot) {
        /* 热启动：只检测温升，窗口 2 分钟须温升 ≥1.0℃ */
        if (!rise_confirmed && (int32_t)(now - rise_start_ms) >= (int32_t)TEMP_RISE_HOT_WINDOW_MS) {
            if (rise_peak - rise_initial < TEMP_RISE_HOT_MIN) { trigger_safety(SAFETY_LID_OPEN); return; }
            rise_confirmed = 1;
        }
        /* 热态跌落保护：峰值回落 >3℃（用户要求）→ 箱体破损/开盖。
         * 峰值起始=初始温度，需先上升后回落，天然不会在启动阶段误触发。 */
        if (rise_peak - g_sys.current_temp >= TEMP_HOT_DROP_DELTA) {
            if (++rise_drop_cnt >= TEMP_DROP_DEBOUNCE) { trigger_safety(SAFETY_BOX_BROKEN); return; }
        } else {
            rise_drop_cnt = 0;
        }
    } else {
        /* 冷启动：窗口 1 分钟须温升 ≥0.2℃（第1次检测） */
        if (!rise_confirmed && (int32_t)(now - rise_start_ms) >= (int32_t)TEMP_RISE_WINDOW_MS) {
            if (rise_peak - rise_initial < TEMP_RISE_MIN) { trigger_safety(SAFETY_LID_OPEN); return; }
            rise_confirmed = 1;   /* 第1次检测通过：加热有效 */
        }
        /* 全程回落检测：显著跌破初始锚点（1℃裕度外，连拍防抖）→ 判作箱体破损。
         * 仅加热已确认(rise_confirmed)后启用：启动阶段风机冷吹+残余热使读数先跌后升，
         * 若未确认就判跌落会"刚烘干就箱体破损"；确认后的真实大跌落(开盖/漏热)仍能抓住。 */
        if (rise_confirmed && g_sys.current_temp < rise_anchor - TEMP_DROP_MARGIN) {
            if (++rise_drop_cnt >= TEMP_DROP_DEBOUNCE) { trigger_safety(SAFETY_BOX_BROKEN); return; }
        } else {
            rise_drop_cnt = 0;
        }
    }
}

/* PID 自整定优先：空气 PID 为主控（随 SHT40 空气温度）保护元件温度，NTC PID 为保护环 */
static float pid_air_int = 0.0f, pid_air_prev = 0.0f;
static uint32_t pid_air_tick = 0;
static float pid_ntc_int = 0.0f, pid_ntc_prev = 0.0f;
static uint32_t pid_ntc_tick = 0;

static void pid_reset(void)
{
    pid_air_int = 0.0f; pid_air_prev = 0.0f; pid_air_tick = 0U;
    pid_ntc_int = 0.0f; pid_ntc_prev = 0.0f; pid_ntc_tick = 0U;
}

/* 通用 PID 步进：输出 0-100 百分比（setpoint=目标，measure=被测温）。
 * 反积分饱和(back-calculation)：输出到顶/底时把超出量立即从积分卸掉——
 * 否则升温段积分积到 +50，元件到达 ptc_max 后仍被残余积分推着多烧（实测冲到 95℃），
 * 并在限制附近 0→25% 反复横跳。卸积分后接近限值功率平滑归零。
 * D 项按"测量值微分"而非"误差微分"：恒温稳定带 air_sp 每跨 0.5℃ 会把误差阶跃 ±0.5,
 * 误差微分把它放大成一次性方向相反的功率踢击(kd×0.5/dt), 表现为"已到目标仍偶发加热"
 * (有概率、温度在 ±0.5℃ 带上被噪声反复跨界)。测量微分只随真实温升/温降作用,
 * 设定切换不产生踢击; 常规工况(设定恒定)两者等价。prev 现存上一拍测量值。 */
static uint8_t pid_step(float *integral, float *prev, uint32_t *tick,
                        uint32_t now, float setpoint, float measure,
                        float kp, float ki, float kd)
{
    uint8_t first = (*tick == 0U);
    float dt = first ? 0.2f : (float)(now - *tick) / 1000.0f;
    float err, der, dterm, out;
    if (dt <= 0.0f || dt > 1.0f) dt = 0.2f;
    /* 测量一阶低通(MF_K=0.4)后再取微分: 传感器 0.01~0.1 量化的相邻拍抖动
     * (0.1℃/0.2s=0.5℃/s)乘上校准出的大 kd 会放大成百 % 的随机 D 尖峰,
     * 表现为"已到目标仍有概率满功率加热/未到仍有概率停热"的反向跳变。
     * prev 现存"滤波后测量值"(首拍直接播种)。 */
    {
        float mf = first ? measure : (*prev) + 0.40f * (measure - *prev);
        err = setpoint - measure;
        der = first ? 0.0f : ((*prev) - mf) / dt;
        *prev = mf;
    }
    *tick = now;
    dterm = kd * der;
    if (dterm >  25.0f) dterm =  25.0f;   /* D 贡献限幅(功率%), 只保留制动/预减作用 */
    if (dterm < -25.0f) dterm = -25.0f;

    *integral += err * dt;
    if (*integral > 50.0f) *integral = 50.0f;
    if (*integral < -50.0f) *integral = -50.0f;
    out = kp * err + ki * (*integral) + dterm;
    if (ki > 0.0001f) {
        if (out > 100.0f) { *integral -= (out - 100.0f) / ki; out = 100.0f; }
        else if (out < 0.0f) { *integral -= out / ki; out = 0.0f; }
        if (*integral > 50.0f) *integral = 50.0f;
        if (*integral < -50.0f) *integral = -50.0f;
    } else {
        if (out > 100.0f) out = 100.0f;
        if (out < 0.0f)   out = 0.0f;
    }
    return (uint8_t)out;
}

static void control_update(void)
{
    float target = (float)g_sys.params.target_temp;      /* 烘干目标温度（空气） */
    float ntc_max = (float)g_sys.params.ptc_max_temp;    /* PTC 元件上限温度（NTC 保护） */
    static uint32_t last_tick = 0;
    uint32_t now = SystemTime_Millis();

    /* PID 自動校準優先: 校準是受控元件加熱測試(內部有 160°C 硬件保護), 不被
     * safety/NTC 異常誤禁——否則 NTC 異常或 safety 殘留時校准永遠不發熱 */
    if (g_sys.pid_autotune_running) {
        PTC_PID_AutotuneProcess();
        g_sys.pid_autotune_progress = PTC_PID_AutotuneGetProgress();
        if (PTC_PID_AutotuneIsDone()) {
            g_sys.pid_autotune_running = 0;
            /* 校准完成自动退回上级菜单 */
            if (g_sys.current_screen == SCREEN_PID_AUTOTUNE) {
                g_sys.current_screen = SCREEN_PTC_ADJUST;
                g_sys.ui_force_redraw = 1;
            }
        }
        return;
    }
    if (g_sys.temp_pid_running) {
        PTC_TempPID_AutotuneProcess();
        g_sys.temp_pid_progress = PTC_TempPID_AutotuneGetProgress();
        if (PTC_TempPID_AutotuneIsDone()) {
            g_sys.temp_pid_running = 0;
            /* 校准完成自动退回上级菜单 */
            if (g_sys.current_screen == SCREEN_TEMP_PID) {
                g_sys.current_screen = SCREEN_TEMP_ADJUST;
                g_sys.ui_force_redraw = 1;
            }
        }
        return;
    }

    if (g_sys.safety_state != SAFETY_NONE) { PTC_SetPower(0); return; }

    /* item1(P0): NTC 当前拍异常 → 本拍立即断加热+风扇满速, 不等 2s。
     * g_sys.ptc_temp 在域外时是"冻结的最后有效值"(如 70℃), 若安全链只信缓存,
     * 存在"显示 70℃、实际失控升温"的时间窗。read_sensors 每拍先于本函数执行,
     * ntc_valid 即当前拍判定; "持续 2s" 只负责升级为正式安全告警(read_sensors 计数)。 */
    if (!g_sys.ntc_valid && g_sys.drying_active) {
        PTC_SetPower(0);
        Fan_SetSpeed(100);
        return;
    }

    /* 绝对过热闸(兜底): 元件温度 ≥ 硬件上限 160℃ 无条件断功率。
     * item2: 过了上方 ntc_valid 闸后, ptc_temp 必为本拍域内真实读数
     * (域外不覆盖缓存 + 域外已被拦截) → 缓存值 ≡ 当前 NTC, 无需二次 ADC。
     * 覆盖正常烘干所有分支——即使 PID 失序导致 ptc_max+10 软顶没生效也不烧穿。
     * 统一 >= 语义: 159.9 正常 / 160.0 起断(P0)。自动校准有独立闸(自读直判),不拦截。
     * 必须 return: 否则落入下方 switch 会重新 PTC_SetPower(pwr) 把加热打开。 */
    if (g_sys.ptc_temp >= (float)PTC_TEMP_MAX) {
        PTC_SetPower(0);
        return;
    }

    switch (g_sys.run_state) {
    case STATE_HEATING:
    case STATE_DRYING: {
        /* 烘干阶段：风机满速运转，加速温度稳定。PTC/NTC 温度均参与保护 */
        Fan_SetSpeed(100);
        /* 烘干：空气 PID（随 SHT40 空气温度）控制功率，误差大时满速 100% 快速升温；
         * NTC PID（元件温度保护）当元件温度接近 ptc_max_temp 时收功率，防止 PTC 过温；
         * PTC 取两者较小值：即使空气未达标，元件温度高时也只按保护限功率工作 */
        /* 稳定带修正(用户要求: 允许低于设定值, 在设定下方 0~1℃ 区间工作,
         * 消除"接近设定就稍微导通"): 温度 ≥ target-0.3 就把目标压到 target-1.0
         * → 功率立即归零, 风扇自然降温; 掉到 target-1.0 以下再恢复向上限加热。
         * 两档相隔 0.7℃ 不 chatter; 上沿 -0.3 仍高于 HEATING→DRYING 的
         * (target-0.5) 判定, 预热转恒温不受影响。 */
        float air_sp = target;
        if (g_sys.current_temp >= target - 0.3f) air_sp = target - 1.0f;
        uint8_t p_air = pid_step(&pid_air_int, &pid_air_prev, &pid_air_tick,
                                 now, air_sp, g_sys.current_temp,
                                 g_sys.params.pid_air_kp, g_sys.params.pid_air_ki, g_sys.params.pid_air_kd);
        uint8_t p_ntc = pid_step(&pid_ntc_int, &pid_ntc_prev, &pid_ntc_tick,
                                 now, ntc_max, g_sys.ptc_temp,
                                 g_sys.params.pid_ntc_kp, g_sys.params.pid_ntc_ki, g_sys.params.pid_ntc_kd);
        {
            uint8_t pwr = (p_air < p_ntc) ? p_air : p_ntc;
            /* 元件硬顶保护：超 ptc_max+10℃ 直接断功率，不依赖 PID 收敛
             * （抗积分饱和后的残余热惯性滑行兜底，防止"快到空气目标却烧到95℃+"） */
            if (g_sys.ptc_temp > (float)g_sys.params.ptc_max_temp + 10.0f) pwr = 0U;
            PTC_SetPower(pwr);
        }

        if (g_sys.run_state == STATE_HEATING && g_sys.current_temp >= target - 0.5f) {
            g_sys.run_state = STATE_DRYING;   /* 空气达到目标，切换恒温烘干阶段 */
            last_tick = now;
        }
        {
            /* 临时改动(2026-09-17, 用户要求): HEATING/DRYING 两阶段都直接走倒计时——
             * 温湿度传感器摆放有偏差、可能长时间达不到目标温度, 原逻辑只在到温后
             * 才计时, 会无限烘干。摆放位置修正后如需"到温才计时",
             * 把本块条件恢复为 if (g_sys.run_state == STATE_DRYING)。 */
            /* 倒计时：固定步进 +1000ms 并结转余量。
             * 原逻辑 last_tick = now 每秒丢 0~(tick周期) 的不足一秒余数，
             * 长时间运行倒计时明显偏慢；>5s 台阶（暂停等）才重新锚定，不连扣补偿。 */
            if (last_tick == 0) last_tick = now;
            if ((int32_t)(now - last_tick) >= (int32_t)1000) {
                if ((int32_t)(now - last_tick) > 5000) {
                    last_tick = now;
                } else {
                    last_tick += 1000U;
                    if (g_sys.remaining_sec > 0) {
                        g_sys.remaining_sec--;
                    } else {
                        StopDrying();
                    }
                }
            }
        }
        break;
    }
    case STATE_COOLING:
        PTC_SetPower(0);              /* 冷却态强制关加热(防残留功率) */
        Fan_SetSpeed(100);
        if (g_sys.ptc_temp <= (float)g_sys.params.ptc_cooling_temp) { g_sys.run_state = STATE_COMPLETE; Fan_Off(); }
        break;
    case STATE_PAUSED:
        PTC_SetPower(0);
        break;
    default:
        /* 空闲/完成态：维持冷却温度 —— 只检测 NTC 超限温度 */
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