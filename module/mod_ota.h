/*
 * ota_manager.h
 * OTA升级管理
 */

#ifndef MOD_OTA_H
#define MOD_OTA_H

#include <stdint.h>

void OTA_Init(void);
void OTA_Process(void);
void OTA_ReceiveChunk(uint8_t *data, uint16_t len);
void OTA_TriggerUpgrade(void);
void OTA_EnterBootloader(void);

#endif /* MOD_OTA_H */
