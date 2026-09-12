/*
 * bsp_can.c
 * CAN 总线驱动 (bxCAN1, PB8=RX / PB9=TX)
 * 500kbps 默认，标准帧(11bit ID)，滤波全收(FIFO0)，发送阻塞+超时，接收轮询。
 */
#ifndef BOOTLOADER_BUILD

#include "stm32f10x.h"
#include "stm32f10x_can.h"
#include "board.h"
#include "pin_config.h"
#include "bsp_can.h"

static volatile uint8_t s_can_ready = 0;

/* APB1=36MHz，固定 1+BS1+BS2 = 18 tq，分频 = 36000/(baud_kbps*18) */
static uint8_t prescaler_for_baud(uint32_t baud_kbps)
{
    uint32_t p = 36000U / (baud_kbps * 18U);
    if (p < 1U) p = 1U;
    if (p > 256U) p = 256U;
    return (uint8_t)p;
}

void BspCan_Init(uint32_t baud_kbps)
{
    GPIO_InitTypeDef gpio;
    CAN_InitTypeDef can;
    CAN_FilterInitTypeDef filter;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    /* PB8=RX, PB9=TX 复用推挽 */
    gpio.GPIO_Pin = PIN_CAN_RX_PIN | PIN_CAN_TX_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_CAN_RX_PORT, &gpio);

    CAN_DeInit(CAN1);
    CAN_StructInit(&can);
    can.CAN_TTCM = DISABLE;
    can.CAN_ABOM = ENABLE;          /* 总线关闭自动恢复 */
    can.CAN_AWUM = DISABLE;
    can.CAN_NART = DISABLE;         /* 发送错误自动重发 */
    can.CAN_RFLM = DISABLE;
    can.CAN_TXFP = DISABLE;         /* 按 ID 优先级仲裁 */
    can.CAN_Mode = CAN_Mode_Normal;
    can.CAN_SJW = CAN_SJW_1tq;
    can.CAN_BS1 = CAN_BS1_15tq;
    can.CAN_BS2 = CAN_BS2_2tq;
    can.CAN_Prescaler = prescaler_for_baud(baud_kbps ? baud_kbps : 500U);

    if (CAN_Init(CAN1, &can) != CAN_InitStatus_Success) {
        s_can_ready = 0;
        return;
    }

    /* 滤波器 0：全收（协议层自行按 ID 分发） */
    CAN_FilterInit(&filter);
    filter.CAN_FilterNumber = 0;
    filter.CAN_FilterMode = CAN_FilterMode_IdMask;
    filter.CAN_FilterScale = CAN_FilterScale_32bit;
    filter.CAN_FilterIdHigh = 0x0000U;
    filter.CAN_FilterIdLow = 0x0000U;
    filter.CAN_FilterMaskIdHigh = 0x0000U;
    filter.CAN_FilterMaskIdLow = 0x0000U;
    filter.CAN_FilterFIFOAssignment = CAN_FIFO0;
    filter.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&filter);

    s_can_ready = 1;
}

void BspCan_DeInit(void)
{
    s_can_ready = 0;
    CAN_DeInit(CAN1);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, DISABLE);
}

uint8_t BspCan_IsReady(void)
{
    return (uint8_t)s_can_ready;
}

uint8_t BspCan_Send(uint32_t id, const uint8_t *data, uint8_t len)
{
    CanTxMsg tx;
    uint8_t mailbox;
    uint32_t t0;
    uint32_t n = 0;

    if (!s_can_ready || !data || len > 8U) return 1U;

    tx.ExtId = 0U;
    tx.RTR = CAN_RTR_Data;
    tx.DLC = len;
    if (id > 0x7FFU) {
        tx.IDE = CAN_Id_Extended;
        tx.ExtId = id;
    } else {
        tx.IDE = CAN_Id_Standard;
        tx.StdId = (uint16_t)id;
    }
    for (n = 0; n < len; n++) tx.Data[n] = data[n];

    mailbox = CAN_Transmit(CAN1, &tx);
    if (mailbox == CAN_TxStatus_Failed) return 1U;

    t0 = 0; /* 简单超时轮询（约 5ms 上限） */
    while (CAN_TransmitStatus(CAN1, mailbox) == CAN_TxStatus_Pending) {
        if (++t0 > 20000U) break;
    }
    return 0U;
}

uint8_t BspCan_Recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    CanRxMsg rx;

    if (!s_can_ready) return 0U;
    if (CAN_MessagePending(CAN1, CAN_FIFO0) == 0U) return 0U;

    CAN_Receive(CAN1, CAN_FIFO0, &rx);
    if (id) *id = (rx.IDE == CAN_Id_Extended) ? rx.ExtId : (uint32_t)rx.StdId;
    if (len) *len = rx.DLC;
    if (data) {
        for (uint8_t i = 0U; i < rx.DLC && i < 8U; i++) data[i] = rx.Data[i];
    }
    return 1U;
}

#endif /* BOOTLOADER_BUILD */
