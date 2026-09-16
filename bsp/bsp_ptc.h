#ifndef BSP_PTC_H
#define BSP_PTC_H

#include <stdint.h>

void PTC_Init(void);
void PTC_SetPower(uint8_t percent);

void PTC_PID_AutotuneStart(void);
void PTC_PID_AutotuneStartWithFan(uint8_t fan_pct);
uint8_t PTC_PID_AutotuneProcess(void);
uint8_t PTC_PID_AutotuneIsDone(void);
void PTC_PID_GetParams(float *kp, float *ki, float *kd);
uint8_t PTC_PID_AutotuneGetProgress(void);

void PTC_TempPID_AutotuneStart(float target_temp);
uint8_t PTC_TempPID_AutotuneProcess(void);
uint8_t PTC_TempPID_AutotuneIsDone(void);
uint8_t PTC_TempPID_AutotuneGetProgress(void);

#endif
