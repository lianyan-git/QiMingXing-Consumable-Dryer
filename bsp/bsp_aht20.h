/*
 * aht20.h
 * AHT20温湿度传感器 (I2C2, PB10/PB11)
 */

#ifndef __AHT20_H
#define __AHT20_H

#include <stdint.h>

#define AHT20_ADDR  0x38

int AHT20_Init(void);
int AHT20_Read(float *temperature, float *humidity);

#endif /* __AHT20_H */
