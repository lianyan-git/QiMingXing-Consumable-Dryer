#include "bsp_spi1_bus.h"

#include "pin_config.h"
#include "system_time.h"
#include "stm32f10x.h"

static Spi1BusOwner_t spi1_owner = SPI1_BUS_OWNER_NONE;
static Spi1BusOwner_t spi1_selected = SPI1_BUS_OWNER_NONE;

static int elapsed(uint32_t start, uint32_t timeout_ms)
{
    return (uint32_t)(SystemTime_Millis() - start) >= timeout_ms;
}

Spi1BusStatus_t Spi1Bus_Init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;
    DMA_InitTypeDef dma;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_SPI1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    gpio.GPIO_Pin = PIN_SPI1_SCK_PIN | PIN_SPI1_MOSI_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = PIN_SPI1_MISO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = PIN_TFT_CS_PIN | PIN_W25Q128_CS_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &gpio);
    GPIO_SetBits(PIN_TFT_CS_PORT, PIN_TFT_CS_PIN);
    GPIO_SetBits(PIN_W25Q128_CS_PORT, PIN_W25Q128_CS_PIN);

    SPI_I2S_DeInit(SPI1);
    SPI_StructInit(&spi);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;   /* 72M/2=36MHz（F103 SPI 最高） */
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    /* SPI1_TX = DMA1_Channel3，仅 TFT 像素流使用 */
    DMA_DeInit(DMA1_Channel3);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    dma.DMA_MemoryBaseAddr = 0U;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;          /* 内存 → 外设 */
    dma.DMA_BufferSize = 0U;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode = DMA_Mode_Normal;
    dma.DMA_Priority = DMA_Priority_High;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);

    spi1_owner = SPI1_BUS_OWNER_NONE;
    spi1_selected = SPI1_BUS_OWNER_NONE;
    return SPI1_BUS_OK;
}

Spi1BusStatus_t Spi1Bus_Acquire(Spi1BusOwner_t owner, uint32_t timeout_ms)
{
    uint32_t start;

    if ((owner != SPI1_BUS_OWNER_TFT) && (owner != SPI1_BUS_OWNER_W25Q128)) {
        return SPI1_BUS_ERROR_ARGUMENT;
    }
    start = SystemTime_Millis();
    do {
        if (spi1_owner == SPI1_BUS_OWNER_NONE) {
            spi1_owner = owner;
            return SPI1_BUS_OK;
        }
    } while ((timeout_ms != 0U) && !elapsed(start, timeout_ms));
    return SPI1_BUS_ERROR_BUSY;
}

Spi1BusStatus_t Spi1Bus_Select(Spi1BusOwner_t owner)
{
    if ((spi1_owner != owner) || (spi1_selected != SPI1_BUS_OWNER_NONE)) {
        return SPI1_BUS_ERROR_STATE;
    }
    GPIO_SetBits(PIN_TFT_CS_PORT, PIN_TFT_CS_PIN);
    GPIO_SetBits(PIN_W25Q128_CS_PORT, PIN_W25Q128_CS_PIN);
    if (owner == SPI1_BUS_OWNER_TFT) {
        GPIO_ResetBits(PIN_TFT_CS_PORT, PIN_TFT_CS_PIN);
    } else if (owner == SPI1_BUS_OWNER_W25Q128) {
        GPIO_ResetBits(PIN_W25Q128_CS_PORT, PIN_W25Q128_CS_PIN);
    } else {
        return SPI1_BUS_ERROR_ARGUMENT;
    }
    spi1_selected = owner;
    return SPI1_BUS_OK;
}

