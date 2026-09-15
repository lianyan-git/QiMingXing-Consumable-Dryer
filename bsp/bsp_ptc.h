#ifndef BSP_PTC_H
#define BSP_PTC_H

#include <stdint.h>

void PTC_Init(void);
void PTC_SetPower(uint8_t percent);

/* 加热许可：默认禁止加热，仅 PTC_Enable() 后 PTC_SetPower 才有效。
 * 主动烘干开始/自整定启动时 Enable，停止/暂停/空闲/异常显 Disable 立即断开。 */
void PTC_Enable(void);
void PTC_Disable(void);

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

/* ---- 临时 PTC 诊断（只读快照环形缓冲，不改变任何控制状态/算法）----
 * 用调试器 Memory 窗口查看 g_ptc_diag：entries[0..count-1]，wrap 为写指针。
 * 判读要点：
 *   ccr1   : PTC_SetPower(X) 后应 == X*10（100→1000, 50→500, 0→0）
 *   cr1 bit0(CEN)      ==1  定时器运行
 *   ccer bit0(CC1E)    ==1  CH1 输出使能
 *   bdtr bit15(MOE)    ==1  高级定时器主输出使能
 *   crh_pa8 (PA8 CRH>>28)&0xF: 0x0A=APP@50M(TIM1 AF，正确) 0x09=AOP@50M 0x02=GAIN输入(错误=未切AF) */
typedef struct {
    uint8_t  src;            /* 0=StartDrying 1=PID_AutotuneStart 2=control_update */
    uint8_t  p_air;          /* 0xFF = 该点无 PID 值 */
    uint8_t  p_ntc;          /* 0xFF = 该点无 PID 值（重点看是否 0） */
    uint8_t  pwr;            /* 0xFF = 该点无 pwr */
    uint16_t ptc_max_temp;   /* g_sys.params.ptc_max_temp（应=70） */
    uint16_t ptc_temp_x100;  /* g_sys.ptc_temp*100 整型化（1234=12.34C） */
    uint32_t t_ms;           /* SystemTime_Millis() */
    uint16_t ccr1;           /* TIM1->CCR1 */
    uint16_t ccer;           /* TIM1->CCER */
    uint16_t bdtr;           /* TIM1->BDTR */
    uint16_t cr1;            /* TIM1->CR1 */
    uint8_t  crh_pa8;        /* PA8 配置 nibble */
} PtcDiagEntry_t;

#define PTC_DIAG_DEPTH 16
typedef struct {
    volatile uint8_t      count;    /* 已记录条数（饱和到 DEPTH） */
    volatile uint8_t      wrap;     /* 下一个写索引 */
    volatile PtcDiagEntry_t entries[PTC_DIAG_DEPTH];
} PtcDiag_t;

extern PtcDiag_t g_ptc_diag;
/* src: 0=StartDrying 1=PID_AutotuneStart 2=control_update。只读寄存器+g_sys，不改控制。 */
void PtcDiag_Snap(uint8_t src, uint8_t p_air, uint8_t p_ntc, uint8_t pwr);

#endif