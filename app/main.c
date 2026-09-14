#include "stm32f10x.h"

#include "board.h"
#include "bsp_w25q128.h"
#include "ota_display.h"
#include "platform_contract.h"
#include "system_time.h"
#include "system_config.h"
#include "bsp_tft_st7789.h"

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

#define SWING_BASE_STEPS_PER_DEG  4545U  /* base steps/deg*100, then x motor_swing_cal/100; 濉?180deg@300%鈻60deg, 顪45.45藱/顐 */
#define SWING_CAL_DEFAULT         100   /* swing cal default %; adjust via UI to match real swing */

SystemState_t g_sys;

#ifndef BOOTLOADER_BUILD
static void refresh_api_data(void);
static void read_sensors(void);
static uint8_t s_rgb_ready = 0;   /* 寮灞忔笎浜鍚庢墠鍏佽 RGB 鐏鏁堬紙涓婄數鐔勭伅寰呭懡锛 */
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

    /* ?瀹☆椊濉垫┚顧烆湁顡PB0顡囧櫠?绔?濉氼亱锕嶃仸??顒顡 */
    {
        GPIO_InitTypeDef g;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        g.GPIO_Pin = PIN_TFT_BL_PIN;
        g.GPIO_Mode = GPIO_Mode_Out_PP;
        g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(PIN_TFT_BL_PORT, &g);
        GPIO_SetBits(PIN_TFT_BL_PORT, PIN_TFT_BL_PIN);
    }

    /* g_sys ??婵?瀣?鏍?鍦睮 ???鐟?閿? */
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
    g_sys.params.motor_oscillate_angle = 60;  /* 楠炲啿褰????濮╃憴鎺戝(鐎圭偤??鎼达附鏆) 1-360 */
    g_sys.params.motor_driver = MOTOR_DRIVER_A4988;
    g_sys.params.motor_current = 2;
    g_sys.params.motor_stealthchop = 0;
    g_sys.params.motor_work_count = 0;      /* 0=work forever, N=rest every N cycles */
    g_sys.params.motor_rest_sec = 0;        /* 0=娑撳秳?鎴?顖??瀹搞儰?婊勵偧?鏆?????宥?????閿? */
    g_sys.params.motor_swing_cal = SWING_CAL_DEFAULT;
    g_sys.params.rgb_enabled = 1;
    g_sys.params.rgb_led_bright = 100;
    g_sys.params.rgb_strip_bright = 100;
    g_sys.params.can_enabled = 0;   /* CAN 榛樿ゅ叧闂 */
    g_sys.params.can_role = 0;      /* 榛樿や富鏈 */

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
    }            /* RGB??顖涙蒋姒涙款吇瀵? */

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

    /* 娑撳秶鏁 Board_Init閿??鎯 NTC_Init ???ADC ?鐗??? while閿涘本婀?甯存导鐘?鐔锋珤?褰??钘夊幢濮濅紮?澶??
     * ?褰??姘?澶?銊?鏇?姘?婵?瀣?鏍????鐘?顓炴珤??铏??+ SWJ ??宥?鐘??閿涘??*/
    Board_EarlyInit();
    Watchdog_Init();   /* App ?鍤?鎯??瀣妫??妤?灞?宥?婵??Bootloader */

    /* ??鎶芥暛閿涙矮?? RCC 鐠囪?鐐?鐐???妞???閿涘瞼?鐘??SystemCoreClock閿?娑? bootloader 娑??鍤ч敍澶??
     * ?鎯??? SysTick ?鎳??鐔?娆?瀛瞴stemTime_Millis 娑撳秷铔???*/
    SystemCoreClockUpdate();
    SystemTime_Init(); /* ?鎯?濮 SysTick閿涘奔?? SystemTime_Millis/Encoder 鐠佲剝妞 */

    /* Bootloader ???BootloaderV2_JumpToApp() 鐠哄疇娴??宥???鏁ゆ禍? __disable_irq()閿?
     * ???App ???SystemInit/main 娴犲簼?宥?宥嗘煀瀵?娑擃厽鏌 ???PRIMASK 娣囨繃?? 1 ???SysTick
     * 濮橀晲?宥埿??? ???SystemTime_Millis ??鑽?? ???缂傛牜???娅??鏇???鏆??澶庮吀?妞??銊?銊ャ亼???
     * 閿???瀣娴嗘禒宥呭讲?鏁ら敍???鐘?璺哄涧鏉烆喛顕 GPIO 娑撳秳?婵?鏍﹁厬?鏌囬敍澶??濮濄倕??韫?妞ゅ?宥嗘煀瀵?娑擃厽鏌???*/
    __enable_irq();

    /* 娑撳﹦鏁?鏆??澶?鏍?????缁?s) ???瀵鍝?鎯?娑?? Bootloader 娑撳娴囧Ο鈥?蹇??
     * ?娲?甯存潪顔款嚄??澶愭尦瀵鏇?姘?灞?宥?婵?? Encoder_Process()閿?鐎??????銊?姘??鐠愰?瀣╂㈤敍?
     * 鐎佃壈鍤ф潻娆????宥?? Encoder_GetEvent() 濮樻瓕?婊勫瑏娑撳秴??LONG_PRESS閿涘??*/
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
            OTA_EnterBootloader();   /* ???FORCE_BOOT ???韫囨鑻熸径宥?宥?灞?宥?姘?鏂?? */
        }
    }

    /* 鐏炲繐?鏇?婵?瀣??+ 娑撹崵?宀??*/
    TFT_Init();
    Buzzer_Init();
    Backlight_Init();

    g_sys.buzzer_link = 1;
    g_sys.buzzer_vol = 5;
    g_sys.light_switch = 1;
    g_sys.backlight = 100;
    g_sys.theme = 1;  /* 姒涙款吇??妤勫婃稉濠?? #1E1E2E */
    g_sys.screen_off_timeout = 0;  /* 姒涙款吇娴犲簼?宥??鐏? */
    g_sys.wifi_enabled = 1;
    g_sys.pid_calibrated = 0;
    System_Init();   /* 娴犲骸?鏍?鈺lash??鐘烘祰瀹歌弓?婵?妯???鏆熼敍?鐟???鏍?妯款吇??纭?澶??婢惰精瑙??娆戞暏姒涙款吇???*/
    TFT_SetBrightness(g_sys.backlight);
    theme_apply();

    /* SHT40 濞撯晜绠嶆惔锔?鐘?鐔锋珤??婵?瀣?鏍??鏉烆垯娆I2C閿涘瑽10=SCL / PB11=SDA閿涘??
     * ??宥??3濞嗏?鏂款嚠娑撳﹦鏁?妞傛惔蹇?娑?婵?瀣?鏍с亼鐠愩儰?宥夋▎婵夌偛鎯?濮╅敍宀?宀勬桨娴犲秵妯夌粈娲?妯款吇??纭??
     * ?鎳??????read_sensors() ???缂佈呯敾鐏忔繆?鏇☆嚢??鏍??*/
    {
        int sht_try;
        for (sht_try = 0; sht_try < 3; sht_try++) {
            if (SHT40_Init() == 0) break;
            { volatile uint32_t d = 0; while (d < 100000U) d++; }
        }
    }

    /* PTC/Fan/NTC 鍒濆嬪寲锛堢‖浠跺凡鎺ュソ瀵瑰簲澶栬撅級 */
    
    PTC_Init();
    Fan_Init();
    NTC_Init();
    CS1237_Init();
    { volatile uint32_t d = 0; while (d < 3000000U) d++; }  /* settle ~200ms for stable boot tare */                 /* ??瀣?娑?鐘?鐔??CS1237閿涘湧A0=DOUT閿涘瑼1=SCLK閿?*/
    RGB_Strip_Init();              /* WS2812 RGB ??顖涙蒋閿涘湧B6=?濮?????顖?瀛瑽7=7妫版?娑樺??顖??*/
    Stepper_Init();
    CAN_Cluster_Init();
    EspLink_Init();                /* 濮濄儴?娑氭暩?婧 GPIO閿涘湧B12-14閿涘?婵?瀣?鏍?宀?妯款吇娴ｈ儻?鍊?姘?妯兼暩楠????*/

    UI_ShowBootScreen();   /* boot progress screen + sensor read */

    /* 闊充箰锛氬栭儴鍥轰欢瀛樺偍 / 涓婁紶鎺ユ敹 / 鎾鏀撅紙TIM3 鍊熺敤锛 */
    MusicStore_Init();
    MusicOta_Init();
    MusicPlay_Init();
    UI_DrawMainScreen();        /* ?????澶?妤佹傜紒妯?鏈靛瘜??宀??*/
    /* ?????澶?鎰?妯瑰瘨鐠у嚖?宀勬苟?鍤娑撹崵?宀勬桨閿?鏉?濞撯冲З?鏁鹃敍? */
    s_rgb_ready = 1;               /* 涓庡睆骞曟笎浜鍚屾ワ細姝ゅ埢璧风伅鏉℃墠寮鍚鍔ㄦ晥 */
    {
        uint16_t b;
        for (b = 0U; b <= 100U; b += 5U) {
            TFT_SetBrightness(b);
            Watchdog_Kick();
            { volatile uint32_t d = 0; while (d < 200000U) d++; }  /* 缁?14ms */
        }
        TFT_SetBrightness(100);
    }

    /* 缂傛牜???娅掗敍娆祅coder_Process ?????銊???????瀣娴/??鏇炲毊閿涘矂鏆??澶?娑?婊?鏇犳暠娑撳鏌?瀚缁斿??濞村??
     * ??瀣娴??搴㈡殻鐏炲繘?宥?妯瑰瘜??宀勬桨閿???鐘垫у?濞堝濂栭敍宀?澶夎厬濡???蹇撳幢???妤傛ü瀵掗敍澶??*/
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
        MusicStore_Poll();   /* 闊充箰涓婁紶钀界洏锛堟椿鍔ㄦ椂鎺ㄨ繘锛屽惁鍒欑┖杞锛 */
        MusicPlay_Poll();    /* 闊充箰鎾鏀鹃煶绗︽帹杩涳紙鑳屾櫙鎸佺画锛 */
        CAN_Cluster_Process();
        EspLink_Process();

        /* 缂栫爜鍣/鎸夐敭杈撳叆鐢 Encoder_Process 缁熶竴澶勭悊锛
         * 鎭灞忔椂浠绘剰杈撳叆鍞ら啋锛圗ncoder_Process 鍐呴儴娓 g_sys.screen_off锛夛紝
         * 姝ｅ父鏃跺勭悊鏃嬭浆/鍗曞嚮/闀挎寜浜嬩欢銆俫_last_input_ms 姣忔¤緭鍏ラ兘浼氬埛鏂般 */
        Encoder_Process();

        /* 鎭灞忓垽鎹锛氭棤杈撳叆瓒呮椂锛堟棆杞/鎸夐敭閮戒細鏇存柊 g_last_input_ms锛夋墠鐔勫睆銆
         * 娉ㄦ剰杩欓噷瑕佺敤"褰撳墠鏂板彇鐨勬椂闂"鑰岄潪寰鐜椤堕儴鐨 now锛欵ncoder_Process 鍙鑳藉垰鎶
         * g_last_input_ms 鍒锋柊鍒版瘮 now 鏇存柊鐨勫硷紝鐢 now-鏃у间細鍥炵粫鎴愬法澶ф暟鈫掕鐔勫睆锛堟棆杞鍗抽粦灞忥級銆 */
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



            /* ?瀚缁斿鏆??澶??濞村?姘瀵??宀????婊?鏇?瀣妫????宕查敍?濠鍨瀹?宕辨稉濠囨毐??澶屾暠缂傛牜???娅掓径??????妯哄叡閿涘奔?宥?娑?婊?鏇??*/
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

    /* sensor safety/control task: 100ms window (faster temp refresh; SHT40 blocks ~16ms) */
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
#define TEMP_SAFETY_ANCHOR_CAP    35.0f     /* 鍒濆嬮敋鐐逛笂闄 */
#define TEMP_RISE_MIN             0.2f      /* 鍐峰惎鍔 1 鍒嗛挓娓╁崌闃堝(鈩) */
#define TEMP_RISE_WINDOW_MS       60000U    /* 鍐峰惎鍔ㄦ娴嬬獥鍙(1min) */
#define TEMP_RISE_HOT_MIN         1.0f      /* 鐑鍚鍔(鍒濆>35鈩) 2 鍒嗛挓娓╁崌闃堝(鈩) */
#define TEMP_RISE_HOT_WINDOW_MS   120000U   /* 鐑鍚鍔ㄦ娴嬬獥鍙(2min) */
#define TEMP_DROP_DEBOUNCE        5         /* 浣庝簬閿氱偣(瑁曞害澶)杩炵画 N 娆(鈮500ms)鎵嶆姤璀︼紝鎶楁姈鍔 */
#define TEMP_DROP_MARGIN          3.0f      /* 鍐峰惎鍔锛氳穼鐮村垵濮嬮敋鐐 3鈩 鍒ょ变綋鐮存崯锛堢敤鎴疯佹眰 >3鈩 瑙﹀彂锛 */
#define TEMP_HOT_DROP_DELTA       3.0f      /* 鐑鎬侊細宄板煎洖钀借秴杩 3鈩 鍒ょ变綋鐮存崯 */

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

    /* SHT40 鐠囪?鏍с亼鐠愩儻?姘?婵??娑撳﹥顐煎〒鈺佸抽敍灞???瀚㈠??婀??妯哄叡??娆捫??鎴?澶?銊?婵囧Б閿?
     * ??鍨??"娴肩姵?鐔锋珤??? 25鎺矯 ???濮樻瓕?婊嗩吇娑撶儤鐥??鐗????????缂侇厼?鐘??"????鍤?鎳?婧???*/
    {
        static uint8_t sht_fail = 0;
        int ar = SHT40_Read(&temp, &hum);
        if (ar == -2) {
            /* 绛夊緟杞鎹㈠畬鎴愶紙瑙﹀彂鍚/鏈鍒 80ms锛夛細灞炴ｅ父鑺傛媿锛屼笉璁″け璐 */
        } else if (ar != 0) {
            /* 杩炵画澶辫触(绾2s)鎵嶆姤瀹夊叏锛歋HT40 鍋跺彂蹇欒诲彇澶辫触灞炴ｅ父 */
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
    if (weight > -1000.0f) {   /* -9999=杞鎹㈡湭灏辩华锛屾暣甯ц烦杩囷紙鏄剧ず淇濇寔涓婁竴鏈夋晥鍊硷級 */
        g_sys.weight_g = (int32_t)(weight + ((weight >= 0.0f) ? 0.5f : -0.5f));

        /* 鑷鍔ㄩ噸鍘荤毊浠呭厑璁稿湪寮鏈哄缓绋崇獥鍙ｏ紙6~25s銆佺┖闂茬姸鎬侊級锛
         * 鐑樺共涓鐢垫ˉ娓╂紓浼氱紦鎱㈣秺杩 卤闃堝硷紝鑻ヤ笉闄愭椂娈碉紝鐑樺埌涓鍗婁細鎶
         * 鐩樺唴鐪熷疄鐗╂枡锛堝 240g锛夎繛鍚屾紓绉讳竴璧"娓呴浂"銆 */
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
    /* 鏃犳晥璇绘暟(-9999=杞鎹㈡湭灏辩华)涓㈠純锛氫繚鎸佷笂涓鏄剧ず鍊硷紝涓嶈Е鍙戝幓鐨 */

    ptc_raw = NTC_GetTemperature();
    g_sys.ptc_temp = (float)ptc_raw / 10.0f;

    /* 绉伴噸娓╁害鍒嗘佃ˉ鍋匡細鐢垫ˉ娓╂紓浣块噸閲忛殢娓╁害婕傜Щ銆備互 25鈩 涓哄熀鍑嗭紝
     * 鎸夊綋鍓 NTC(鐑樼剻鑵/鏈轰綋)娓╁害鏌ュ垎娈电郴鏁板归噸閲忎慨姝ｃ
     * 绯绘暟闇瀹炴祴鏍囧畾锛氭瘡娈 = 姣忊剝 鐨勯噸閲忎慨姝ｆ瘮渚嬶紙鍙姝ｅ彲璐燂級銆
     * 渚嬶細0鈩儈15鈩 绯绘暟 -0.0004 鈫 10鈩 鏃朵慨姝 weight*(1-0.0004*(25-10))=0.994鍊 */
    {
        float tc;
        float t = g_sys.ptc_temp;
        if (t < 15.0f)       tc = -0.0004f;   /* 浣庢俯娈 */
        else if (t < 35.0f)  tc = 0.0f;       /* 甯告俯娈碉紙鍩哄噯锛 */
        else                 tc = 0.0004f;    /* 楂樻俯娈 */
        if (g_sys.weight_g != 0) {
            float comp = (float)g_sys.weight_g * (1.0f + tc * (t - 25.0f));
            g_sys.weight_g = (int32_t)(comp + ((comp >= 0.0f) ? 0.5f : -0.5f));
        }
    }

    /* NTC 瀵?鐠???顓＄熅瀵?鐢闈????鐘????鐗??缁旑垰?纭?澶?鐔恍??鎴?澶?銊?婵??
     * ??鎰?瀣?鏇??濞夈劑????鎴炴弓?甯寸圭偤????鐘?顓??閿????濞村娅?妞勭粻陇?钘夋儊濮?鐢鍛?鎾??閿涙稒?瀣?鏇?搴?銏?宥??
     * if (ptc_raw <= -100 || ptc_raw >= 2000) {
     *     if (g_sys.drying_active && g_sys.safety_state == SAFETY_NONE) {
     *         trigger_safety(SAFETY_BOX_BROKEN);
     *     }
     * }
     */
}

static void update_rgb(void)
{
    /* 涓婄數寮灞忓墠淇濇寔鐏鏉＄唲鐏锛氳儗鍏夋笎浜锛坰_rgb_ready=1锛夊悗鎵嶅紑濮嬭蛋鐏鏁堬紝
     * 閬垮厤"灞忓箷杩橀粦鐫鐏鏉″氨鍍电‖浜璧"銆 */
    if (!s_rgb_ready) return;    /* 鑳屽厜娓愪寒鍓嶇伅鏉′繚鎸佺唲鐏 */
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
        /* 鐎瑰本?鎰瀹抽敍姘?婵?瀣?銊?顓?宀?蹇?鎺曨吀?妞傚В蹇?灞?? 14% ??閫涘瘨娑?妫版?宀?鎾?鐔?銊ゅ瘨 */
        uint8_t pct = 0;
        if (g_sys.params.dry_time_sec > 0)
            pct = (uint8_t)(100U - g_sys.remaining_sec * 100U / g_sys.params.dry_time_sec);
        if (pct > 100) pct = 100;
        RGB_Progress_DryingBar(pct);
        complete_start = 0;
    } else if (g_sys.run_state == STATE_COOLING || g_sys.run_state == STATE_COMPLETE) {
        /* 鐑樺共璁℃椂缁撴潫锛堣繘鍏ュ喎鍗达級鍗宠嗕负瀹屾垚浜缁匡紝鍐峰嵈鍒颁綅鍚庝繚鎸 */
        if (complete_start == 0) complete_start = SystemTime_Millis();
        if (SystemTime_Millis() - complete_start < 30000) {
            RGB_Status_Green();
        } else {
            RGB_Status_Off();
        }
        RGB_Progress_Rainbow();
    } else {
        /* 缁屾椽妫介敍姝匓6 ??顓?瀛瑽7 瑜扳晞娅ｅù??濮 */
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
    Fan_SetSpeed(100);                       /* 妞嬪孩????鎺?銊?鐔?? */
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
    g_sys.remaining_sec = g_sys.params.dry_time_sec;   /* 鍋滄㈠悗鍓╀綑鏃堕暱褰掓暣涓鸿惧畾鏃堕暱锛岄伩鍏嶇綉椤/鐣岄潰娈嬬暀鍊掕℃椂 */
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

/* 鑵斾綋娓╁崌瀹夊叏鐩戞祴锛堝规瘮 SHT40 绌烘皵娓╁害 current_temp锛夛細
 * 閿氬畾鍒濆嬬偣 anchor = min(鍒濆嬫俯搴, 35鈩)銆
 * 鍐峰惎鍔(鍒濆嬧墹35)锛氭娴嬬獥鍙 1 鍒嗛挓锛岄』娓╁崌 鈮0.2鈩 鎵嶅垽鍔犵儹姝ｅ父锛
 * 鐑鍚鍔(鍒濆>35)锛氳存槑瓒佺卞唴浣欐俯缁х画鐑樺共锛屽彧闇妫娴嬫俯鍗囷紝2 鍒嗛挓鍐呴』娓╁崌 鈮1.0鈩冦
 * 娓╁崌鐩戞祴鍊煎彇鏈澶у奸攣瀛橈紙鍙楂樹笉浣庯級锛屾护闄 SHT40 0.1~0.2鈩 鐨勮绘暟鍥炶烦銆
 * 鍐峰惎鍔ㄥ叏绋嬶細娓╁害璺岀牬鍒濆嬮敋鐐癸紙杩炲抚闃叉姈锛夆啋 鍒ゆ晠闅滐紙鍔犵儹澶辨晥/浼犳劅鍣ㄥ紓甯革級銆 */
static uint8_t  rise_mon_active = 0;   /* 鏈娆＄儤骞插懆鏈熺洃娴嬫槸鍚﹀凡鍒濆嬪寲 */
static float    rise_initial = 0.0f;   /* 鍒濆嬫俯搴︼紙瀹為檯璇绘暟锛屼綔娓╁崌鍩哄噯锛 */
static float    rise_anchor  = 0.0f;   /* 閿氱偣 = min(鍒濆嬫俯搴, 35) */
static float    rise_peak    = 0.0f;   /* 鏈澶у奸攣瀛橈細鍙楂樹笉浣 */
static uint32_t rise_start_ms = 0;
static uint8_t  rise_hot = 0;          /* 鍒濆嬫俯搴 > 35鈩 鐑鍚鍔 */
static uint8_t  rise_confirmed = 0;    /* 绐楀彛鍐呮俯鍗囪揪鏍囷紝纭璁ゅ姞鐑鏈夋晥 */
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
    /* 宸插勪簬鐩鏍囬檮杩(<3鈩冨樊璺)锛氭俯鍗囬棬闄愬け鍘绘剰涔夛紙绌烘皵鐜浼氬湪璁惧畾鐐归檮杩戠紦鎱㈤艰繎銆
     * 鎽嗗姩 卤0.3鈩冿級锛屾ゆ椂鎸"绗1娆℃娴嬪凡閫氳繃"澶勭悊锛屼粎淇濈暀璺岃惤淇濇姢锛岄伩鍏嶆渶鍚 1鈩
     * 缁存寔杩囨參琚璇鍒"闀挎椂闂翠笉涓婂崌鈫掑畨鍏ㄨ﹀憡"銆 */
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

    /* 棣栨¤繘鍏ユ湰鍛ㄦ湡锛氶敋瀹氬垵濮嬬偣 */
    if (!rise_mon_active) { rise_monitor_start(); return; }

    /* 妫娴嬪兼渶澶у奸攣瀛橈紙鍙楂樹笉浣庯級锛屾护浼犳劅鍣ㄥ洖璺 */
    if (g_sys.current_temp > rise_peak) rise_peak = g_sys.current_temp;

    if (rise_hot) {
        /* 鐑鍚鍔锛氬彧妫娴嬫俯鍗囷紝绐楀彛 2 鍒嗛挓椤绘俯鍗 鈮1.0鈩 */
        if (!rise_confirmed && (int32_t)(now - rise_start_ms) >= (int32_t)TEMP_RISE_HOT_WINDOW_MS) {
            if (rise_peak - rise_initial < TEMP_RISE_HOT_MIN) { trigger_safety(SAFETY_LID_OPEN); return; }
            rise_confirmed = 1;
        }
        /* 鐑鎬佽穼钀戒繚鎶わ細宄板煎洖钀 >3鈩冿紙鐢ㄦ埛瑕佹眰锛夆啋 绠变綋鐮存崯/寮鐩栥
         * 宄板艰捣姝=鍒濆嬫俯搴︼紝闇鍏堜笂鍗囧悗鍥炶惤锛屽ぉ鐒朵笉浼氬湪鍚鍔ㄩ樁娈佃瑙﹀彂銆 */
        if (rise_peak - g_sys.current_temp >= TEMP_HOT_DROP_DELTA) {
            if (++rise_drop_cnt >= TEMP_DROP_DEBOUNCE) { trigger_safety(SAFETY_BOX_BROKEN); return; }
        } else {
            rise_drop_cnt = 0;
        }
    } else {
        /* 鍐峰惎鍔锛氱獥鍙 1 鍒嗛挓椤绘俯鍗 鈮0.2鈩冿紙绗1娆℃娴嬶級 */
        if (!rise_confirmed && (int32_t)(now - rise_start_ms) >= (int32_t)TEMP_RISE_WINDOW_MS) {
            if (rise_peak - rise_initial < TEMP_RISE_MIN) { trigger_safety(SAFETY_LID_OPEN); return; }
            rise_confirmed = 1;   /* 绗1娆℃娴嬮氳繃锛氬姞鐑鏈夋晥 */
        }
        /* 鍏ㄧ▼鍥炶惤妫娴嬶細鏄捐憲璺岀牬鍒濆嬮敋鐐癸紙1鈩冭曞害澶栵紝杩炲抚闃叉姈锛夆啋 鍒ょ变綋鐮存崯銆
         * 浠呭姞鐑宸茬‘璁(rise_confirmed)鍚庡惎鐢锛氬惎鍔ㄩ樁娈甸庢満鍐烽庡惞+娈嬩綑鐑浣胯绘暟鍏堣穼鍚庡崌锛
         * 鑻ユ湭纭璁ゅ氨鍒よ穼钀戒細"鍒氱儤骞插氨绠变綋鐮存崯"锛涚‘璁ゅ悗鐨勭湡瀹炲ぇ璺岃惤(寮鐩/婕忕儹)浠嶈兘鎶撲綇銆 */
        if (rise_confirmed && g_sys.current_temp < rise_anchor - TEMP_DROP_MARGIN) {
            if (++rise_drop_cnt >= TEMP_DROP_DEBOUNCE) { trigger_safety(SAFETY_BOX_BROKEN); return; }
        } else {
            rise_drop_cnt = 0;
        }
    }
}

/* ???PID閿涙氨鈹栧樻梹淇鎼?PID閿?娑撶粯甯堕敍???宥?? SHT40閿? ??鐘?顓炴珤濞撯晛??PID閿?娣囨繃濮㈤敍???宥?? NTC閿?*/
static float pid_air_int = 0.0f, pid_air_prev = 0.0f;
static uint32_t pid_air_tick = 0;
static float pid_ntc_int = 0.0f, pid_ntc_prev = 0.0f;
static uint32_t pid_ntc_tick = 0;

static void pid_reset(void)
{
    pid_air_int = 0.0f; pid_air_prev = 0.0f; pid_air_tick = 0U;
    pid_ntc_int = 0.0f; pid_ntc_prev = 0.0f; pid_ntc_tick = 0U;
}

/* 閫氱敤 PID 姝ヨ繘锛氳緭鍑 0-100 鐧惧垎姣旓紙setpoint=鐩鏍囷紝measure=琚娴嬫俯锛夈
 * 鍙嶇Н鍒嗛ケ鍜(back-calculation)锛氳緭鍑哄埌椤/搴曟椂鎶婅秴鍑洪噺鍗虫椂浠庣Н鍒嗗嵏鎺夆斺
 * 鍚﹀垯鍗囨俯娈电Н鍒嗛挸鍦 +50锛屽厓浠跺埌杈 ptc_max 鍚庝粛琚娈嬩綑绉鍒嗘帹鐫澶氱儳锛堝疄娴嬪啿鍒 95鈩冿級锛
 * 骞跺湪闄愬奸檮杩 0鈫25% 鍙嶅嶆í璺炽傚嵏绉鍒嗗悗鎺ヨ繎闄愬煎姛鐜囧钩婊戝綊闆躲 */
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
    float target = (float)g_sys.params.target_temp;      /* 缁岀儤?鏃傛窗???濞撯晛瀹 */
    float ntc_max = (float)g_sys.params.ptc_max_temp;    /* ??鐘?顓炴珤娑撳﹪?鎰淇鎼?NTC) */
    static uint32_t last_tick = 0;
    uint32_t now = SystemTime_Millis();

    /* ?瀚缁斿鈥栨潻?濞撯晙?婵囧Б閿涙瓍TC 濞撯晛瀹崇搾???鎰?瀣宓????鏌??鐘?顓?灞?宥?婵?鏍ㄥ付??鍓佸Ц??????
     * ??鎰?瀣?鏇??濞夈劑????鎴炴弓?甯寸圭偤????鐘?顓??閿????濞村娅?妞勭粻陇?钘夋儊濮?鐢鍛?鎾????鏂??
     * ??鍨?? NTC ??顓??鐠囶垵顕(??閿????顒傗敄)鐏忚鲸?? PTC ????鏌囬獮璺鸿剨??濠咁劅鐏炲繈??濞村?鏇?搴?銏?宥??
     * if (g_sys.drying_active && NTC_IsOverTemp()) {
     *     PTC_SetPower(0);
     *     trigger_safety(SAFETY_BOX_BROKEN);
     *     return;
     * }
     */

    if (g_sys.safety_state != SAFETY_NONE) { PTC_SetPower(0); return; }

    /* PID 鑷鏁村畾锛氫紭鍏堥┍鍔ㄧ姸鎬佹満锛屽苟鍚屾ヨ繘搴﹀埌 g_sys 渚涚晫闈㈡樉绀猴紙鍘燂細杩囩▼鍑芥暟浠庢湭琚璋冪敤鈫0%锛 */
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
        /* ??妯哄叡??銊?瀣?搴????鎺?銊?鐔??閿涙艾??婵瀣宓 100%閿涘奔?? PTC/NTC 濞撯晛瀹??鐘?? */
        Fan_SetSpeed(100);
        /* 缁岀儤?? PID閿涙碍?? SHT40 缁岀儤?鏃淇鎼达附?澶?鎵娲???閿?鐠囶垰妯婃径褉??00% 韫囶偊?鐔??濞撯晪?澶??
         * NTC PID閿涙碍?濠?鐘?顓炴珤濞撯晛瀹??鎰?璺烘躬 ptc_max_temp 娴犮儱??閿涘湤TC ??棰?濠?鎰?鎺?瀣?鐔??閿涘??
         * PTC ??鏍﹁⒈???鏉?鐏忓繐?纭?姘?鏃鐥??鎵娲???鐏忚鲸寮??鐔??閿???鐘?顓炴珤?甯存潻鎴?濠?鎰姘??鎰?鐔??*/
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
            /* 鍏冧欢纭椤朵繚鎶わ細瓒 ptc_max+10鈩 鐩存帴鏂鍔熺巼锛屼笉渚濊禆 PID 鏀舵暃
             * 锛堟姉绉鍒嗛ケ鍜屽悗鐨勬畫浣欑儹鎯鎬ф粦琛屽厹搴曪紝闃叉"蹇鍒扮┖姘旂洰鏍囧嵈鐑у埌95鈩+"锛 */
            if (g_sys.ptc_temp > (float)g_sys.params.ptc_max_temp + 10.0f) pwr = 0U;
            PTC_SetPower(pwr);
        }

        if (g_sys.run_state == STATE_HEATING && g_sys.current_temp >= target - 0.5f) {
            g_sys.run_state = STATE_DRYING;   /* 缁岀儤?鏃囨彧?娲??????瀵?婵瀣?鎺撲刊??鎺曨吀???*/
            last_tick = now;
        }
        if (g_sys.run_state == STATE_DRYING) {
            /* 鍊掕℃椂锛氬浐瀹氭ヨ繘 +1000ms 骞剁粨杞浣欓噺銆
             * 鍘熼昏緫 last_tick = now 姣忕掍涪寮 0~(tick鍛ㄦ湡) 鐨勪笉瓒充竴绉掍綑鏁帮紝
             * 闀挎椂闂磋繍琛屽掕℃椂鏄庢樉鍋忔參锛>5s 鏂妗ｏ紙鏆傚仠绛夛級鍙閲嶆柊閿氬畾锛屼笉杩炴墸琛ュ伩銆 */
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
        /* 缁屾椽妫/鐎瑰本?鎰濮???閿涙碍??缂侇厽淇鎼达箑?澶?銊?鏂?鏂垮涧濡?濞?NTC 鐡?鏉???宄板祱濞撯晛瀹 */
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