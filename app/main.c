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

#define SWING_BASE_STEPS_PER_DEG  4545U  /* base steps/deg*100, then x motor_swing_cal/100; 塨?180deg@300%△60deg, 45.45˙/ */
#define SWING_CAL_DEFAULT         100   /* swing cal default %; adjust via UI to match real swing */

SystemState_t g_sys;

#ifndef BOOTLOADER_BUILD
static void refresh_api_data(void);
static void read_sensors(void);
static uint8_t s_rgb_ready = 0;   /* 开屏渐亮后才允许 RGB 灯效（上电熄灯待命） */
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

    /* ?审塵橾PB0噶?竟?塚﹍て?? */
    {
        GPIO_InitTypeDef g;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        g.GPIO_Pin = PIN_TFT_BL_PIN;
        g.GPIO_Mode = GPIO_Mode_Out_PP;
        g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(PIN_TFT_BL_PORT, &g);
        GPIO_SetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
    }

    /* g_sys ??濆?嬪?栵?圲I ???瑕?锛? */
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
    g_sys.params.motor_oscillate_angle = 60;  /* 骞冲彴????姩瑙掑害(瀹為??搴︽暟) 1-360 */
    g_sys.params.motor_driver = MOTOR_DRIVER_A4988;
    g_sys.params.motor_current = 2;
    g_sys.params.motor_stealthchop = 0;
    g_sys.params.motor_work_count = 0;      /* 0=work forever, N=rest every N cycles */
    g_sys.params.motor_rest_sec = 0;        /* 0=涓嶄?戞???宸ヤ?滄?暟?????嶇?????锛? */
    g_sys.params.motor_swing_cal = SWING_CAL_DEFAULT;
    g_sys.params.rgb_enabled = 1;
    g_sys.params.rgb_led_bright = 100;
    g_sys.params.rgb_strip_bright = 100;

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
    }            /* RGB??潯榛樿寮? */

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

    /* 涓嶇敤 Board_Init锛??惈 NTC_Init ???ADC ?牎??? while锛屾湭?帴浼犳?熷櫒?彲??藉崱姝伙?夈??
     * ?彧??氬?夊?ㄥ?曡?氬?濆?嬪?栵????犵?櫒??虫??+ SWJ ??嶆?犲??锛夈??*/
    Board_EarlyInit();
    Watchdog_Init();   /* App ?嚜?惎??嬮棬??楋?屼?嶄?濊??Bootloader */

    /* ??抽敭锛氫?? RCC 璇诲?炲?為???椂???锛岀?犳??SystemCoreClock锛?涓? bootloader 涓??嚧锛夈??
     * ?惁??? SysTick ?懆??熼?欙?孲ystemTime_Millis 涓嶈蛋???*/
    SystemCoreClockUpdate();
    SystemTime_Init(); /* ?惎?姩 SysTick锛屼?? SystemTime_Millis/Encoder 璁℃椂 */

    /* Bootloader ???BootloaderV2_JumpToApp() 璺宠浆??嶈???敤浜? __disable_irq()锛?
     * ???App ???SystemInit/main 浠庝?嶉?嶆柊寮?涓柇 ???PRIMASK 淇濇?? 1 ???SysTick
     * 姘镐?嶈Е??? ???SystemTime_Millis ??荤?? ???缂栫???櫒??曞???暱??夎?椂??ㄩ?ㄥけ???
     * 锛???嬭浆浠嶅彲?敤锛???犲?跺彧杞 GPIO 涓嶄?濊?栦腑?柇锛夈??姝ゅ??蹇?椤婚?嶆柊寮?涓柇???*/
    __enable_irq();

    /* 涓婄數?暱??夌?栫?????绾?s) ???寮哄?惰?涘?? Bootloader 涓嬭浇妯″?忋??
     * ?洿?帴杞??夐挳寮曡?氾?屼?嶄?濊?? Encoder_Process()锛?瀹??????ㄤ?氭??璐逛?嬩欢锛?
     * 瀵艰嚧杩欓????嶈?? Encoder_GetEvent() 姘歌?滄嬁涓嶅??LONG_PRESS锛夈??*/
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
            OTA_EnterBootloader();   /* ???FORCE_BOOT ???蹇楀苟澶嶄?嶏?屼?嶄?氳?斿?? */
        }
    }

    /* 灞忓?曞?濆?嬪??+ 涓荤?岄??*/
    TFT_Init();
    Buzzer_Init();
    Backlight_Init();

    g_sys.buzzer_link = 1;
    g_sys.buzzer_vol = 5;
    g_sys.light_switch = 1;
    g_sys.backlight = 100;
    g_sys.theme = 1;  /* 榛樿??楄壊涓婚?? #1E1E2E */
    g_sys.screen_off_timeout = 0;  /* 榛樿浠庝?嶇??灞? */
    g_sys.wifi_enabled = 1;
    g_sys.pid_calibrated = 0;
    System_Init();   /* 浠庡?栭?╢lash??犺浇宸蹭?濆?樺???暟锛?瑕???栭?樿??硷?夛??澶辫触??欑敤榛樿???*/
    TFT_SetBrightness(g_sys.backlight);
    theme_apply();

    /* AHT20 娓╂箍搴︿?犳?熷櫒??濆?嬪?栵??杞欢I2C锛孭B10=SCL / PB11=SDA锛夈??
     * ??嶈??3娆″?斿涓婄數?椂搴忥?涘?濆?嬪?栧け璐ヤ?嶉樆濉炲惎?姩锛岀?岄潰浠嶆樉绀洪?樿??硷??
     * ?懆??????read_sensors() ???缁х画灏濊?曡??栥??*/
    {
        int aht_try;
        for (aht_try = 0; aht_try < 3; aht_try++) {
            if (AHT20_Init() == 0) break;
            { volatile uint32_t d = 0; while (d < 100000U) d++; }
        }
    }

    /* PTC/Fan/NTC 初始化（硬件已接好对应外设） */
    
    PTC_Init();
    Fan_Init();
    NTC_Init();
    CS1237_Init();
    { volatile uint32_t d = 0; while (d < 3000000U) d++; }  /* settle ~200ms for stable boot tare */                 /* ??嬪?涗?犳?熷??CS1237锛圥A0=DOUT锛孭A1=SCLK锛?*/
    RGB_Strip_Init();              /* WS2812 RGB ??潯锛圥B6=?姸??????孭B7=7棰楄?涘害????*/
    Stepper_Init();                /* 姝ヨ?涚數?満 GPIO锛圥B12-14锛夊?濆?嬪?栵?岄?樿浣胯?借?氶?樼數骞????*/

    UI_ShowBootScreen();   /* boot progress screen + sensor read */
    UI_DrawMainScreen();        /* ?????夋?楁椂缁樺?朵富??岄??*/
    /* ?????夋?愬?樹寒璧凤?岄湶?嚭涓荤?岄潰锛?杩?娓″姩?敾锛? */
    s_rgb_ready = 1;               /* 与屏幕渐亮同步：此刻起灯条才开启动效 */
    {
        uint16_t b;
        for (b = 0U; b <= 100U; b += 5U) {
            TFT_SetBrightness(b);
            Watchdog_Kick();
            { volatile uint32_t d = 0; while (d < 200000U) d++; }  /* 绾?14ms */
        }
        TFT_SetBrightness(100);
    }

    /* 缂栫???櫒锛欵ncoder_Process ?????ㄥ???????嬭浆/??曞嚮锛岄暱??夎?涜?滃?曠敱涓嬫柟?嫭绔嬫??娴嬨??
     * ??嬭浆??庢暣灞忛?嶇?樹富??岄潰锛???犵櫧妗?娈嬪奖锛岄?変腑妗???忓崱???楂樹寒锛夈??*/
    {
        uint32_t btn_press_ms = 0;
        uint8_t  btn_was_down = 0;
        uint8_t  btn_long_done = 0;
        Screen_t btn_press_screen = (Screen_t)0xFF;
        uint8_t  last_sel = 0xFF;
        uint32_t last_activity = SystemTime_Millis();  /* ???灞忚?椂 */
    uint32_t sensor_tick = 0;              /* sensor/control loop: 200ms period */
        uint8_t  screen_off = 0;
        for (;;) {
            now = SystemTime_Millis();
            Watchdog_Kick();
            Stepper_Update();   /* 姝ヨ?涚數?満??夊?茬????愶?屾?忚疆寰幆璋??敤锛??????ㄦ??step_period_us 璁℃椂 */
            System_PollSave();  /* ??庡彴淇濆?樺???暟锛氭?忚疆浠?涓?娆＄?? SPI 浜嬪姟锛屼?嶉樆濉炲?锋柊 */

            /* 息屏后编码器输入不产生任何动作（见 Encoder_Process），屏幕不唤醒 */
            if (!screen_off && g_sys.screen_off_timeout > 0U) {
                static const uint16_t off_secs[9] = {0, 1, 5, 10, 20, 30, 60, 120, 300};
                uint16_t to = (g_sys.screen_off_timeout < 9U) ? off_secs[g_sys.screen_off_timeout] : 0U;
                if (to > 0U && (now - last_activity) > (uint32_t)to * 1000U) {
                    TFT_SetBrightness(0);
                    screen_off = 1;
                    g_sys.screen_off = 1;
                }
            }
            {
                uint8_t old_sel = last_sel;
                Encoder_Process();   /* ??嬭浆/??曞嚮/?暱??夌敱?????ㄧ姸????満澶???? */
                if (g_sys.current_screen == SCREEN_MAIN && last_sel != 0xFF
                    && g_sys.selected_item != last_sel) {
                    UI_RefreshCard(old_sel);
                    UI_RefreshCard(g_sys.selected_item);
                }
                last_sel = g_sys.selected_item;
            }

            /* ?嫭绔嬮暱??夋??娴嬶?氫富??岄????滃?曚?嬮棿????崲锛?婀垮害?崱涓婇暱??夌敱缂栫???櫒澶??????樺共锛屼?嶈?涜?滃?曪??*/
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

    /* sensor safety/control task: 100ms window (faster temp refresh; AHT20 blocks ~16ms) */
            if ((int32_t)(now - sensor_tick) >= (int32_t)100) {
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
#define TEMP_SAFETY_ANCHOR_CAP    35.0f     /* 初始锚点上限 */
#define TEMP_RISE_MIN             0.2f      /* 冷启动 1 分钟温升阈值(℃) */
#define TEMP_RISE_WINDOW_MS       60000U    /* 冷启动检测窗口(1min) */
#define TEMP_RISE_HOT_MIN         1.0f      /* 热启动(初始>35℃) 2 分钟温升阈值(℃) */
#define TEMP_RISE_HOT_WINDOW_MS   120000U   /* 热启动检测窗口(2min) */
#define TEMP_DROP_DEBOUNCE        5         /* 低于锚点(裕度外)连续 N 次(≈500ms)才报警，抗抖动 */
#define TEMP_DROP_MARGIN          1.0f      /* 初始锚点下方再留 1℃ 裕度：风机启动/传感器回跳 0.2℃ 不算箱体破损 */

static void __attribute__((unused)) refresh_api_data(void)
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

    /* AHT20 璇诲?栧け璐ワ?氫?濇??涓婃娓╁害锛屼???嫢姝??湪??樺共??欒Е??戝?夊?ㄤ?濇姢锛?
     * ??垮??"浼犳?熷櫒??? 25掳C ???姘歌?滆涓烘病??版????????缁?犵??"????嚧?懡?満???*/
    {
        static uint8_t aht_fail = 0;
        int ar = AHT20_Read(&temp, &hum);
        if (ar == -2) {
            /* 等待转换完成（触发后/未到 80ms）：属正常节拍，不计失败 */
        } else if (ar != 0) {
            /* 连续失败(约2s)才报安全：AHT20 偶发忙读取失败属正常 */
            if (++aht_fail >= 20) {
                aht_fail = 0;
                if (g_sys.drying_active && g_sys.safety_state == SAFETY_NONE) {
                    trigger_safety(SAFETY_BOX_BROKEN);
                }
            }
        } else {
            aht_fail = 0;
            g_sys.current_temp = temp;
            g_sys.current_humidity = hum;
        }
    }

    weight = CS1237_ReadWeight();
    if (weight > -1000.0f) {   /* -9999=转换未就绪，整帧跳过（显示保持上一有效值） */
        g_sys.weight_g = (int32_t)(weight + ((weight >= 0.0f) ? 0.5f : -0.5f));

        /* 自动重去皮仅允许在开机建稳窗口（6~25s、空闲状态）：
         * 烘干中电桥温漂会缓慢越过 ±阈值，若不限时段，烘到一半会把
         * 盘内真实物料（如 240g）连同漂移一起"清零"。 */
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
    g_sys.ptc_temp = (float)ptc_raw / 10.0f;

    /* 称重温度分段补偿：电桥温漂使重量随温度漂移。以 25℃ 为基准，
     * 按当前 NTC(烘焙腔/机体)温度查分段系数对重量修正。
     * 系数需实测标定：每段 = 每℃ 的重量修正比例（可正可负）。
     * 例：0℃~15℃ 系数 -0.0004 → 10℃ 时修正 weight*(1-0.0004*(25-10))=0.994倍 */
    {
        float tc;
        float t = g_sys.ptc_temp;
        if (t < 15.0f)       tc = -0.0004f;   /* 低温段 */
        else if (t < 35.0f)  tc = 0.0f;       /* 常温段（基准） */
        else                 tc = 0.0004f;    /* 高温段 */
        if (g_sys.weight_g != 0) {
            float comp = (float)g_sys.weight_g * (1.0f + tc * (t - 25.0f));
            g_sys.weight_g = (int32_t)(comp + ((comp >= 0.0f) ? 0.5f : -0.5f));
        }
    }

    /* NTC 寮?璺???矾寮?甯革????犲????版??绔?硷?変?熻Е??戝?夊?ㄤ?濇??
     * ??愭?嬭?曟??娉ㄩ????戞湭?帴瀹為????犵???锛????娴嬫櫠?椄绠¤?藉惁姝?甯告?撳??锛涙?嬭?曞?庢?㈠?嶏??
     * if (ptc_raw <= -100 || ptc_raw >= 2000) {
     *     if (g_sys.drying_active && g_sys.safety_state == SAFETY_NONE) {
     *         trigger_safety(SAFETY_BOX_BROKEN);
     *     }
     * }
     */
}

static void update_rgb(void)
{
    /* 上电开屏前保持灯条熄灭：背光渐亮（s_rgb_ready=1）后才开始走灯效，
     * 避免"屏幕还黑着灯条就僵硬亮起"。 */
    if (!s_rgb_ready) return;    /* 背光渐亮前灯条保持熄灭 */
    if (!g_sys.light_switch) {
        RGB_AllOff();
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
        /* 瀹屾?愬害锛氬?濆?嬪?ㄧ??岄?忓?掕?椂姣忓?屾?? 14% ??逛寒涓?棰楋?岀?撴?熷?ㄤ寒 */
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
        /* 绌洪棽锛歅B6 ???孭B7 褰╄櫣娴??姩 */
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
    Fan_SetSpeed(100);                       /* 椋庢????掑?ㄥ?熺?? */
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
        if (g_sys.params.motor_oscillate) Stepper_SetOscillate((uint32_t)g_sys.params.motor_oscillate_angle * SWING_BASE_STEPS_PER_DEG * (uint32_t)g_sys.params.motor_swing_cal / 10000U);
        else Stepper_Move(g_sys.params.motor_direction ? -100000 : 100000);
    }
}

/* 腔体温升安全监测（对比 AHT20 空气温度 current_temp）：
 * 锚定初始点 anchor = min(初始温度, 35℃)。
 * 冷启动(初始≤35)：检测窗口 1 分钟，须温升 ≥0.2℃ 才判加热正常；
 * 热启动(初始>35)：说明趁箱内余温继续烘干，只需检测温升，2 分钟内须温升 ≥1.0℃。
 * 温升监测值取最大值锁存（只高不低），滤除 AHT20 0.1~0.2℃ 的读数回跳。
 * 冷启动全程：温度跌破初始锚点（连帧防抖）→ 判故障（加热失效/传感器异常）。 */
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
    /* 已处于目标附近(<3℃差距)：温升门限失去意义（空气环会在设定点附近缓慢逼近、
     * 摆动 ±0.3℃），此时按"第1次检测已通过"处理，仅保留跌落保护，避免最后 1℃
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
    } else {
        /* 冷启动：窗口 1 分钟须温升 ≥0.2℃（第1次检测） */
        if (!rise_confirmed && (int32_t)(now - rise_start_ms) >= (int32_t)TEMP_RISE_WINDOW_MS) {
            if (rise_peak - rise_initial < TEMP_RISE_MIN) { trigger_safety(SAFETY_LID_OPEN); return; }
            rise_confirmed = 1;   /* 第1次检测通过：加热有效 */
        }
        /* 全程回落检测：显著跌破初始锚点（1℃裕度外，连帧防抖）→ 判箱体破损。
         * 仅加热已确认(rise_confirmed)后启用：启动阶段风机冷风吹+残余热使读数先跌后升，
         * 若未确认就判跌落会"刚烘干就箱体破损"；确认后的真实大跌落(开盖/漏热)仍能抓住。 */
        if (rise_confirmed && g_sys.current_temp < rise_anchor - TEMP_DROP_MARGIN) {
            if (++rise_drop_cnt >= TEMP_DROP_DEBOUNCE) { trigger_safety(SAFETY_BOX_BROKEN); return; }
        } else {
            rise_drop_cnt = 0;
        }
    }
}

/* ???PID锛氱┖姘旀俯搴?PID锛?涓绘帶锛???嶉?? AHT20锛? ??犵?櫒娓╁??PID锛?淇濇姢锛???嶉?? NTC锛?*/
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
 * 反积分饱和(back-calculation)：输出到顶/底时把超出量即时从积分卸掉——
 * 否则升温段积分钳在 +50，元件到达 ptc_max 后仍被残余积分推着多烧（实测冲到 95℃），
 * 并在限值附近 0↔25% 反复横跳。卸积分后接近限值功率平滑归零。 */
static uint8_t pid_step(float *integral, float *prev, uint32_t *tick,
                        uint32_t now, float setpoint, float measure,
                        float kp, float ki, float kd)
{
    uint8_t first = (*tick == 0U);
    float dt = first ? 0.2f : (float)(now - *tick) / 1000.0f;
    float err, der, out;
    if (dt <= 0.0f || dt > 1.0f) dt = 0.2f;
    err = setpoint - measure;
    der = first ? 0.0f : (err - *prev) / dt;
    *prev = err;
    *tick = now;

    *integral += err * dt;
    if (*integral > 50.0f) *integral = 50.0f;
    if (*integral < -50.0f) *integral = -50.0f;
    out = kp * err + ki * (*integral) + kd * der;
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
    float target = (float)g_sys.params.target_temp;      /* 绌烘?旂洰???娓╁害 */
    float ntc_max = (float)g_sys.params.ptc_max_temp;    /* ??犵?櫒涓婇?愭俯搴?NTC) */
    static uint32_t last_tick = 0;
    uint32_t now = SystemTime_Millis();

    /* ?嫭绔嬬‖杩?娓╀?濇姢锛歅TC 娓╁害瓒???愮?嬪嵆????柇??犵??屼?嶄?濊?栨帶??剁姸??????
     * ??愭?嬭?曟??娉ㄩ????戞湭?帴瀹為????犵???锛????娴嬫櫠?椄绠¤?藉惁姝?甯告?撳????斺??
     * ??垮?? NTC ????璇(??锋????┖)灏辨?? PTC ????柇骞跺脊??婅灞忋??娴嬭?曞?庢?㈠?嶏??
     * if (g_sys.drying_active && NTC_IsOverTemp()) {
     *     PTC_SetPower(0);
     *     trigger_safety(SAFETY_BOX_BROKEN);
     *     return;
     * }
     */

    if (g_sys.safety_state != SAFETY_NONE) { PTC_SetPower(0); return; }

    /* PID 自整定：优先驱动状态机，并同步进度到 g_sys 供界面显示（原：过程函数从未被调用→0%） */
    if (g_sys.pid_autotune_running) {
        PTC_PID_AutotuneProcess();
        g_sys.pid_autotune_progress = PTC_PID_AutotuneGetProgress();
        if (PTC_PID_AutotuneIsDone()) g_sys.pid_autotune_running = 0;
        return;
    }
    if (g_sys.temp_pid_running) {
        PTC_TempPID_AutotuneProcess();
        g_sys.temp_pid_progress = PTC_TempPID_AutotuneGetProgress();
        if (PTC_TempPID_AutotuneIsDone()) g_sys.temp_pid_running = 0;
        return;
    }

    switch (g_sys.run_state) {
    case STATE_HEATING:
    case STATE_DRYING: {
        /* ??樺共??ㄧ?嬮?庢????掑?ㄥ?熺??锛氬??濮嬪嵆 100%锛屼?? PTC/NTC 娓╁害??犲?? */
        Fan_SetSpeed(100);
        /* 绌烘?? PID锛氭?? AHT20 绌烘?旀俯搴︽?夊?扮洰???锛?璇樊澶р??00% 蹇?熷??娓╋?夈??
         * NTC PID锛氭?婂?犵?櫒娓╁害??愬?跺湪 ptc_max_temp 浠ュ??锛圢TC ??颁?婇?愨?掑?嬪?熺??锛夈??
         * PTC ??栦袱???杈?灏忓?硷?氭?旀病??扮洰???灏辨弧??熺??锛???犵?櫒?帴杩戜?婇?愬氨??愬?熴??*/
        uint8_t p_air = pid_step(&pid_air_int, &pid_air_prev, &pid_air_tick,
                                 now, target, g_sys.current_temp,
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

        if (g_sys.run_state == STATE_HEATING && g_sys.current_temp >= target) {
            g_sys.run_state = STATE_DRYING;   /* 绌烘?旇揪?洰??????寮?濮嬫?掓俯??掕???*/
            last_tick = now;
        }
        if (g_sys.run_state == STATE_DRYING) {
            /* 倒计时：固定步进 +1000ms 并结转余量。
             * 原逻辑 last_tick = now 每秒丢弃 0~(tick周期) 的不足一秒余数，
             * 长时间运行倒计时明显偏慢；>5s 断档（暂停等）只重新锚定，不连扣补偿。 */
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
        Fan_SetSpeed(100);
        if (g_sys.ptc_temp <= (float)g_sys.params.ptc_cooling_temp) { g_sys.run_state = STATE_COMPLETE; Fan_Off(); }
        break;
    case STATE_PAUSED:
        PTC_SetPower(0);
        break;
    default:
        /* 绌洪棽/瀹屾?愮姸???锛氭??缁俯搴﹀?夊?ㄢ?斺?斿彧妫?娴?NTC 瓒?杩???峰嵈娓╁害 */
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