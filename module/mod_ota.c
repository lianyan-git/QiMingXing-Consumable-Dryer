#ifndef BOOTLOADER_BUILD
/*
 * mod_ota.c
 * OTA 升级入口：只保留"写升级标志 + 软复位进 Bootloader"。
 * 旧的"先写外部 Flash 再升级"(OTA_ReceiveChunk/TriggerUpgrade/CRC32_Update 等)
 * 已被新二进制协议(esp_link → 内部 Flash → Bootloader)取代, 本轮瘦身移除。
 */
#include "mod_ota.h"
#include "system_config.h"
#include "upgrade_flag.h"
#include "flash_ops.h"
#include "system_time.h"
#include "stm32f10x.h"

/* 强制进入 Bootloader 下载模式（不刷写，直接开 AP 收固件）。
 * 用于上电长按编码器等场景。 */
void OTA_EnterBootloader(void) {
    UpgradeFlag_t flag = {
        .magic = UPGRADE_MAGIC,
        .version = 0x00020000,
        .firmware_size = 0,
        .firmware_crc32 = 0,
        .target_addr = APP_ADDR,
        .status = UPGRADE_STATUS_FORCE_BOOT,
        .timestamp = SystemTime_Millis(),
    };

    UpgradeFlag_Write(&flag);
    UpgradeFlag_WriteExt(&flag);

    NVIC_SystemReset();
}
#endif /* BOOTLOADER_BUILD */
