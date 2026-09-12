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
#include "bsp_can.h"
#include "bsp_cs1237.h"
#include "bsp_encoder.h"
#include "bsp_fan.h"
#include "bsp_ntc.h"
#include "bsp_ptc.h"
#include "bsp_rgb_led.h"
#include "bsp_stepper.h"
#include "esp_at.h"
#include "esp_http_bridge.h"
#include "esp_link.h"
#include "can_cluster.h"
#include "music_play.h"
#include "music_store.h"
#include "music_ota.h"
#include "music_data.h"
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

#define SWING_BASE_STEPS_PER_DEG  4545U  /* base steps/deg*100, then x motor_swing_cal/100; �?180deg@300%�60deg, �45.45˙/� */
#define SWING_CAL_DEFAULT         100   /* swing cal default %; adjust via UI to match real swing */

SystemState_t g_sys;

#ifndef BOOTLOADER_BUILD
static void refresh_api_data(void);
static void read_sensors(void);
static uint8_t s_rgb_ready = 0;   /* �屏渐�后才允� RGB �效（上电熄灯待命� */
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

    /* ?审塵橾�PB0噶?�?塚﹍て??�� */
    {
        GPIO_InitTypeDef g;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        g.GPIO_Pin = PIN_TFT_BL_PIN;
        g.GPIO_Mode = GPIO_Mode_Out_PP;
        g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(PIN_TFT_BL_PORT, &g);
        GPIO_SetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
    }

    /* g_sys ??�?�?�?圲I ???�?�? */
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
    g_sys.params.motor_oscillate_angle = 60;  /* 骞冲�????姩瑙掑�(瀹為??搴︽�) 1-360 */
    g_sys.params.motor_driver = MOTOR_DRIVER_A4988;
    g_sys.params.motor_current = 2;
    g_sys.params.motor_stealthchop = 0;
    g_sys.params.motor_work_count = 0;      /* 0=work forever, N=rest every N cycles */
    g_sys.params.motor_rest_sec = 0;        /* 0=涓嶄?�?�??宸ヤ?滄?�?????�?????�? */
    g_sys.params.motor_swing_cal = SWING_CAL_DEFAULT;
    g_sys.params.rgb_enabled = 1;
    g_sys.params.rgb_led_bright = 100;
    g_sys.params.rgb_strip_bright = 100;
    g_sys.params.can_enabled = 0;   /* CAN 默�关� */
    g_sys.params.can_role = 0;      /* 默�主� */

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
    }            /* RGB??潯榛��? */

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

    /* 涓嶇� Board_Init�??� NTC_Init ???ADC ?�??? while锛屾�?帴浼�?熷櫒?�??藉崱姝伙?�??
     * ?�??�?�?�?�?�?�?�?�????�?櫒??�??+ SWJ ??�?�??锛�??*/
    Board_EarlyInit();
    Watchdog_Init();   /* App ?�?�??��??�?�?�?�??Bootloader */

    /* ??抽敭锛氫?? RCC 璇�?�?�???�???锛岀?�??SystemCoreClock�?�? bootloader �??嚧锛�??
     * ?�??? SysTick ?�??�?�?孲ystemTime_Millis 涓嶈�???*/
    SystemCoreClockUpdate();
    SystemTime_Init(); /* ?�?� SysTick锛屼?? SystemTime_Millis/Encoder 璁℃� */

    /* Bootloader ???BootloaderV2_JumpToApp() 璺宠�??�???敤浜? __disable_irq()�?
     * ???App ???SystemInit/main 浠庝?�?嶆柊�?涓� ???PRIMASK 淇濇?? 1 ???SysTick
     * 姘镐?嶈�??? ???SystemTime_Millis ??�?? ???缂栫???�??�???�??夎?�??�?ㄥけ???
     * �???�浆浠嶅彲?敤锛???�?跺彧杞� GPIO 涓嶄?�?栦腑?柇锛�??姝ゅ??�?椤�?嶆柊�?涓�???*/
    __enable_irq();

    /* 涓婄�?�??�?�?????�?s) ???��?�?�?? Bootloader 涓�浇妯�?�??
     * ?�?帴杞??夐挳��?�?�?�?�?? Encoder_Process()�?�??????�?�??璐�?嬩�锛?
     * 瀵艰嚧杩�????�?? Encoder_GetEvent() 姘歌?滄嬁涓嶅??LONG_PRESS锛�??*/
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
            OTA_EnterBootloader();   /* ???FORCE_BOOT ???蹇�苟澶�?�?�?�?�?�?? */
        }
    }

    /* 灞忓?�?�?�??+ 涓荤?�??*/
    TFT_Init();
    Buzzer_Init();
    Backlight_Init();

    g_sys.buzzer_link = 1;
    g_sys.buzzer_vol = 5;
    g_sys.light_switch = 1;
    g_sys.backlight = 100;
    g_sys.theme = 1;  /* 榛�??楄�涓�?? #1E1E2E */
    g_sys.screen_off_timeout = 0;  /* 榛�浠庝?�??�? */
    g_sys.wifi_enabled = 1;
    g_sys.pid_calibrated = 0;
    System_Init();   /* 浠庡?�?�lash??犺浇宸蹭?�?�???暟锛?�???�?樿??�?�??澶辫�??欑敤榛�???*/
    TFT_SetBrightness(g_sys.backlight);
    theme_apply();

    /* AHT20 娓╂箍搴�?�?熷櫒??�?�?�??杞�I2C锛�B10=SCL / PB11=SDA锛�??
     * ??�??3娆�?斿涓婄�?椂搴�?�?�?�?栧け璐ヤ?嶉樆濉炲�?姩锛�?岄潰浠嶆樉绀�?樿??�??
     * ?�??????read_sensors() ???缁х画灏濊?曡??�??*/
    {
        int aht_try;
        for (aht_try = 0; aht_try < 3; aht_try++) {
            if (AHT20_Init() == 0) break;
            { volatile uint32_t d = 0; while (d < 100000U) d++; }
        }
    }

    /* PTC/Fan/NTC 初�化（硬件已接好对应外�） */
    
    PTC_Init();
    Fan_Init();
    NTC_Init();
    CS1237_Init();
    { volatile uint32_t d = 0; while (d < 3000000U) d++; }  /* settle ~200ms for stable boot tare */                 /* ??�?�?�?�??CS1237锛圥A0=DOUT锛�A1=SCLK�?*/
    RGB_Strip_Init();              /* WS2812 RGB ??潯锛圥B6=?�?????�?孭B7=7棰�?涘�??�??*/
    Stepper_Init();
    CAN_Cluster_Init();
    EspLink_Init();                /* 姝ヨ?涚數?� GPIO锛圥B12-14锛�?�?�?�?�?樿浣胯?�?�?樼數�????*/

    UI_ShowBootScreen();   /* boot progress screen + sensor read */

    /* 音乐：�部固件存储 / 上传接收 / �放（TIM3 借用� */
    MusicStore_Init();
    MusicOta_Init();
    MusicPlay_Init();
    UI_DrawMainScreen();        /* ?????�?楁�缁�?朵富??�??*/
    /* ?????�?�?樹寒璧凤?岄湶?�涓荤?岄潰�?�?娓�姩?敾锛? */
    s_rgb_ready = 1;               /* 与屏幕渐�同�：此刻起灯条才��动效 */
    {
        uint16_t b;
        for (b = 0U; b <= 100U; b += 5U) {
            TFT_SetBrightness(b);
            Watchdog_Kick();
            { volatile uint32_t d = 0; while (d < 200000U) d++; }  /* �?14ms */
        }
        TFT_SetBrightness(100);
    }

    /* 缂栫???櫒锛欵ncoder_Process ?????�???????��/??曞嚮锛岄�??�?�?�?曠敱涓��?�绔�??娴�??
     * ??��??庢暣灞忛?�?樹富??岄潰�???犵��?娈�奖锛�?変腑�???忓崱???楂樹寒锛�??*/
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
        MusicStore_Poll();   /* 音乐上传落盘（活动时推进，否则空�� */
        MusicPlay_Poll();    /* 音乐�放音符推进（背景持续� */
        CAN_Cluster_Process();
        EspLink_Process();

        /* 编码�/按键输入� Encoder_Process 统一处理�
         * �屏时任意输入唤醒（Encoder_Process 内部� g_sys.screen_off），
         * 正常时�理旋转/单击/长按事件。g_last_input_ms 每�输入都会刷新� */
        Encoder_Process();

        /* �屏判�：无输入超时（旋�/按键都会更新 g_last_input_ms）才熄屏�
         * 注意这里要用"当前新取的时�"而非��顶部� now：Encoder_Process �能刚�
         * g_last_input_ms 刷新到比 now 更新的�，� now-旧�会回绕成巨大数→�熄屏（旋�即黑屏）� */
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



            /* ?�绔��??�??娴�?��??�????�?�?��????崲锛?���?崱涓婇暱??夌敱缂栫???櫒澶??????樺共锛屼?�?�?�?�??*/
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
#define TEMP_SAFETY_ANCHOR_CAP    35.0f     /* 初�锚点上� */
#define TEMP_RISE_MIN             0.2f      /* 冷启� 1 分钟温升阈�(�) */
#define TEMP_RISE_WINDOW_MS       60000U    /* 冷启动�测窗�(1min) */
#define TEMP_RISE_HOT_MIN         1.0f      /* ���(初�>35�) 2 分钟温升阈�(�) */
#define TEMP_RISE_HOT_WINDOW_MS   120000U   /* ��动�测窗�(2min) */
#define TEMP_DROP_DEBOUNCE        5         /* 低于锚点(裕度�)连续 N �(�500ms)才报警，抗抖� */
#define TEMP_DROP_MARGIN          3.0f      /* 冷启�：跌破初始锚� 3� 判�体破损（用户�求 >3� 触发� */
#define TEMP_HOT_DROP_DELTA       3.0f      /* �态：峰�回落超� 3� 判�体破损 */

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

    /* AHT20 璇�?栧け璐ワ?�?�??涓婃娓╁�锛�???嫢�??�??樺共??欒�??�?�?�?濇姢�?
     * ??�??"浼犳?熷櫒??? 25掳C ???姘歌?滆涓烘�??�????????缁?�??"????�?�?�???*/
    {
        static uint8_t aht_fail = 0;
        int ar = AHT20_Read(&temp, &hum);
        if (ar == -2) {
            /* 等待�换完成（触发�/�� 80ms）：属�常节拍，不计失� */
        } else if (ar != 0) {
            /* 连续失败(�2s)才报安全：AHT20 偶发忙�取失败属�常 */
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
    if (weight > -1000.0f) {   /* -9999=�换未就绪，整帧跳过（显示保持上一有效值） */
        g_sys.weight_g = (int32_t)(weight + ((weight >= 0.0f) ? 0.5f : -0.5f));

        /* �动重去皮仅允许在�机建稳窗口（6~25s、空闲状态）�
         * 烘干�电桥温漂会缓慢越� ±阈�，若不限时段，烘到�半会�
         * 盘内真实物料（� 240g）连同漂移一�"清零"� */
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
    /* 无效读数(-9999=�换未就绪)丢弃：保持上�显示值，不触发去� */

    ptc_raw = NTC_GetTemperature();
    g_sys.ptc_temp = (float)ptc_raw / 10.0f;

    /* 称重温度分�补偿：电桥温漂使重量随温度漂移。以 25� 为基准，
     * 按当� NTC(烘焙�/机体)温度查分段系数�重量修正�
     * 系数�实测标定：每� = 每℃ 的重量修正比例（�正可负）�
     * 例：0℃~15� 系数 -0.0004 � 10� 时修� weight*(1-0.0004*(25-10))=0.994� */
    {
        float tc;
        float t = g_sys.ptc_temp;
        if (t < 15.0f)       tc = -0.0004f;   /* 低温� */
        else if (t < 35.0f)  tc = 0.0f;       /* 常温段（基准� */
        else                 tc = 0.0004f;    /* 高温� */
        if (g_sys.weight_g != 0) {
            float comp = (float)g_sys.weight_g * (1.0f + tc * (t - 25.0f));
            g_sys.weight_g = (int32_t)(comp + ((comp >= 0.0f) ? 0.5f : -0.5f));
        }
    }

    /* NTC �?�???矾�?��????�????�??绔?�?�?熻�??�?�?�?�??
     * ??�?�?�??娉ㄩ????戞湭?帴�為????�?�??�????娴��?椄绠¤?藉惁�?��?�??锛涙?�?�?�?�?�??
     * if (ptc_raw <= -100 || ptc_raw >= 2000) {
     *     if (g_sys.drying_active && g_sys.safety_state == SAFETY_NONE) {
     *         trigger_safety(SAFETY_BOX_BROKEN);
     *     }
     * }
     */
}

static void update_rgb(void)
{
    /* 上电�屏前保持�条熄�：背光渐�（s_rgb_ready=1）后才开始走�效，
     * 避免"屏幕还黑��条就僵硬��"� */
    if (!s_rgb_ready) return;    /* 背光渐亮前灯条保持熄� */
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
        /* 瀹屾?�害锛�?�?�?�?�?�?�?掕?椂姣�?�?? 14% ??逛寒�?棰�?�?�?�?ㄤ寒 */
        uint8_t pct = 0;
        if (g_sys.params.dry_time_sec > 0)
            pct = (uint8_t)(100U - g_sys.remaining_sec * 100U / g_sys.params.dry_time_sec);
        if (pct > 100) pct = 100;
        RGB_Progress_DryingBar(pct);
        complete_start = 0;
    } else if (g_sys.run_state == STATE_COOLING || g_sys.run_state == STATE_COMPLETE) {
        /* 烘干计时结束（进入冷却）即�为完成�绿，冷却到位后保� */
        if (complete_start == 0) complete_start = SystemTime_Millis();
        if (SystemTime_Millis() - complete_start < 30000) {
            RGB_Status_Green();
        } else {
            RGB_Status_Off();
        }
        RGB_Progress_Rainbow();
    } else {
        /* 绌洪棽锛歅B6 ??�?孭B7 褰╄櫣娴??� */
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
    Fan_SetSpeed(100);                       /* 椋庢????�?�?�?? */
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
    g_sys.remaining_sec = g_sys.params.dry_time_sec;   /* 停�后剩余时长归整为�定时长，避免网�/界面残留倒�时 */
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

/* 腔体温升安全监测（�比 AHT20 空气温度 current_temp）：
 * 锚定初�点 anchor = min(初�温�, 35�)�
 * 冷启�(初�≤35)：�测窗� 1 分钟，须温升 �0.2� 才判加热正常�
 * ���(初�>35)：�明趁�内余温继续烘干，只��测温升，2 分钟内须温升 �1.0℃�
 * 温升监测值取�大�锁存（�高不低），滤� AHT20 0.1~0.2� 的�数回跳�
 * 冷启动全程：温度跌破初�锚点（连帧防抖）→ 判故障（加热失效/传感器异常）� */
static uint8_t  rise_mon_active = 0;   /* �次烘干周期监测是否已初�化 */
static float    rise_initial = 0.0f;   /* 初�温度（实际读数，作温升基准� */
static float    rise_anchor  = 0.0f;   /* 锚点 = min(初�温�, 35) */
static float    rise_peak    = 0.0f;   /* �大�锁存：�高不� */
static uint32_t rise_start_ms = 0;
static uint8_t  rise_hot = 0;          /* 初�温� > 35� ��� */
static uint8_t  rise_confirmed = 0;    /* 窗口内温升达标，�认加�有效 */
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
    /* 已�于�标附�(<3℃差�)：温升门限失去意义（空气�会在设定点附近缓慢�近�
     * 摆动 ±0.3℃），�时�"�1次�测已通过"处理，仅保留跌落保护，避免最� 1�
     * 维持过慢���"长时间不上升→安全�告"� */
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

    /* 首�进入本周期：锚定初始点 */
    if (!rise_mon_active) { rise_monitor_start(); return; }

    /* �测�最大�锁存（�高不低），滤传感器回� */
    if (g_sys.current_temp > rise_peak) rise_peak = g_sys.current_temp;

    if (rise_hot) {
        /* ���：只�测温升，窗口 2 分钟须温� �1.0� */
        if (!rise_confirmed && (int32_t)(now - rise_start_ms) >= (int32_t)TEMP_RISE_HOT_WINDOW_MS) {
            if (rise_peak - rise_initial < TEMP_RISE_HOT_MIN) { trigger_safety(SAFETY_LID_OPEN); return; }
            rise_confirmed = 1;
        }
        /* �态跌落保护：峰�回� >3℃（用户要求）→ 箱体破损/�盖�
         * 峰�起�=初�温度，�先上升后回落，天然不会在�动阶段�触发� */
        if (rise_peak - g_sys.current_temp >= TEMP_HOT_DROP_DELTA) {
            if (++rise_drop_cnt >= TEMP_DROP_DEBOUNCE) { trigger_safety(SAFETY_BOX_BROKEN); return; }
        } else {
            rise_drop_cnt = 0;
        }
    } else {
        /* 冷启�：窗� 1 分钟须温� �0.2℃（�1次�测） */
        if (!rise_confirmed && (int32_t)(now - rise_start_ms) >= (int32_t)TEMP_RISE_WINDOW_MS) {
            if (rise_peak - rise_initial < TEMP_RISE_MIN) { trigger_safety(SAFETY_LID_OPEN); return; }
            rise_confirmed = 1;   /* �1次�测�过：加�有效 */
        }
        /* 全程回落�测：显著跌破初�锚点（1℃�度外，连帧防抖）→ 判�体破损�
         * 仅加�已确�(rise_confirmed)后启�：启动阶段�机冷�吹+残余�使�数先跌后升�
         * 若未�认就判跌落会"刚烘干就箱体破损"；确认后的真实大跌落(��/漏热)仍能抓住� */
        if (rise_confirmed && g_sys.current_temp < rise_anchor - TEMP_DROP_MARGIN) {
            if (++rise_drop_cnt >= TEMP_DROP_DEBOUNCE) { trigger_safety(SAFETY_BOX_BROKEN); return; }
        } else {
            rise_drop_cnt = 0;
        }
    }
}

/* ???PID锛氱┖�旀��?PID�?涓绘帶锛???�?? AHT20�? ??�?櫒娓╁??PID�?淇濇姢锛???�?? NTC�?*/
static float pid_air_int = 0.0f, pid_air_prev = 0.0f;
static uint32_t pid_air_tick = 0;
static float pid_ntc_int = 0.0f, pid_ntc_prev = 0.0f;
static uint32_t pid_ntc_tick = 0;

static void pid_reset(void)
{
    pid_air_int = 0.0f; pid_air_prev = 0.0f; pid_air_tick = 0U;
    pid_ntc_int = 0.0f; pid_ntc_prev = 0.0f; pid_ntc_tick = 0U;
}

/* 通用 PID 步进：输� 0-100 百分比（setpoint=�标，measure=�测温）�
 * 反积分饱�(back-calculation)：输出到�/底时把超出量即时从积分卸掉��
 * 否则升温段积分钳� +50，元件到� ptc_max 后仍�残余�分推�多烧（实测冲� 95℃）�
 * 并在限�附� 0�25% 反�横跳�卸�分后接近限�功率平滑归零� */
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
    float target = (float)g_sys.params.target_temp;      /* 绌烘?旂洰???娓╁� */
    float ntc_max = (float)g_sys.params.ptc_max_temp;    /* ??�?櫒涓婇?���?NTC) */
    static uint32_t last_tick = 0;
    uint32_t now = SystemTime_Millis();

    /* ?�绔�‖杩?娓╀?濇姢锛歅TC 娓╁害瓒???�?��????�??�?�?�?�?�?栨帶??剁姸??????
     * ??�?�?�??娉ㄩ????戞湭?帴�為????�?�??�????娴��?椄绠¤?藉惁�?��?�????�??
     * ??�?? NTC ??�??璇�(??�????┖)灏辨?? PTC ????柇骞跺脊??婅灞忋??娴�?�?�?�?�??
     * if (g_sys.drying_active && NTC_IsOverTemp()) {
     *     PTC_SetPower(0);
     *     trigger_safety(SAFETY_BOX_BROKEN);
     *     return;
     * }
     */

    if (g_sys.safety_state != SAFETY_NONE) { PTC_SetPower(0); return; }

    /* PID �整定：优先驱动状态机，并同�进度到 g_sys 供界面显示（原：过程函数从未�调用�0%� */
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
        /* ??樺共??�?�?�????�?�?�??锛氬??��� 100%锛屼?? PTC/NTC 娓╁�??�?? */
        Fan_SetSpeed(100);
        /* 绌烘?? PID锛氭?? AHT20 绌烘?��搴︽?�?��???�?璇樊澶р??00% 蹇?�??娓╋?�??
         * NTC PID锛氭?�?�?櫒娓╁�??�?跺湪 ptc_max_temp 浠ュ??锛圢TC ??�?�?�?�?�?�??锛�??
         * PTC ??栦袱???�?灏忓?�?�?��??��???灏辨�??�??�???�?櫒?帴杩�?�?��??�?�??*/
        /* stable band: pull temperature back into [target-0.5, target+0.5] */
        float air_sp = target;
        if (g_sys.current_temp > target + 0.5f) air_sp = target - 0.5f;
        uint8_t p_air = pid_step(&pid_air_int, &pid_air_prev, &pid_air_tick,
                                 now, air_sp, g_sys.current_temp,
                                 g_sys.params.pid_air_kp, g_sys.params.pid_air_ki, g_sys.params.pid_air_kd);
        uint8_t p_ntc = pid_step(&pid_ntc_int, &pid_ntc_prev, &pid_ntc_tick,
                                 now, ntc_max, g_sys.ptc_temp,
                                 g_sys.params.pid_ntc_kp, g_sys.params.pid_ntc_ki, g_sys.params.pid_ntc_kd);
        {
            uint8_t pwr = (p_air < p_ntc) ? p_air : p_ntc;
            /* 元件�顶保护：� ptc_max+10� 直接�功率，不依赖 PID 收敛
             * （抗�分饱和后的残余热�性滑行兜底，防�"�到空气目标却烧到95�+"� */
            if (g_sys.ptc_temp > (float)g_sys.params.ptc_max_temp + 10.0f) pwr = 0U;
            PTC_SetPower(pwr);
        }

        if (g_sys.run_state == STATE_HEATING && g_sys.current_temp >= target - 0.5f) {
            g_sys.run_state = STATE_DRYING;   /* 绌烘?旇揪?�??????�?��?掓俯??掕???*/
            last_tick = now;
        }
        if (g_sys.run_state == STATE_DRYING) {
            /* 倒�时：固定�进 +1000ms 并结�余量�
             * 原�辑 last_tick = now 每�丢� 0~(tick周期) 的不足一秒余数，
             * 长时间运行��时明显偏慢�>5s �档（暂停等）�重新锚定，不连扣补偿� */
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
        /* 绌洪�/瀹屾?��???锛氭??缁�搴﹀?�?�?�?斿彧�?�?NTC �?�???峰嵈娓╁� */
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