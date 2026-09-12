#ifndef BOOTLOADER_BUILD
#include "system_config.h"
#include "shared_defs.h"
#include "sfud_flash.h"
#include "sfud.h"
#include "system_time.h"
#include "board.h"
#include <string.h>

#define PARAM_MAGIC     0x50415242
#define PARAM_VERSION   2U   /* 结构布局版本：以后只在末尾追加字段并升版本号，重烧不覆盖旧参数 */
#define PARAM_EXT_ADDR  0x00FE0000

/* ── SFUD 参数存储封装（内置于本文件，确保符号参与链接） ── */
static sfud_flash *g_flash = NULL;

int SfudFlash_Init(void)
{
    sfud_err r = sfud_init();
    g_flash = sfud_get_device(SFUD_W25Q128_DEVICE_INDEX);
    if (r != SFUD_SUCCESS) return -1;
    if (!g_flash) return -1;
    sfud_write_status(g_flash, false, 0U);   /* 清写保护（BP位） */
    return 0;
}

void *SfudFlash_GetDevice(void)
{
    return g_flash;
}

int SfudFlash_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!g_flash || !buf) return -1;
    return (sfud_read(g_flash, addr, len, buf) == SFUD_SUCCESS) ? 0 : -1;
}

/* 单次 SPI 事务发裸命令（CS 由 sfud 端口按事务控制）。
 * 返回：0=成功 1=总线忙(TFT占用，稍后重试) -1=错误 */
static int sfud_raw_cmd(const uint8_t *tx, size_t txlen, uint8_t *rx, size_t rxlen)
{
    sfud_err e;
    if (!g_flash) return -1;
    e = g_flash->spi.wr(&g_flash->spi, tx, txlen, rx, rxlen);
    if (e == SFUD_SUCCESS) return 0;
    if (e == SFUD_ERR_TIMEOUT) return 1;
    return -1;
}

int SfudFlash_StartEraseSector(uint32_t addr)
{
    uint8_t cmd[4];
    int r;
    if (addr & 0xFFFU) return -1;              /* 4KB 对齐 */
    cmd[0] = 0x06;                             /* WREN */
    r = sfud_raw_cmd(cmd, 1, NULL, 0);
    if (r) return r;
    cmd[0] = 0x20;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)addr;
    return sfud_raw_cmd(cmd, 4, NULL, 0);
}

int SfudFlash_StartWritePage(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint8_t cmd[260];
    int r;
    if (!buf || len == 0U || len > 256U) return -1;
    if (addr & 0xFFU) return -1;               /* 256B 对齐 */
    cmd[0] = 0x06;                             /* WREN */
    r = sfud_raw_cmd(cmd, 1, NULL, 0);
    if (r) return r;
    cmd[0] = 0x02;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)addr;
    memcpy(&cmd[4], buf, len);
    return sfud_raw_cmd(cmd, 4U + len, NULL, 0);
}

int SfudFlash_ReadSR1(uint8_t *sr1)
{
    uint8_t cmd = 0x05;
    if (!sr1) return -1;
    return sfud_raw_cmd(&cmd, 1, sr1, 1);
}

int SfudFlash_EraseSectorBlocking(uint32_t addr)
{
    if (!g_flash) return -1;
    return (sfud_erase(g_flash, addr, g_flash->chip.erase_gran) == SFUD_SUCCESS) ? 0 : -1;
}

typedef struct {
    uint32_t magic;
    uint32_t version;   /* 固定偏移4：未来结构变更时按此版本做迁移，重烧不覆盖旧参数 */
    uint16_t target_temp;
    uint32_t dry_time_sec;
    uint16_t ptc_max_temp;
    uint16_t ptc_cooling_temp;
float pid_air_kp;
    float pid_air_ki;
    float pid_air_kd;
    float pid_ntc_kp;
    float pid_ntc_ki;
    float pid_ntc_kd;

    uint8_t motor_enabled;
    uint8_t motor_direction;
    uint8_t motor_speed;
    uint8_t motor_oscillate;
    uint16_t motor_oscillate_angle;
    uint8_t motor_driver;
    uint8_t motor_current;
    uint8_t motor_stealthchop;
    uint16_t motor_work_count;
    uint16_t motor_rest_sec;
    uint16_t motor_swing_cal;
    uint8_t rgb_enabled;
    uint8_t rgb_led_bright;
    uint8_t rgb_strip_bright;
    uint8_t buzzer_link;
    uint8_t buzzer_vol;
    uint8_t light_switch;
    uint8_t backlight;
    uint8_t theme;
    uint8_t screen_off_timeout;
    uint8_t wifi_enabled;
    uint8_t pid_calibrated;
    Preset_t presets[PRESET_MAX];
    uint8_t preset_count;
    uint8_t current_preset;
    uint8_t reserved[1];

    uint8_t can_enabled;
    uint8_t can_role;

    uint32_t checksum;
} SystemParams_t;

