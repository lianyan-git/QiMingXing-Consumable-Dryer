/*
 * flash_ops.h
 * STM32内部Flash操作
 */

#ifndef __FLASH_OPS_H
#define __FLASH_OPS_H

#include <stdint.h>

int  Flash_ErasePage(uint32_t addr);
int  Flash_EraseAppArea(void);
int  Flash_Write(uint32_t addr, uint8_t *data, uint32_t len);
int  Flash_Read(uint32_t addr, uint8_t *data, uint32_t len);

#endif /* __FLASH_OPS_H */
