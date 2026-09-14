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
#define PID_MANUAL_DEFAULT      10.0f   /* PID未校准时的相同默认值 */
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
    SCREEN_TEMP_PID,
    SCREEN_PID_ADJUST,
    SCREEN_PRESET,
    SCREEN_PRESET_LIST,
    SCREEN_PRESET_EDIT,
    SCREEN_TIME_ADJUST,
    SCREEN_PTC_ADJUST,
    SCREEN_PTC_EDIT,
    SCREEN_PTC_COOLING_EDIT,
    SCREEN_PID_AUTOTUNE,
    SCREEN_MENU,
    SCREEN_MOTOR_ADJUST,
    SCREEN_ABOUT,
    SCREEN_WIFI,
    SCREEN_OTA,
    SCREEN_SAFETY_ALERT,
    SCREEN_SETTINGS,
    SCREEN_CAN,
    SCREEN_MUSIC,        /* 音乐主页面（上传音乐 / 音乐列表 / 退出） */
    SCREEN_MUSIC_LIST,   /* 音乐列表（自适应滚动，含播放中进度与长名跑马灯） */
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
    MOTOR_DRIVER_TMC2208,
    MOTOR_DRIVER_TMC2209,
} MotorDriver_t;

#define PRESET_BUILTIN          4      /* 内置预设个数 */
#define PRESET_MAX              12     /* 预设总数上限（含内置） */
#define PRESET_NAME_MAX         8

typedef struct {
    char     name[PRESET_NAME_MAX + 1];  /* 大写字母+空格，≤8字符 */
    uint8_t  temp;                       /* 烘干温度 30-80℃ */
    uint32_t time_sec;                   /* 烘干时间 */
} Preset_t;

typedef struct {
    uint16_t target_temp;
    uint32_t dry_time_sec;
    uint16_t ptc_max_temp;
    uint16_t ptc_cooling_temp;
    float pid_air_kp;              /* 空气温度 PID */
    float pid_air_ki;
    float pid_air_kd;
    float pid_ntc_kp;              /* PTC 元件温度 PID（独立校准） */
    float pid_ntc_ki;
    float pid_ntc_kd;

    uint8_t motor_enabled;
    uint8_t motor_direction;
    uint8_t motor_speed;              // rpm/s
    uint8_t motor_oscillate;
    uint16_t motor_oscillate_angle;   // 平台摆动实际角度 1-360 deg (默认60)
    uint8_t motor_driver;
    uint8_t motor_current;            // x100 (0.2-0.6A)
    uint8_t motor_stealthchop;
    uint16_t motor_work_count;        // 工作次数 0-1000, 0=一直工作, 每工作N次休息
    uint16_t motor_rest_sec;          // 休息时长 0-600s, 0=不休息(工作次数非0才生效)
    uint16_t motor_swing_cal;         // 摆动校准 100-300% (默认120，对应基准505步/度), 用于修正传动/打滑
    uint8_t rgb_enabled;              // RGB灯条总开关 1=开 0=关
    uint8_t rgb_led_bright;           // 指示灯亮度 0-100
    uint8_t rgb_strip_bright;         // 灯条亮度 0-100

    uint8_t can_enabled;              // CAN通讯总开关 0=关 1=开
    uint8_t can_role;                 // 主从关系 0=主机 1=从机

    Preset_t presets[PRESET_MAX];   // 动态预设列表（前4个为内置，可增删）
    uint8_t preset_count;           // 当前预设个数
    uint8_t current_preset;         // 当前选中预设索引（默认1=PETG）
} Params_t;