static uint32_t preset_sum(const Preset_t *p)
{
    uint8_t i;
    uint32_t s = p->temp + p->time_sec;
    for (i = 0; i < PRESET_NAME_MAX + 1; i++) s += (uint8_t)p->name[i];
    return s;
}

static uint32_t calc_checksum(const SystemParams_t *p)
{
    uint32_t sum = p->magic + p->version + p->target_temp + p->dry_time_sec
                 + p->ptc_max_temp + p->ptc_cooling_temp
                 + (uint32_t)(p->pid_air_kp * 100)
                 + (uint32_t)(p->pid_air_ki * 100) + (uint32_t)(p->pid_air_kd * 100)
                 + (uint32_t)(p->pid_ntc_kp * 100)
                 + (uint32_t)(p->pid_ntc_ki * 100) + (uint32_t)(p->pid_ntc_kd * 100)
                 + p->motor_enabled + p->motor_direction + p->motor_speed
                 + p->motor_oscillate + p->motor_oscillate_angle
                 + p->motor_driver + p->motor_current + p->motor_stealthchop
                 + p->motor_work_count + p->motor_rest_sec + p->motor_swing_cal
                 + p->rgb_enabled + p->rgb_led_bright + p->rgb_strip_bright
                 + p->buzzer_link + p->buzzer_vol + p->light_switch
                 + p->backlight + p->theme + p->screen_off_timeout
                 + p->wifi_enabled + p->pid_calibrated
                 + p->can_enabled + p->can_role
                 + p->preset_count + p->current_preset;
    uint8_t i;
    for (i = 0; i < PRESET_MAX; i++) sum += preset_sum(&p->presets[i]);
    return ~sum;
}

/* 将系统设置项快照写入待保存缓冲（供后台保存与工厂重置共用） */
static void params_snapshot(SystemParams_t *p)
{
    memset(p, 0, sizeof(*p));
    p->magic = PARAM_MAGIC;
    p->version = PARAM_VERSION;
    p->target_temp = g_sys.params.target_temp;
    p->dry_time_sec = g_sys.params.dry_time_sec;
    p->ptc_max_temp = g_sys.params.ptc_max_temp;
    p->ptc_cooling_temp = g_sys.params.ptc_cooling_temp;
    p->pid_air_kp = g_sys.params.pid_air_kp;
    p->pid_air_ki = g_sys.params.pid_air_ki;
    p->pid_air_kd = g_sys.params.pid_air_kd;
    p->pid_ntc_kp = g_sys.params.pid_ntc_kp;
    p->pid_ntc_ki = g_sys.params.pid_ntc_ki;
    p->pid_ntc_kd = g_sys.params.pid_ntc_kd;

    p->motor_enabled = g_sys.params.motor_enabled;
    p->motor_direction = g_sys.params.motor_direction;
    p->motor_speed = g_sys.params.motor_speed;
    p->motor_oscillate = g_sys.params.motor_oscillate;
    p->motor_oscillate_angle = g_sys.params.motor_oscillate_angle;
    p->motor_driver = g_sys.params.motor_driver;
    p->motor_current = g_sys.params.motor_current;
    p->motor_stealthchop = g_sys.params.motor_stealthchop;
    p->motor_work_count = g_sys.params.motor_work_count;
    p->motor_rest_sec = g_sys.params.motor_rest_sec;
    p->motor_swing_cal = g_sys.params.motor_swing_cal;
    p->rgb_enabled = g_sys.params.rgb_enabled;
    p->rgb_led_bright = g_sys.params.rgb_led_bright;
    p->rgb_strip_bright = g_sys.params.rgb_strip_bright;
    p->can_enabled = g_sys.params.can_enabled;
    p->can_role = g_sys.params.can_role;

    p->buzzer_link = g_sys.buzzer_link;
    p->buzzer_vol = g_sys.buzzer_vol;
    p->light_switch = g_sys.light_switch;
    p->backlight = g_sys.backlight;
    p->theme = g_sys.theme;
    p->screen_off_timeout = g_sys.screen_off_timeout;
    p->wifi_enabled = g_sys.wifi_enabled;
    p->pid_calibrated = g_sys.pid_calibrated;
    memcpy(p->presets, g_sys.params.presets, sizeof(g_sys.params.presets));
    p->preset_count = g_sys.params.preset_count;
    p->current_preset = g_sys.params.current_preset;
}

