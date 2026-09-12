/*
 * bsp_can.h
 * CAN 总线驱动 (bxCAN1, PB8=RX / PB9=TX)
 * 注：函数加 BspCan_ 前缀，避免与 StdPeriph 库 CAN_Init/CAN_DeInit 重名。
 */

#ifndef __BSP_CAN_H
#define __BSP_CAN_H

#include <stdint.h>

void BspCan_Init(uint32_t baud_kbps);
void BspCan_DeInit(void);
uint8_t BspCan_IsReady(void);
uint8_t BspCan_Send(uint32_t id, const uint8_t *data, uint8_t len);
uint8_t BspCan_Recv(uint32_t *id, uint8_t *data, uint8_t *len);

#endif /* __BSP_CAN_H */
