#ifndef BSP_NTC_H
#define BSP_NTC_H

#include <stdint.h>

#define NTC_OVERTEMP_THRESHOLD  85      // PTC 过热保护阈值 °C

void NTC_Init(void);
uint16_t NTC_ReadADC(void);
int16_t NTC_GetTemperature(void);       // 返回温度值 (单位 °C, 含小数部分 *10)
uint8_t NTC_IsOverTemp(void);           // 是否超过过热保护阈值
uint8_t NTC_IsValid(void);               // NTC 读数有效(非开路/短路异常值)

#endif /* BSP_NTC_H */