/* 从已校验的存储结构恢复到系统状态 */
static void params_restore(const SystemParams_t *p)
{
    g_sys.params.target_temp = p->target_temp;
    g_sys.params.dry_time_sec = p->dry_time_sec;
    g_sys.params.ptc_max_temp = p->ptc_max_temp;
    g_sys.params.ptc_cooling_temp = p->ptc_cooling_temp;
    g_sys.params.pid_air_kp = p->pid_air_kp;
    g_sys.params.pid_air_ki = p->pid_air_ki;
    g_sys.params.pid_air_kd = p->pid_air_kd;
    g_sys.params.pid_ntc_kp = p->pid_ntc_kp;
    g_sys.params.pid_ntc_ki = p->pid_ntc_ki;
    g_sys.params.pid_ntc_kd = p->pid_ntc_kd;

    g_sys.params.motor_enabled = p->motor_enabled;
    g_sys.params.motor_direction = p->motor_direction;
    g_sys.params.motor_speed = p->motor_speed;
    g_sys.params.motor_oscillate = p->motor_oscillate;
    g_sys.params.motor_oscillate_angle = p->motor_oscillate_angle;
    g_sys.params.motor_driver = p->motor_driver;
    g_sys.params.motor_current = p->motor_current;
    g_sys.params.motor_stealthchop = p->motor_stealthchop;
    g_sys.params.motor_work_count = p->motor_work_count;
    g_sys.params.motor_rest_sec = p->motor_rest_sec;
    g_sys.params.motor_swing_cal = p->motor_swing_cal;
    g_sys.params.rgb_enabled = p->rgb_enabled;
    g_sys.params.rgb_led_bright = p->rgb_led_bright;
    g_sys.params.rgb_strip_bright = p->rgb_strip_bright;
    g_sys.params.can_enabled = p->can_enabled;
    g_sys.params.can_role = p->can_role;

    g_sys.buzzer_link = p->buzzer_link;
    g_sys.buzzer_vol = p->buzzer_vol;
    g_sys.light_switch = p->light_switch;
    g_sys.backlight = p->backlight;
    g_sys.theme = p->theme;
    g_sys.screen_off_timeout = p->screen_off_timeout;
    g_sys.wifi_enabled = p->wifi_enabled;
    g_sys.pid_calibrated = p->pid_calibrated;
    memcpy(g_sys.params.presets, p->presets, sizeof(g_sys.params.presets));
    g_sys.params.preset_count = p->preset_count;
    g_sys.params.current_preset = p->current_preset;
}

uint32_t System_GetDeviceId(void)
{
    uint32_t id0 = *(__IO uint32_t*)0x1FFFF7E8;
    uint32_t id1 = *(__IO uint32_t*)0x1FFFF7EC;
    uint32_t id2 = *(__IO uint32_t*)0x1FFFF7F0;
    uint32_t raw = id0 ^ id1 ^ id2 ^ 0xA5A5A5A5;
    return (raw ^ (raw >> 16)) & 0xFFFFFFFF;
}

void System_Init(void)
{
    SfudFlash_Init();   /* SFUD 探测 W25Q128 + 清写保护 */
    System_LoadParams();
}

