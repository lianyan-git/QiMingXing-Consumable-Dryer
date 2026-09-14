/*
 * flash_ops.c
 * STM32内部Flash操作实现
 */

#include "flash_ops.h"
#include "shared_defs.h"
#include "stm32f10x.h"

int Flash_ErasePage(uint32_t addr) {
    FLASH_Status status;

    FLASH_Unlock();

    status = FLASH_ErasePage(addr);

    FLASH_Lock();

    return (status == FLASH_COMPLETE) ? 0 : -1;
}

int Flash_EraseAppArea(void) {
    FLASH_Status status;

    FLASH_Unlock();

    for (uint32_t addr = APP_ADDR; addr < APP_ADDR + APP_SIZE; addr += FLASH_PAGE_SIZE) {
        status = FLASH_ErasePage(addr);
        if (status != FLASH_COMPLETE) {
            FLASH_Lock();
            return -1;
        }
    }

    FLASH_Lock();
    return 0;
}

int Flash_Write(uint32_t addr, uint8_t *data, uint32_t len) {
    FLASH_Status status;
    uint32_t i;

    /* 保证按 4 字节写入，不足部分补 0xFF，避免越界读取 */
    uint32_t aligned_len = (len + 3) & ~3U;
    uint8_t tmp[4];

    FLASH_Unlock();

    for (i = 0; i < aligned_len; i += 4) {
        uint32_t word;
        uint32_t rem = (len > i) ? (len - i) : 0U;
        if (rem >= 4U) {
            word = data[i] | ((uint32_t)data[i+1] << 8) | ((uint32_t)data[i+2] << 16) | ((uint32_t)data[i+3] << 24);
        } else {
            uint32_t k;
            for (k = 0; k < 4U; k++) {
                tmp[k] = (k < rem) ? data[i + k] : 0xFF;
            }
            word = tmp[0] | ((uint32_t)tmp[1] << 8) | ((uint32_t)tmp[2] << 16) | ((uint32_t)tmp[3] << 24);
        }
        status = FLASH_ProgramWord(addr + i, word);
        if (status != FLASH_COMPLETE) {
            FLASH_Lock();
            return -1;
        }
    }

    FLASH_Lock();
    return 0;
}

int Flash_Read(uint32_t addr, uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        data[i] = *(__IO uint8_t*)(addr + i);
    }
    return 0;
}
