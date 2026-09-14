/*
 * cs1237.h
 * CS1237称重传感器 (PA0/PA1, 软件时序)
 */

#ifndef __CS1237_H
#define __CS1237_H

#include <stdint.h>

void CS1237_Init(void);
float CS1237_ReadWeight(void);
void CS1237_Tare(void);  // 去皮

#endif /* __CS1237_H */
