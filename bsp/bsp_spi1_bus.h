#ifndef BSP_SPI1_BUS_H
#define BSP_SPI1_BUS_H

#include <stdint.h>

typedef enum {
    SPI1_BUS_OWNER_NONE = 0,
    SPI1_BUS_OWNER_TFT = 1,
    SPI1_BUS_OWNER_W25Q128 = 2
} Spi1BusOwner_t;

typedef enum {
    SPI1_BUS_OK = 0,
    SPI1_BUS_ERROR_ARGUMENT = -1,
    SPI1_BUS_ERROR_BUSY = -2,
    SPI1_BUS_ERROR_TIMEOUT = -3,
    SPI1_BUS_ERROR_STATE = -4
} Spi1BusStatus_t;

Spi1BusStatus_t Spi1Bus_Init(void);
Spi1BusStatus_t Spi1Bus_Acquire(Spi1BusOwner_t owner, uint32_t timeout_ms);
Spi1BusStatus_t Spi1Bus_Select(Spi1BusOwner_t owner);
Spi1BusStatus_t Spi1Bus_Transfer(const uint8_t *tx,
                                 uint8_t *rx,
                                 uint32_t length,
                                 uint32_t timeout_ms);
/* TX-only DMA 发送（SPI1_TX = DMA1_Channel3）。调用前需已 Acquire+Select。
 * 同步等待 DMA 完成并保证 SPI 移位寄存器清空后才返回。 */
Spi1BusStatus_t Spi1Bus_TransferDma(const uint8_t *tx,
                                    uint32_t length,
                                    uint32_t timeout_ms);
void Spi1Bus_Deselect(Spi1BusOwner_t owner);
void Spi1Bus_Release(Spi1BusOwner_t owner);
Spi1BusOwner_t Spi1Bus_GetOwner(void);

#endif /* BSP_SPI1_BUS_H */