/* 参数语义范围校验：损坏/越界参数直接拒绝，使用默认值 */
static int params_valid(const SystemParams_t *p)
{
    if (p->target_temp < TEMP_MIN || p->target_temp > TEMP_MAX) return 0;
    if (p->dry_time_sec == 0 || p->dry_time_sec > TIME_MAX_SEC) return 0;
    if (p->ptc_max_temp < PTC_TEMP_MIN || p->ptc_max_temp > PTC_TEMP_MAX) return 0;
    if (p->ptc_cooling_temp >= p->ptc_max_temp) return 0;
    /* PID 范围：自整定结果 kp*Tu/8 可远超 100（烘干热惯性大），上限放宽到 1000；
     * !(x>=0&&x<=1000) 写法同时过滤 NaN，避免旧版>100即拒绝整套参数导致PID/预设被重置 */
    if (!(p->pid_air_kp >= 0.0f && p->pid_air_kp <= 1000.0f)) return 0;
    if (!(p->pid_air_ki >= 0.0f && p->pid_air_ki <= 1000.0f)) return 0;
    if (!(p->pid_air_kd >= 0.0f && p->pid_air_kd <= 1000.0f)) return 0;
    if (!(p->pid_ntc_kp >= 0.0f && p->pid_ntc_kp <= 1000.0f)) return 0;
    if (!(p->pid_ntc_ki >= 0.0f && p->pid_ntc_ki <= 1000.0f)) return 0;
    if (!(p->pid_ntc_kd >= 0.0f && p->pid_ntc_kd <= 1000.0f)) return 0;
    if (p->motor_work_count > 1000) return 0;
    if (p->motor_rest_sec > 600) return 0;
    if (p->motor_swing_cal < 100 || p->motor_swing_cal > 300) return 0;
    if (p->motor_driver > MOTOR_DRIVER_TMC2209) return 0;
    if (p->rgb_led_bright > 100 || p->rgb_strip_bright > 100) return 0;
    if (p->can_enabled > 1 || p->can_role > 1) return 0;
    if (p->buzzer_vol > 10 || p->backlight > 100 || p->theme > 1
        || p->screen_off_timeout > 8 || p->light_switch > 1
        || p->buzzer_link > 1 || p->wifi_enabled > 1 || p->pid_calibrated > 1) return 0;
    if (p->preset_count > PRESET_MAX || p->current_preset >= p->preset_count) return 0;
    return 1;
}

void System_LoadParams(void)
{
    SystemParams_t params;
    uint8_t raw[sizeof(SystemParams_t)];

    if (SfudFlash_Read(PARAM_EXT_ADDR, raw, sizeof(SystemParams_t)) != 0) {
        return;
    }
    memcpy(&params, raw, sizeof(SystemParams_t));

    if (params.magic == PARAM_MAGIC && params.version == PARAM_VERSION
        && calc_checksum(&params) == params.checksum
        && params_valid(&params)) {
        params_restore(&params);
    }
}

/* 参数保存：单击退出选项时同步写入外部Flash（见 System_RequestSave） */

/* 分页写入：W25Q128 页编程一次最多 256 字节且不能跨页，数据跨多页时逐页写并等 WIP。
 * 返回 0=成功 1=总线忙(已重试完) -1=硬错误 */
static int flash_write_pages(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint8_t sr1;
    int r, attempts;
    uint32_t off = 0;

    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > 256U) chunk = 256U;
        for (attempts = 0; attempts < 60; attempts++) {
            r = SfudFlash_StartWritePage(addr + off, buf + off, chunk);
            if (r == 0) break;
            if (r != 1) return -1;
        }
        if (attempts >= 60) return 1;
        {
            uint32_t t0 = SystemTime_Millis();
            for (;;) {
                Watchdog_Kick();
                if (SfudFlash_ReadSR1(&sr1) != 0) return -1;
                if (!(sr1 & 0x01U)) break;
                if ((uint32_t)(SystemTime_Millis() - t0) > 2000U) return 1;
            }
        }
        off += chunk;
    }
    return 0;
}

