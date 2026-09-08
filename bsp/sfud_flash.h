#ifndef BSP_SFUD_FLASH_H
#define BSP_SFUD_FLASH_H

#include <stdint.h>

/* 参数保存扇区（4KB 对齐） */
#define SFUD_PARAM_SECTOR_ADDR   0x00FE0000UL

/* 初始化 SFUD（探测 W25Q128）。返回 sfud_err */
int SfudFlash_Init(void);

/* 整块读 */
int SfudFlash_Read(uint32_t addr, uint8_t *buf, uint32_t len);

/* 非阻塞原语：只发命令不等待（配合 SfudFlash_ReadSR1 轮询，与 TFT 共用总线） */
int SfudFlash_StartEraseSector(uint32_t addr);
int SfudFlash_StartWritePage(uint32_t addr, const uint8_t *buf, uint32_t len);
int SfudFlash_ReadSR1(uint8_t *sr1);

/* 阻塞式整扇区擦除（恢复出厂用） */
int SfudFlash_EraseSectorBlocking(uint32_t addr);

/* 返回 SFUD 设备 */
void *SfudFlash_GetDevice(void);

#endif /* BSP_SFUD_FLASH_H */
