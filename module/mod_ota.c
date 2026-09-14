#ifndef BOOTLOADER_BUILD
/*
 * mod_ota.c
 * OTA upgrade manager
 */
#include "mod_ota.h"
#include "system_config.h"
#include "shared_defs.h"
#include "bsp_w25q128.h"
#include "upgrade_flag.h"
#include "flash_ops.h"
#include "system_time.h"
#include "stm32f10x.h"
#include <string.h>

static uint32_t ota_write_addr = FIRMWARE_TEMP_ADDR;
static uint32_t ota_received = 0;
static uint8_t ota_buffer[256];
static uint8_t ota_buf_idx = 0;

static uint32_t CRC32_Update(uint32_t crc, uint8_t *data, uint32_t len);

void OTA_Init(void) {
    ota_write_addr = FIRMWARE_TEMP_ADDR;
    ota_received = 0;
    ota_buf_idx = 0;
}

void OTA_Process(void) {
}

void OTA_ReceiveChunk(uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        ota_buffer[ota_buf_idx++] = data[i];
        if (ota_buf_idx >= 256) {
            (void)W25Q128_Write(ota_write_addr, ota_buffer, 256U);
            ota_write_addr += 256;
            ota_buf_idx = 0;
        }
    }
    ota_received += len;
    if (g_sys.ota_total_size > 0) {
        g_sys.ota_progress = (ota_received * 100) / g_sys.ota_total_size;
    }
    if (ota_received >= g_sys.ota_total_size) {
        if (ota_buf_idx > 0) {
            (void)W25Q128_Write(ota_write_addr, ota_buffer, ota_buf_idx);
        }
        g_sys.ota_downloading = 0;
        g_sys.ota_download_done = 1;
        g_sys.ota_progress = 100;
    }
}

void OTA_TriggerUpgrade(void) {
    uint32_t crc = 0;
    uint8_t buf[256];
    for (uint32_t addr = FIRMWARE_TEMP_ADDR;
         addr < FIRMWARE_TEMP_ADDR + g_sys.ota_total_size;
         addr += 256) {
        uint32_t chunk = (g_sys.ota_total_size - (addr - FIRMWARE_TEMP_ADDR));
        if (chunk > 256) chunk = 256;
        (void)W25Q128_Read(addr, buf, chunk);
        crc = CRC32_Update(crc, buf, chunk);
    }

    UpgradeFlag_t flag = {
        .magic = UPGRADE_MAGIC,
        .version = 0x00020000,
        .firmware_size = g_sys.ota_total_size,
        .firmware_crc32 = crc,
        .target_addr = APP_ADDR,
        .status = UPGRADE_STATUS_DOWNLOADED,
        .timestamp = SystemTime_Millis(),
    };

    UpgradeFlag_Write(&flag);
    UpgradeFlag_WriteExt(&flag);

    NVIC_SystemReset();
}

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

static uint32_t CRC32_Update(uint32_t crc, uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}
#endif /* BOOTLOADER_BUILD */