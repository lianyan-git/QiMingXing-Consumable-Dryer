/*
 * sht40.h
 * SHT40 温湿度传感器 (I2C2 软件模拟, PB10=SCL / PB11=SDA)
 */

#ifndef __SHT40_H
#define __SHT40_H

#include <stdint.h>

#define SHT40_ADDR  0x44

int SHT40_Init(void);
int SHT40_Read(float *temperature, float *humidity);

#endif /* __SHT40_H */