/* ── 后台非阻塞保存 ─────────────────────────────────────────────
 * 旧版 System_RequestSave 同步擦扇区+写页（擦除典型 ~0.4s，最坏 ~2s），
 * 主循环在此期间完全停摆：编码器/串口收行/ESP 链路/UI 全部冻结，
 * 网页滑块每步都触发保存时表现为"命令慢/卡/按钮无响应"。
 * 现改为：Request 只做 RAM 快照+置位，Poll 在主循环里分片执行
 * （发起擦除→等待 WIP→逐页写，每步只占 SPI 极短的命令事务）。
 * 进行中再来的请求：覆盖快照（pending 置位），本次保存完成后自动重存。*/
static SystemParams_t s_save_mem;          /* 待保存快照 */
static uint8_t  s_save_pending = 0;
static uint8_t  s_save_st      = 0;        /* 0=空闲 1=擦除等待 2=发写页命令 3=写页等待 */
static uint8_t  s_save_fails   = 0;
static uint16_t s_save_page    = 0;
static uint32_t s_save_t0      = 0;

#define SAVE_PAGES      ((uint16_t)((sizeof(SystemParams_t) + 255U) / 256U))
#define SAVE_ERASE_TMO  3000U
#define SAVE_PAGE_TMO   1500U
#define SAVE_MAX_FAILS  8U

/* ---- 外部 Flash 操作所有权（参数保存 / 曲线记录 共享，见 system_config.h 说明）---- */
static uint8_t s_fop_owner = 0;   /* 0=空闲 1=有人持有（操作者自发出命令起持有，直至 WIP 清零释放） */

uint8_t SysFlashOp_TryBegin(void)
{
    if (s_fop_owner) return 0;
    s_fop_owner = 1;
    return 1;
}

void SysFlashOp_Release(void)
{
    s_fop_owner = 0;
}

void System_RequestSave(void)
{
    SystemParams_t params;
    uint8_t raw[sizeof(SystemParams_t)];

    params_snapshot(&params);
    params.checksum = calc_checksum(&params);
    memcpy(raw, &params, sizeof(SystemParams_t));
    memcpy(&s_save_mem, raw, sizeof(SystemParams_t));
    s_save_pending = 1;
}

void System_PollSave(void)
{
    uint8_t sr1;
    int r;

    if (s_save_st == 0) {
        if (!s_save_pending) return;
        s_save_pending = 0;                                 /* 快照已在 s_save_mem：期间新请求会重新置位 */
        if (!SysFlashOp_TryBegin()) { s_save_pending = 1; return; }  /* 曲线记录等正持闪：下轮再试 */
        r = SfudFlash_StartEraseSector(PARAM_EXT_ADDR);     /* 发出命令起持有 */
        if (r == 1) { SysFlashOp_Release(); s_save_pending = 1; return; }  /* 总线忙(TFT占用)，未发命令：释放重试 */
        if (r != 0) { SysFlashOp_Release(); return; }       /* 硬错误 */
        s_save_fails = 0;
        s_save_st    = 1;                                    /* 持有中：等待擦除 */
        s_save_page  = 0;
        s_save_t0    = SystemTime_Millis();
        return;
    }

    if (s_save_st == 1) {                                    /* 持有中：等待擦除完成 */
        if (SystemTime_Millis() - s_save_t0 > SAVE_ERASE_TMO) {
            SysFlashOp_Release();
            s_save_st = 0;
            if (++s_save_fails >= SAVE_MAX_FAILS) s_save_pending = 0;
            return;
        }
        if (SfudFlash_ReadSR1(&sr1) != 0) return;            /* 读状态失败：下轮重试 */
        if (sr1 & 0x01U) return;                             /* WIP 仍在擦除 */
        SysFlashOp_Release();                                /* WIP 清零：释放 */
        s_save_st = 2;                                       /* 下轮重新获取并发改页写 */
        return;
    }

    if (s_save_st == 2) {                                    /* 未持有：获取→发当前页写命令 */
        if (!SysFlashOp_TryBegin()) return;                  /* 他人持有：下轮再试 */
        {
            uint32_t off = (uint32_t)s_save_page * 256U;
            uint32_t len = (uint32_t)sizeof(SystemParams_t) - off;
            if (len > 256U) len = 256U;
            r = SfudFlash_StartWritePage(PARAM_EXT_ADDR + off, (const uint8_t *)&s_save_mem + off, len);
        }
        if (r == 1) { SysFlashOp_Release(); return; }        /* 总线忙，未发命令：释放重试 */
        if (r != 0) {
            SysFlashOp_Release();
            s_save_st = 0;
            if (++s_save_fails >= SAVE_MAX_FAILS) s_save_pending = 0;
            return;
        }
        s_save_st = 3;                                       /* 持有中：等待页写 */
        s_save_t0 = SystemTime_Millis();
        return;
    }

    /* st == 3：持有中，等待页写入完成 */
    if (SystemTime_Millis() - s_save_t0 > SAVE_PAGE_TMO) {
        SysFlashOp_Release();
        s_save_st = 0;
        if (++s_save_fails >= SAVE_MAX_FAILS) s_save_pending = 0;
        return;
    }
    if (SfudFlash_ReadSR1(&sr1) != 0) return;
    if (sr1 & 0x01U) return;                                 /* WIP 仍在写 */
    SysFlashOp_Release();                                    /* WIP 清零：释放 */
    s_save_page++;
    if (s_save_page >= SAVE_PAGES) {
        s_save_st = 0;                                       /* 保存完成；期间有新请求则 pending 仍为1，下轮重存 */
        s_save_fails = 0;
    } else {
        s_save_st = 2;                                       /* 下一页 */
    }
}

