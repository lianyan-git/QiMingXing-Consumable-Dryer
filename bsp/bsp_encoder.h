/*
 * encoder.h
 * EC11旋转编码器驱动
 */

#ifndef __ENCODER_H
#define __ENCODER_H

#include <stdint.h>

typedef enum {
    ENC_EVT_NONE,
    ENC_EVT_CW,         // 顺时针
    ENC_EVT_CCW,        // 逆时针
    ENC_EVT_CLICK,      // 单击
    ENC_EVT_LONG_PRESS, // 长按
} EncoderEvent_t;

void Encoder_Init(void);
EncoderEvent_t Encoder_GetEvent(void);
void Encoder_Process(void);
void Encoder_TickISR(void);   /* 1kHz SysTick 采样：不丢相位，主循环阻塞也能连续响应 */

extern volatile uint32_t g_last_input_ms;  /* 最后输入时间(ms), 供熄屏计时 */

#endif /* __ENCODER_H */