typedef struct {
    Params_t params;

    float current_temp;
    float current_humidity;
    int32_t weight_g;            /* 重量(克)，int 变量，允许负数 */
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
    uint8_t time_edit_active;  /* 时间页：0=移动光标选位 1=编辑当前位数字 */
    uint8_t preset_time_edit;  /* 预设弹窗时间：0=移动光标选位 1=编辑当前位数字 */
    uint8_t mute_anim;         /* 静音开关滑动动画帧：0=无，1-6 关→开，7-12 开→关 */

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
    uint8_t scroll_offset;  /* 翻页滚动偏移(兼容保留，滚动改用 pixel_offset) */
    int16_t pixel_offset;   /* 像素滚动偏移：0 ~ (总项数*行高 - 可视区高度) */
    uint8_t menu_selected;      /* 进入子页前保存的菜单选中项，返回时恢复 */
    int16_t menu_pixel_offset;  /* 进入子页前保存的菜单滚动位置 */
    uint8_t screen_off_timeout; /* 熄屏超时索引: 0=从不 1=1s 2=5s 3=10s 4=20s 5=30s 6=60s 7=120s 8=300s */
    uint8_t pid_calibrated; /* PID是否已校准(手动调参后置1) 0=未校准 1=已校准 */
    uint8_t motor_edit_active; /* 电机页当前项是否处于编辑中 */
    uint8_t pid_edit_active;   /* PID调整页: 0=未编辑 1=KP编辑 2=KI编辑 3=KD编辑 */
    uint8_t pid_return_screen; /* PID调整页返回目标: 0=温度页 1=PTC页 */
    uint8_t settings_edit_active; /* 设置页当前项是否处于编辑中（同电机页交互） */
    uint8_t screen_off;       /* 1=屏幕已熄灭（编码器输入不产生任何动作） */
    uint8_t preset_edit_idx;  /* 正在编辑的预设索引 0-11（内置或新增） */
    uint8_t preset_row;       /* 预设编辑页当前行: 0=名称 1=温度 2=时间 3=保存并退出 4=退出 */
    uint8_t preset_row_edit;  /* 预设行编辑激活: 0=无 1=名称 2=温度 3=时间 */
    uint8_t preset_name_cur;  /* 名称编辑光标 0-7 */
    uint8_t preset_time_cur;  /* 时间编辑光标 0-5 */
    uint8_t preset_edit_new;  /* 1=新增预设模式（保存时追加到列表） */
    uint8_t preset_confirm;   /* 确认弹窗: 0=无 1=删除确认 2=退出保存确认 */
    uint8_t preset_confirm_yes; /* 确认选项: 0=否 1=是 */
    uint8_t preset_del_mode;    /* 预设删除模式：1=列出预设待删 */
    Preset_t preset_scratch;  /* 编辑前的预设备份（退出不保存时还原） */
    uint8_t time_popup;       /* 时间弹窗编辑激活（删除全屏时间编辑二级菜单） */
    uint8_t rgb_bright_popup; /* RGB亮度弹窗激活 */
    uint8_t rgb_bright_sel;   /* 0=指示灯 1=灯条 2=完成 */
    uint8_t rgb_bright_edit;  /* RGB亮度弹窗编辑态: 0=选参数 1=编辑数值 */
    uint8_t can_connected;    /* CAN 已连接从机数(主机视角，实时) */
    uint8_t can_joined;       /* CAN 从机是否已接入主机网络(从机视角) */
    uint8_t can_edit_active;  /* CAN 页编辑态: 0=选行 1=编辑 */
    uint8_t can_search_tick;  /* CAN 搜索提示帧计数 0=无 (旧字段, 不再使用) */
    uint8_t can_search_state; /* CAN 搜索状态机: 0=空闲 1=搜索中 2=发现设备 3=连接中 4=连接成功 5=未发现设备 6=请重试 */
    uint8_t can_search_cnt0;  /* 搜索开始时的已连接数(命中检测用) */
    uint32_t can_search_t0;   /* 状态进入时间(ms) */
    uint32_t can_search_last; /* 搜索中最近一次广播时间(ms) */
    uint8_t wifi_edit_active; /* WiFi开关编辑态: 0=选行 1=选中待确认(再单击退出才生效) */
    uint8_t wifi_edit_orig;   /* 进入WiFi开关编辑态时的原状态，退出时比较决定是否切换ESP */
    uint8_t ui_force_redraw;  /* 外部(网页命令等)请求整屏重绘的标志，UI_Update 消费后清零 */

    /* ---- 音乐 / 上传 ---- */
    uint8_t music_popup;      /* 上传音乐弹窗态: 0=无 1=已显示待开AP 2=AP已开/等待上传 3=上传中 4=完成 5=失败 */
    uint8_t music_ota_active; /* 1=音乐上传接收态（EspLink 字节路由到 MusicOta） */
    uint8_t music_upload_pct; /* 上传进度 0..100 */
    uint8_t music_track_play; /* 正在播放的曲目索引(0..n-1) 或 0xFF=未播放 */
    int16_t music_marquee;    /* 列表长名跑马灯水平偏移(px) */
    uint32_t music_upload_total;   /* 本次上传字节数（进度用） */
    uint32_t music_upload_recv;    /* 本次已收字节数 */
} SystemState_t;

extern SystemState_t g_sys;

void System_Init(void);
void System_TickHandler(void);
void System_LoadParams(void);
void System_SaveParams(void);
void System_RequestSave(void);
void System_PollSave(void);
void System_FlushSave(void);
void System_FactoryReset(void);

/* ---- 外部 Flash 串行操作协调 -------------------------------------------------
 * SPI NOR 在 WIP=1 期间会静默忽略（不排队）新操作命令。参数保存与曲线记录
 * 若交叉发起操作，后发命令被丢弃、而各自的 WIP 轮询又会误判“完成”，
 * 最终可能把数据写进未擦除的扇区。约定：操作者自“发出命令”起持有 Flash，
 * 直到“确认 WIP 清零”才释放；未持有者发起前必须先 TryBegin，失败则延迟重试。 */
uint8_t SysFlashOp_TryBegin(void);   /* 1=获取成功 0=他人持有（调用方应延迟重试） */
void    SysFlashOp_Release(void);    /* 只在确认 WIP 已清零（或放弃操作）后调用 */

uint32_t System_GetDeviceId(void);
void StartDrying(void);
void StopDrying(void);
void PauseDrying(void);
void ResumeDrying(void);;

#endif