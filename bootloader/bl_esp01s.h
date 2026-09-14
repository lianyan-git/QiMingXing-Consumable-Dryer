#ifndef __BL_ESP01S_H
#define __BL_ESP01S_H

#include <stdint.h>

void BL_ESP01S_Init(void);
int  BL_ESP01S_StartOta(void);   /* 发 AT+OTAAP，等 OK 后进入 UART 接收状态 */
int  BL_ESP01S_CloseWeb(void);   /* 发 AT+OTACLOSE，让 ESP 进入低功耗休眠（由 STM32 控制时机） */
const char* BL_ESP01S_GetIP(void);

void BL_ESP01S_Process(void);    /* 按二进制 OTA 协议收固件并写入 W25Q128 */
void BL_ESP01S_ResetTransfer(void);
void BL_ESP01S_FinishTransfer(void);
void BL_ESP01S_AbortTransfer(void);

int BL_ESP01S_GetTransferState(void);
uint32_t BL_ESP01S_GetReceivedSize(void);
uint32_t BL_ESP01S_GetTotalSize(void);
uint32_t BL_ESP01S_GetTotalFirmwareSize(void);
uint32_t BL_ESP01S_GetFirmwareCrc32(void);

#endif