Spi1BusStatus_t Spi1Bus_Transfer(const uint8_t *tx,
                                 uint8_t *rx,
                                 uint32_t length,
                                 uint32_t timeout_ms)
{
    uint32_t index;
    uint32_t start;
    uint16_t received;

    if ((spi1_selected == SPI1_BUS_OWNER_NONE) ||
        ((tx == 0) && (rx == 0) && (length != 0U))) {
        return SPI1_BUS_ERROR_STATE;
    }
    for (index = 0U; index < length; ++index) {
        start = SystemTime_Millis();
        while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) {
            if ((timeout_ms == 0U) || elapsed(start, timeout_ms)) {
                return SPI1_BUS_ERROR_TIMEOUT;
            }
        }
        SPI_I2S_SendData(SPI1, (tx != 0) ? tx[index] : UINT8_C(0xFF));
        start = SystemTime_Millis();
        while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) {
            if ((timeout_ms == 0U) || elapsed(start, timeout_ms)) {
                return SPI1_BUS_ERROR_TIMEOUT;
            }
        }
        received = SPI_I2S_ReceiveData(SPI1);
        if (rx != 0) {
            rx[index] = (uint8_t)received;
        }
    }
    return SPI1_BUS_OK;
}

Spi1BusStatus_t Spi1Bus_TransferDma(const uint8_t *tx,
                                    uint32_t length,
                                    uint32_t timeout_ms)
{
    uint32_t start;

    if ((spi1_selected == SPI1_BUS_OWNER_NONE) || (tx == 0) || (length == 0U)) {
        return SPI1_BUS_ERROR_STATE;
    }
    if (length > 0xFFFFU) {
        return SPI1_BUS_ERROR_ARGUMENT;
    }

    DMA_ClearFlag(DMA1_FLAG_TC3 | DMA1_FLAG_HT3 | DMA1_FLAG_TE3);
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA_SetCurrDataCounter(DMA1_Channel3, (uint16_t)length);
    DMA1_Channel3->CMAR = (uint32_t)tx;

    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
    DMA_Cmd(DMA1_Channel3, ENABLE);

    start = SystemTime_Millis();
    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET) {
        if (DMA_GetFlagStatus(DMA1_FLAG_TE3) != RESET) {
            SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
            DMA_Cmd(DMA1_Channel3, DISABLE);
            return SPI1_BUS_ERROR_BUSY;
        }
        if ((timeout_ms != 0U) && elapsed(start, timeout_ms)) {
            SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
            DMA_Cmd(DMA1_Channel3, DISABLE);
            return SPI1_BUS_ERROR_TIMEOUT;
        }
    }
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
    DMA_Cmd(DMA1_Channel3, DISABLE);

    /* 等最后字节从移位寄存器移出，避免 CS 提前拉高截断数据 */
    start = SystemTime_Millis();
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) {
        if ((timeout_ms != 0U) && elapsed(start, timeout_ms)) {
            return SPI1_BUS_ERROR_TIMEOUT;
        }
    }
    /* TX-only DMA 全程未读 RX，OVR 被置位。清 OVR（先读 SR 再读 DR）
     * 并丢弃 RXNE 残留，避免下次轮询传输读到脏数据。 */
    (void)SPI1->SR;
    (void)SPI1->DR;
    return SPI1_BUS_OK;
}

void Spi1Bus_Deselect(Spi1BusOwner_t owner)
{
    if ((spi1_owner == owner) && (spi1_selected == owner)) {
        if (owner == SPI1_BUS_OWNER_TFT) {
            GPIO_SetBits(PIN_TFT_CS_PORT, PIN_TFT_CS_PIN);
        } else {
            GPIO_SetBits(PIN_W25Q128_CS_PORT, PIN_W25Q128_CS_PIN);
        }
        spi1_selected = SPI1_BUS_OWNER_NONE;
    }
}

void Spi1Bus_Release(Spi1BusOwner_t owner)
{
    if (spi1_owner == owner) {
        Spi1Bus_Deselect(owner);
        spi1_owner = SPI1_BUS_OWNER_NONE;
    }
}

Spi1BusOwner_t Spi1Bus_GetOwner(void)
{
    return spi1_owner;
}
