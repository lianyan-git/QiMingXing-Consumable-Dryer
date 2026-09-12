#ifndef __STEPPER_MOTOR_H
#define __STEPPER_MOTOR_H

#include <stdint.h>

/* 料盘连续旋转标定：一整圈 360° 需要的步数（硬编码，实测校准）。
 * 注意：连续旋转 ≠ 摆动——摆动角用 swing_cal(百分比)标定摆幅，而"次数"
 * 指的是连续旋转的整圈数，必须用独立的旋转步数常数，不乘 swing_cal。
 *
 * 2026-09-10 实测：51200 步转了 3 圈又 150°（≈3.42 圈）→ 一整圈 ≈ 51200/3.42 ≈ 15000 步。
 * 若实机仍不是整好一圈：把"实际圈数/期望1圈"的比值乘到 15000 上即可（如转 1.3 圈则 15000×1.3=19500）。 */
#define TRAY_STEPS_PER_360   15000U

/* 摆动标定（仅摆动角度用）：每度基本步数×100，再乘 motor_swing_cal% */
#define SWING_BASE_STEPS_PER_DEG  4545U
#define SWING_CAL_DEFAULT         100

void Stepper_Init(void);
void Stepper_Enable(uint8_t enable);
void Stepper_SetSpeed(uint16_t steps_per_sec);
void Stepper_Move(int32_t steps);
void Stepper_SetOscillate(int32_t steps);
void Stepper_Update(void);
void Stepper_SyncProfile(void);            /* 从最新参数重建运动曲线（下一转动周期生效） */
void Stepper_SetSilent(uint8_t en);        /* TMC stealthChop 静音开关（CHOPCONF bit30） */
uint8_t Stepper_IsRunning(void);
uint8_t Stepper_TmcComOk(void);
uint8_t Stepper_TmcProbe(uint32_t *ifcnt);   /* 连读两次 IFCNT 确认递增：1=通讯真实成功并回报计数 */

#endif /* __STEPPER_MOTOR_H */