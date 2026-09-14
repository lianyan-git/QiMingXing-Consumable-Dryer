/*
 * upgrade_flag.c
 * 升级标志管理实现
 * 双份保存策略：内部Flash + 外部Flash
 */

#include "upgrade_flag.h"
#include "flash_ops.h"
#include "bsp_w25q128.h"
#include "shared_defs.h"
#include <string.h>

// 内部Flash标志区地址
#define FLAG_ADDR_INTERNAL  FLAG_SECTOR_ADDR

int UpgradeFlag_Read(UpgradeFlag_t *flag) {
    // 先读主份
    memcpy(flag, (void*)FLAG_ADDR_INTERNAL, sizeof(UpgradeFlag_t));

    if (Flag_Verify(flag)) {
        return 0;  // 主份有效
    }

    // 主份无效，读备份
    memcpy(flag, (void*)(FLAG_ADDR_INTERNAL + sizeof(UpgradeFlag_t)), sizeof(UpgradeFlag_t));

    if (Flag_Verify(flag)) {
        // 恢复主份
        Flash_ErasePage(FLAG_ADDR_INTERNAL);
        Flash_Write(FLAG_ADDR_INTERNAL, (uint8_t*)flag, sizeof(UpgradeFlag_t));
        return 0;
    }

    return -1;  // 都无效
}

int UpgradeFlag_Write(UpgradeFlag_t *flag) {
    // 计算校验和
    flag->checksum = Flag_CalculateChecksum(flag);

    // 擦除标志页
    Flash_ErasePage(FLAG_ADDR_INTERNAL);

    // 写入主份
    Flash_Write(FLAG_ADDR_INTERNAL, (uint8_t*)flag, sizeof(UpgradeFlag_t));

    // 写入备份份
    Flash_Write(FLAG_ADDR_INTERNAL + sizeof(UpgradeFlag_t), (uint8_t*)flag, sizeof(UpgradeFlag_t));

    return 0;
}

int UpgradeFlag_ReadExt(UpgradeFlag_t *flag) {
    uint8_t buf[sizeof(UpgradeFlag_t)];

    W25Q128_Read(UPGRADE_FLAG_EXT_ADDR, buf, sizeof(UpgradeFlag_t));
    memcpy(flag, buf, sizeof(UpgradeFlag_t));

    if (Flag_Verify(flag)) {
        return 0;
    }

    // 读备份
    W25Q128_Read(UPGRADE_FLAG_EXT_ADDR + sizeof(UpgradeFlag_t), buf, sizeof(UpgradeFlag_t));
    memcpy(flag, buf, sizeof(UpgradeFlag_t));

    return Flag_Verify(flag) ? 0 : -1;
}

int UpgradeFlag_WriteExt(UpgradeFlag_t *flag) {
    flag->checksum = Flag_CalculateChecksum(flag);

    (void)W25Q128_EraseSector(UPGRADE_FLAG_EXT_ADDR);
    (void)W25Q128_Write(UPGRADE_FLAG_EXT_ADDR, (uint8_t*)flag, sizeof(UpgradeFlag_t));

    (void)W25Q128_EraseSector(UPGRADE_FLAG_EXT_ADDR + EXT_FLASH_BLOCK_SIZE);
    (void)W25Q128_Write(UPGRADE_FLAG_EXT_ADDR + EXT_FLASH_BLOCK_SIZE,
                        (uint8_t*)flag, sizeof(UpgradeFlag_t));

    return 0;
}

uint32_t Flag_CalculateChecksum(UpgradeFlag_t *flag)
{
    uint32_t sum = 0;
    uint32_t *p = (uint32_t*)flag;
    uint8_t i;
    for (i = 0; i < (sizeof(UpgradeFlag_t) / 4) - 1; i++) {
        sum += p[i];
    }
    return ~sum + 1;
}

int Flag_Verify(UpgradeFlag_t *flag)
{
    if (flag->magic != UPGRADE_MAGIC && flag->magic != UPGRADE_MAGIC_BACKUP) return 0;
    uint32_t expected = flag->checksum;
    flag->checksum = 0;
    uint32_t calc = Flag_CalculateChecksum(flag);
    flag->checksum = expected;
    return (calc == expected) ? 1 : 0;
}

void UpgradeFlag_Clear(void) {
    UpgradeFlag_t flag = {0};
    flag.magic = UPGRADE_MAGIC;
    flag.status = UPGRADE_STATUS_IDLE;
    UpgradeFlag_Write(&flag);
    UpgradeFlag_WriteExt(&flag);
}
