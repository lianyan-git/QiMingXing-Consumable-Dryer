/*
 * littlefs_adapter.h
 * LittleFS适配层
 */

#ifndef __LITTLEFS_ADAPTER_H
#define __LITTLEFS_ADAPTER_H

#include <stdint.h>

int LittleFS_Init(void);
int LittleFS_SaveWeight(float weight);
int LittleFS_ReadWeightHistory(float *weights, uint16_t max_count);

#endif /* __LITTLEFS_ADAPTER_H */
