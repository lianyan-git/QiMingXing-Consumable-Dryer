#ifndef BOOTLOADER_BUILD
#include "system_config.h"
#include "shared_defs.h"
#include "bsp_w25q128.h"
#include <string.h>

#define PARAM_MAGIC     0x50415242
#define PARAM_EXT_ADDR  0x00FE0000

typedef struct {
    uint32_t magic;
    uint16_t target_temp;
    uint32_t dry_time_sec;
    uint16_t ptc_max_temp;
    uint16_t ptc_cooling_temp;
    float pid_kp;
    float pid_ki;
    float pid_kd;

    uint8_t motor_enabled;
    uint8_t motor_direction;
    uint8_t motor_speed;
    uint8_t motor_oscillate;
    uint16_t motor_oscillate_angle;
    uint8_t motor_driver;
    uint8_t motor_current;
    uint8_t motor_stealthchop;
    uint8_t rgb_enabled;
    uint8_t reserved[6];

    uint32_t checksum;
} SystemParams_t;

static uint32_t calc_checksum(const SystemParams_t *p)
{
    uint32_t sum = p->magic + p->target_temp + p->dry_time_sec
                 + p->ptc_max_temp + p->ptc_cooling_temp
                 + (uint32_t)(p->pid_kp * 100)
                 + (uint32_t)(p->pid_ki * 100) + (uint32_t)(p->pid_kd * 100)
                 + p->motor_enabled + p->motor_direction + p->motor_speed
                 + p->motor_oscillate + p->motor_oscillate_angle
                 + p->motor_driver + p->motor_current + p->motor_stealthchop
                 + p->rgb_enabled;
    return ~sum;
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
    System_LoadParams();
}

/* 参数语义范围校验：损坏/越界参数直接拒绝，使用默认值 */
static int params_valid(const SystemParams_t *p)
{
    if (p->target_temp < TEMP_MIN || p->target_temp > TEMP_MAX) return 0;
    if (p->dry_time_sec == 0 || p->dry_time_sec > TIME_MAX_SEC) return 0;
    if (p->ptc_max_temp < PTC_TEMP_MIN || p->ptc_max_temp > PTC_TEMP_MAX) return 0;
    if (p->ptc_cooling_temp >= p->ptc_max_temp) return 0;
    if (p->pid_kp < 0.0f || p->pid_kp > 100.0f) return 0;
    if (p->pid_ki < 0.0f || p->pid_ki > 100.0f) return 0;
    if (p->pid_kd < 0.0f || p->pid_kd > 100.0f) return 0;
    return 1;
}

void System_LoadParams(void)
{
    SystemParams_t params;
    uint8_t raw[sizeof(SystemParams_t)];

    if (W25Q128_Read(PARAM_EXT_ADDR, raw, sizeof(SystemParams_t)) != W25Q128_OK) {
        return;
    }
    memcpy(&params, raw, sizeof(SystemParams_t));

    if (params.magic == PARAM_MAGIC && calc_checksum(&params) == params.checksum
        && params_valid(&params)) {
        g_sys.params.target_temp = params.target_temp;
        g_sys.params.dry_time_sec = params.dry_time_sec;
        g_sys.params.ptc_max_temp = params.ptc_max_temp;
        g_sys.params.ptc_cooling_temp = params.ptc_cooling_temp;
        g_sys.params.pid_kp = params.pid_kp;
        g_sys.params.pid_ki = params.pid_ki;
        g_sys.params.pid_kd = params.pid_kd;

        g_sys.params.motor_enabled = params.motor_enabled;
        g_sys.params.motor_direction = params.motor_direction;
        g_sys.params.motor_speed = params.motor_speed;
        g_sys.params.motor_oscillate = params.motor_oscillate;
        g_sys.params.motor_oscillate_angle = params.motor_oscillate_angle;
        g_sys.params.motor_driver = params.motor_driver;
        g_sys.params.motor_current = params.motor_current;
        g_sys.params.motor_stealthchop = params.motor_stealthchop;
        g_sys.params.rgb_enabled = params.rgb_enabled;
    }
}

void System_SaveParams(void)
{
    SystemParams_t params;
    uint8_t raw[sizeof(SystemParams_t)];

    memset(&params, 0, sizeof(params));   /* 保留字节清零，避免写入不确定内容 */

    params.magic = PARAM_MAGIC;
    params.target_temp = g_sys.params.target_temp;
    params.dry_time_sec = g_sys.params.dry_time_sec;
    params.ptc_max_temp = g_sys.params.ptc_max_temp;
    params.ptc_cooling_temp = g_sys.params.ptc_cooling_temp;
    params.pid_kp = g_sys.params.pid_kp;
    params.pid_ki = g_sys.params.pid_ki;
    params.pid_kd = g_sys.params.pid_kd;

    params.motor_enabled = g_sys.params.motor_enabled;
    params.motor_direction = g_sys.params.motor_direction;
    params.motor_speed = g_sys.params.motor_speed;
    params.motor_oscillate = g_sys.params.motor_oscillate;
    params.motor_oscillate_angle = g_sys.params.motor_oscillate_angle;
    params.motor_driver = g_sys.params.motor_driver;
    params.motor_current = g_sys.params.motor_current;
    params.motor_stealthchop = g_sys.params.motor_stealthchop;
    params.rgb_enabled = g_sys.params.rgb_enabled;
    params.checksum = calc_checksum(&params);

    memcpy(raw, &params, sizeof(SystemParams_t));

    /* 先擦除再写，检查两者是否成功 */
    if (W25Q128_EraseSector(PARAM_EXT_ADDR) != W25Q128_OK) return;
    if (W25Q128_Write(PARAM_EXT_ADDR, raw, sizeof(SystemParams_t)) != W25Q128_OK) return;

    /* read-back 验证 */
    uint8_t verify[sizeof(SystemParams_t)];
    if (W25Q128_Read(PARAM_EXT_ADDR, verify, sizeof(SystemParams_t)) == W25Q128_OK) {
        if (memcmp(verify, raw, sizeof(SystemParams_t)) != 0) {
            /* 写入不一致，可重试一次 */
        }
    }
}

void System_TickHandler(void)
{
}
#endif /* BOOTLOADER_BUILD */

