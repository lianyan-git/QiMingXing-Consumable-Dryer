/*
 * music_play.h — 蜂鸣器音乐播放器
 *
 * 数据源：内置曲目表（music_data.c，受 `music` 编译宏控制）
 *        + 外部 Flash 音乐固件（music_store，.mub）。
 * 曲目统一编号：0..(内置数-1) 为内置；其后为外部固件曲目。
 *
 * 播放：占住 TIM3（改 ARR 实现音符频率，CCR3 背光按比例重缩放实现无闪烁）。
 * 只播放一次（不循环），可后台继续（离开列表页亦持续）。
 */
#ifndef MUSIC_PLAY_H
#define MUSIC_PLAY_H

#include <stdint.h>

void   MusicPlay_Init(void);
void   MusicPlay_Poll(void);                 /* 每主循环调用（音符推进/进度） */
int    MusicPlay_Play(uint16_t track);       /* 0=ok；-1=失败 */
void   MusicPlay_Stop(void);
uint8_t MusicPlay_IsPlaying(void);
uint16_t MusicPlay_CurTrack(void);
uint16_t MusicPlay_CurFreq(void);          /* 当前音符频率Hz(0=休止), RGB音高灯效用 */
uint32_t MusicPlay_ElapsedMs(void);
uint32_t MusicPlay_TotalMs(void);
uint32_t MusicPlay_FinishMs(void);           /* 自然结束时间戳(0=无) */
uint16_t MusicPlay_ProgressPct(void);        /* 0..100 */

/* 曲目总表（内置+外部合并） */
uint16_t MusicPlay_TrackCount(void);
int      MusicPlay_GetTitle(uint16_t track, char *buf, uint16_t buflen);
int      MusicPlay_GetNote(uint16_t track, uint32_t k, uint16_t *freq, uint16_t *dur);
uint32_t MusicPlay_TrackNoteCount(uint16_t track);

#endif /* MUSIC_PLAY_H */