/* 等待保存完成（重启前调用）——限时 5s，期间持续推进状态机并喂狗（擦除最坏 ~2s < IWDG 4s 窗口） */
void System_FlushSave(void)
{
    uint32_t t0 = SystemTime_Millis();
    while ((s_save_st != 0 || s_save_pending) && (SystemTime_Millis() - t0) < 5000U) {
        System_PollSave();
        Watchdog_Kick();
    }
}

void System_SaveParams(void)
{
    SystemParams_t params;
    uint8_t raw[sizeof(SystemParams_t)];

    params_snapshot(&params);
    params.checksum = calc_checksum(&params);
    memcpy(raw, &params, sizeof(SystemParams_t));

    /* 阻塞版保存（备用；正常保存请用 System_RequestSave 后台非阻塞） */
    {
        uint8_t sr1;
        if (SfudFlash_StartEraseSector(PARAM_EXT_ADDR)) return;
        for (;;) { if (SfudFlash_ReadSR1(&sr1)) return; if (!(sr1 & 0x01U)) break; }
        (void)flash_write_pages(PARAM_EXT_ADDR, raw, sizeof(SystemParams_t));
    }
}

void System_TickHandler(void)
{
}

/* 恢复出厂设置：清空外部flash中保存的参数 + 恢复内存默认值，不重启、不写回。
 * 下次开机因 flash 无有效数据将自动使用默认值。 */
void System_FactoryReset(void)
{
    SfudFlash_EraseSectorBlocking(SFUD_PARAM_SECTOR_ADDR);   /* 清空保存区（阻塞约数百ms，一次性可接受） */

    memset(&g_sys.params, 0, sizeof(g_sys.params));
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
    g_sys.params.motor_oscillate_angle = 60;
    g_sys.params.motor_driver = MOTOR_DRIVER_A4988;
    g_sys.params.motor_current = 2;
    g_sys.params.motor_stealthchop = 0;
    g_sys.params.motor_work_count = 0;
    g_sys.params.motor_rest_sec = 0;
    g_sys.params.motor_swing_cal = 120;
    g_sys.params.rgb_enabled = 1;
    g_sys.params.rgb_led_bright = 100;
    g_sys.params.rgb_strip_bright = 100;
    g_sys.buzzer_link = 1;
    g_sys.buzzer_vol = 5;
    g_sys.light_switch = 1;
    g_sys.backlight = 100;
    g_sys.theme = 1;
    g_sys.screen_off_timeout = 0;
    g_sys.wifi_enabled = 1;
    g_sys.pid_calibrated = 0;
    /* 预设恢复出厂默认：4 个内置 + 当前预设 PETG */
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
    }
}
#endif /* BOOTLOADER_BUILD */

