/*
 * buzzer.h
 * 蜂鸣器 (TIM3_CH4, PB1)
 */

#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdint.h>

void Buzzer_Init(void);
void Buzzer_Beep(uint16_t ms);
void Buzzer_SetFreq(uint16_t freq);

#endif /* __BUZZER_H */
