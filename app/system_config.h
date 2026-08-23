#ifndef __SYSTEM_CONFIG_H
#define __SYSTEM_CONFIG_H

#include "shared_defs.h"
#include "stm32f10x.h"

#define SYS_CLOCK_FREQ          72000000
#define TICK_FREQ               1000

#define TEMP_MIN                30
#define TEMP_MAX                80
#define TEMP_DEFAULT            50

#define TIME_MAX_SEC            172799
#define TIME_DEFAULT_SEC        7200

#define PTC_TEMP_MIN            40
#define PTC_TEMP_MAX            160
#define PTC_TEMP_DEFAULT        70
#define PTC_COOLING_TEMP_DEFAULT 40
#define PTC_TEMP_RISE_MAX       80

#define OTA_CHUNK_SIZE          1024
#define OTA_TIMEOUT_MS          30000

#define WIFI_AP_SSID            "Dryer_001"
#define WIFI_AP_PASS            "12345678"
#define WIFI_AP_CHANNEL         5
#define WEB_SERVER_PORT         8080

#define APP_VERSION             "0.1.0"
#define DEV_NAME                "lianyan"
#define DEV_SHELL               "-e-"

typedef enum {
    SCREEN_MAIN,
    SCREEN_WEIGHT,
    SCREEN_TEMP_ADJUST,
    SCREEN_TEMP_EDIT,
    SCREEN_TEMP_PID,
    SCREEN_TIME_ADJUST,
    SCREEN_TIME_EDIT,
    SCREEN_PTC_ADJUST,
    SCREEN_PTC_EDIT,
    SCREEN_PTC_COOLING_EDIT,
    SCREEN_PID_AUTOTUNE,
    SCREEN_MENU,
    SCREEN_MOTOR_ADJUST,
    SCREEN_MOTOR_EDIT,
    SCREEN_ABOUT,
    SCREEN_WIFI,
    SCREEN_OTA,
    SCREEN_SAFETY_ALERT,
    SCREEN_SETTINGS,
} Screen_t;

typedef enum {
    STATE_IDLE,
    STATE_HEATING,
    STATE_DRYING,
    STATE_PAUSED,
    STATE_COOLING,
    STATE_COMPLETE,
} RunState_t;

typedef enum {
    TIME_DIGIT_H1,
    TIME_DIGIT_H2,
    TIME_DIGIT_M1,
    TIME_DIGIT_M2,
    TIME_DIGIT_S1,
    TIME_DIGIT_S2,
    TIME_DIGIT_COUNT,
} TimeField_t;

typedef enum {
    SAFETY_NONE,
    SAFETY_BOX_BROKEN,
    SAFETY_LID_OPEN,
} SafetyState_t;

typedef enum {
    MOTOR_DRIVER_A4988,
    MOTOR_DRIVER_TMC2209,
} MotorDriver_t;

typedef struct {
    uint16_t target_temp;
    uint32_t dry_time_sec;
    uint16_t ptc_max_temp;
    uint16_t ptc_cooling_temp;
    float pid_kp;
    float pid_ki;
    float pid_kd;

    uint8_t motor_enabled;
    uint8_t motor_direction;
    uint8_t motor_speed;              // rpm/s
    uint8_t motor_oscillate;
    uint16_t motor_oscillate_angle;   // 1-360 deg, effective max 30
    uint8_t motor_driver;
    uint8_t motor_current;            // x100 (0.2-0.6A)
    uint8_t motor_stealthchop;
    uint8_t rgb_enabled;              // RGB灯条总开关 1=开 0=关
} Params_t;

typedef struct {
    Params_t params;

    float current_temp;
    float current_humidity;
    float weight_g;
    float ptc_temp;
    uint32_t remaining_sec;

    RunState_t run_state;
    Screen_t current_screen;
    Screen_t prev_screen;
    uint8_t selected_item;
    uint8_t submenu_active;

    TimeField_t time_cursor;
    uint8_t temp_edit_active;
    uint8_t ptc_edit_active;

    uint8_t pid_autotune_running;
    uint8_t pid_autotune_progress;
    uint8_t temp_pid_running;
    uint8_t temp_pid_progress;

    uint8_t wifi_enabled;
    uint8_t wifi_connected;
    uint8_t wifi_ap_mode;
    char wifi_ip[16];

    uint8_t ota_downloading;
    uint8_t ota_download_done;
    uint32_t ota_received_size;
    uint32_t ota_total_size;
    uint8_t ota_progress;

    uint8_t complete_timer;
    uint32_t device_id;

    SafetyState_t safety_state;
    uint8_t fan_speed;
    float ptc_temp_last;
    float chamber_temp_last;
    uint32_t temp_stuck_start;
    uint8_t drying_active;
    uint8_t time_digits[6];
    uint8_t buzzer_link;    /* 0=关 1=开 */
    uint8_t buzzer_vol;     /* 0-10 */
    uint8_t light_switch;   /* 0=关 1=开 */
    uint8_t backlight;      /* 0-100 */
    uint8_t theme;          /* 0=亮色 1=暗色 */
    uint8_t scroll_offset;  /* 翻页滚动偏移 */
    uint8_t screen_off_timeout; /* 熄屏超时索引: 0=从不 1=1s 2=5s 3=10s 4=20s 5=30s 6=60s 7=120s 8=300s */
} SystemState_t;

extern SystemState_t g_sys;

void System_Init(void);
void System_TickHandler(void);
void System_LoadParams(void);
void System_SaveParams(void);
uint32_t System_GetDeviceId(void);
void StartDrying(void);
void StopDrying(void);
void PauseDrying(void);
void ResumeDrying(void);;

#endif