/*
 * shared_defs.h
 * Bootloader和App共享的定义
 * 必须保持一致!
 */

#ifndef __SHARED_DEFS_H
#define __SHARED_DEFS_H

#include <stdint.h>
#include "platform_contract.h"

/*═════════════════════════════════════════════════════════════════════════════
 *  内部Flash分区定义 (STM32F103CBT6 = 128KB；C8T6 64KB 亦兼容，分区地址不变)
 *═════════════════════════════════════════════════════════════════════════════*/

#define FLASH_BASE_ADDR         PLATFORM_FLASH_BASE
#define FLASH_PAGE_SIZE         PLATFORM_FLASH_PAGE_SIZE
#define FLASH_TOTAL_SIZE        PLATFORM_FLASH_SIZE

// Bootloader区: 0x08000000 ~ 0x08002FFF (12KB)
#define BOOTLOADER_ADDR         PLATFORM_BOOT_ADDR
#define BOOTLOADER_SIZE         PLATFORM_BOOT_SIZE

// 标志区: 0x08003000 ~ 0x080033FF (1KB)
#define FLAG_SECTOR_ADDR        PLATFORM_FLAG_ADDR
#define FLAG_SECTOR_SIZE        PLATFORM_FLAG_SIZE

// APP主程序区: 0x08003400 ~ 0x0801FFFF (115KB, CBT6 占满128KB)
#define APP_ADDR                PLATFORM_APP_ADDR
#define APP_SIZE                PLATFORM_APP_SIZE

/*═════════════════════════════════════════════════════════════════════════════
 *  外部Flash (W25Q128) 分区定义 (16MB)
 *═════════════════════════════════════════════════════════════════════════════*/

#define EXT_FLASH_TOTAL_SIZE    PLATFORM_EXT_FLASH_SIZE
#define EXT_FLASH_BLOCK_SIZE    PLATFORM_EXT_SECTOR_SIZE

// LittleFS数据区: 0x000000 ~ 0xBFFFFF (12MB)
#define LITTLEFS_START_ADDR     PLATFORM_DATA_ADDR
#define LITTLEFS_SIZE           PLATFORM_DATA_SIZE
#define LITTLEFS_BLOCKS         (LITTLEFS_SIZE / EXT_FLASH_BLOCK_SIZE)  // 3072

// 固件升级暂存区: 0xC00000 ~ 0xFEFFFF (4MB - 64KB)
#define FIRMWARE_TEMP_ADDR      PLATFORM_FIRMWARE_ADDR
#define FIRMWARE_TEMP_SIZE      PLATFORM_FIRMWARE_SIZE

// 升级标志/版本信息区: 0xFF0000 ~ 0xFFFFFF (64KB)
#define UPGRADE_FLAG_EXT_ADDR   PLATFORM_METADATA_ADDR
#define UPGRADE_FLAG_EXT_SIZE   PLATFORM_METADATA_SIZE

/*═════════════════════════════════════════════════════════════════════════════
 *  升级标志结构 (双份保存，掉电安全)
 *═════════════════════════════════════════════════════════════════════════════*/

#define UPGRADE_MAGIC           0x5A5A5A5A
#define UPGRADE_MAGIC_BACKUP    0xA5A5A5A5

// 升级状态
#define UPGRADE_STATUS_IDLE         0   // 空闲
#define UPGRADE_STATUS_DOWNLOADING  1   // 下载中
#define UPGRADE_STATUS_DOWNLOADED   2   // 下载完成，等待升级
#define UPGRADE_STATUS_UPGRADING    3   // 升级中
#define UPGRADE_STATUS_DONE         4   // 升级完成
#define UPGRADE_STATUS_FAILED       5   // 升级失败
#define UPGRADE_STATUS_FORCE_BOOT   6   // 强制进入升级模式（上电长按编码器触发）

typedef struct {
    uint32_t magic;             // 魔数 0x5A5A5A5A
    uint32_t version;           // 固件版本号
    uint32_t firmware_size;     // 固件大小
    uint32_t firmware_crc32;    // CRC32校验
    uint32_t target_addr;       // 目标地址 (APP_ADDR)
    uint8_t  status;            // 升级状态
    uint8_t  reserved[3];
    uint32_t timestamp;         // 时间戳
    uint32_t checksum;          // 本结构校验和
} UpgradeFlag_t;

// 双份标志结构
typedef struct {
    UpgradeFlag_t flag_a;       // 主份
    UpgradeFlag_t flag_b;       // 备份份
} UpgradeFlagDual_t;

/*═════════════════════════════════════════════════════════════════════════════
 *  版本信息
 *═════════════════════════════════════════════════════════════════════════════*/

#define BOOTLOADER_VERSION      0x00010001  // V1.0.1
#define APP_VERSION_ADDR        (APP_ADDR + 0x100)  // APP版本号偏移

/*═════════════════════════════════════════════════════════════════════════════
 *  公共函数声明
 *═════════════════════════════════════════════════════════════════════════════*/

uint32_t CRC32_Calculate(uint8_t *data, uint32_t len);
uint32_t Flag_CalculateChecksum(UpgradeFlag_t *flag);
int      Flag_Verify(UpgradeFlag_t *flag);

#endif /* __SHARED_DEFS_H */
