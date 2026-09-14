#ifndef __FAN_CONTROLLER_H
#define __FAN_CONTROLLER_H

#include <stdint.h>

void Fan_Init(void);
void Fan_SetSpeed(uint8_t percent);      // 0-100%
uint8_t Fan_GetSpeed(void);
void Fan_AdjustSpeed(int8_t delta);      // 调整百分之delta，限制在 0-100
void Fan_Off(void);

#endif /* __FAN_CONTROLLER_H */