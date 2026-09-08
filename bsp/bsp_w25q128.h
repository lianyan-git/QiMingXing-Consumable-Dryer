#ifndef BSP_W25Q128_H
#define BSP_W25Q128_H

#include <stdint.h>

typedef enum {
    W25Q128_OK = 0,
    W25Q128_ERROR_ARGUMENT = -1,
    W25Q128_ERROR_RANGE = -2,
    W25Q128_ERROR_BUS = -3,
    W25Q128_ERROR_TIMEOUT = -4,
    W25Q128_ERROR_ID = -5
} W25Q128_Status_t;

#define W25Q128_EXPECTED_JEDEC_ID UINT32_C(0xEF4018)
#define W25Q128_PAGE_SIZE         256U
#define W25Q128_SECTOR_SIZE       4096U

typedef void (*W25Q128_ServiceFn)(void);

W25Q128_Status_t W25Q128_Init(void);
W25Q128_Status_t W25Q128_GetJedecId(uint32_t *jedec_id);
W25Q128_Status_t W25Q128_Read(uint32_t address, uint8_t *buffer, uint32_t length);
W25Q128_Status_t W25Q128_Write(uint32_t address, const uint8_t *buffer, uint32_t length);
W25Q128_Status_t W25Q128_EraseSector(uint32_t address);
W25Q128_Status_t W25Q128_StartEraseSector(uint32_t address);
W25Q128_Status_t W25Q128_StartWritePage(uint32_t address, const uint8_t *buffer, uint32_t length);
W25Q128_Status_t W25Q128_EraseRange(uint32_t address, uint32_t length);
W25Q128_Status_t W25Q128_ClearProtection(void);
W25Q128_Status_t W25Q128_WREN(void);
W25Q128_Status_t W25Q128_ReadSR1(uint8_t *sr1);
void W25Q128_SetServiceCallback(W25Q128_ServiceFn service);

#endif /* BSP_W25Q128_H */
