#ifndef __STEPPER_MOTOR_H
#define __STEPPER_MOTOR_H

#include <stdint.h>

void Stepper_Init(void);
void Stepper_Enable(uint8_t enable);
void Stepper_SetSpeed(uint16_t steps_per_sec);
void Stepper_Move(int32_t steps);
void Stepper_SetOscillate(int32_t steps);
void Stepper_Update(void);
uint8_t Stepper_IsRunning(void);
uint8_t Stepper_TmcComOk(void);

#endif /* __STEPPER_MOTOR_H */