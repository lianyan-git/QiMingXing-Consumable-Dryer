/*
 * music_play.c — 蜂鸣器音乐播放器
 * 音符表统一映射：内置曲目（music_data）在前，外部固件曲目（music_store）在后。
 * 播放占用 TIM3（bz 时基）：按音符改 ARR，CCR3（背光）按 ARR 比例重缩放，
 * 恢复时还原 kBaseArr 与背光 CCR3。进度推进由 MusicPlay_Poll 每主循环调用。
 */
#include "music_play.h"
#include "music_data.h"
#include "music_store.h"
#include "pin_config.h"
#include "system_config.h"
#include "system_time.h"
#include "stm32f10x.h"
#include "stm32f10x_tim.h"
#include <stddef.h>

static uint16_t s_track   = 0;
static uint8_t  s_playing = 0;
static uint16_t s_note    = 0;        /* 当前音符下标 */
static uint16_t s_note_rem= 0;        /* 音符剩余 ms */
static uint32_t s_total_ms= 0;
static uint32_t s_elapsed = 0;
static uint32_t s_beat_t0 = 0;
static uint32_t s_finish_ms = 0;   /* 自然播放结束时间戳(0=无) */

#define BASE_ARR  499U                  /* buzzer 默认 ARR（2kHz 载波） */

static uint16_t s_saved_ccr3 = 0;      /* 进入播放前背光 CCR3 */

/* 按音符改 ARR（背光等比缩放）：freq=0 表示休止→关输出 */
static void set_tone(uint16_t freq)
{
    uint32_t arr;
    if (freq == 0) {
        TIM_SetCompare4(TIM3, 0);
        return;
    }
    arr = 1000000UL / (uint32_t)freq;
    if (arr < 2U) arr = 2U;
    if (arr > 65535U) arr = 65535U;
    /* 背光和蜂鸣器共用 TIM3: 播放期间 ARR 随音符变。CCR3 保持 Play 时的满占空(0xFFFF)
     * => 背光恒亮无闪烁(代价: 播放期间为最大亮度, 播完 restore_base 恢复设定亮度) */
    TIM_SetAutoreload(TIM3, (uint16_t)(arr - 1U));
    TIM_SetCompare4(TIM3, (uint16_t)((arr - 1U) / 2U));
}

static void restore_base(void)
{
    TIM_SetAutoreload(TIM3, BASE_ARR);
    TIM_SetCompare3(TIM3, s_saved_ccr3);
    TIM_SetCompare4(TIM3, 0);
}

static int track_note(uint16_t track, uint32_t k, uint16_t *freq, uint16_t *dur)
{
    return MusicStore_GetNote(track, k, freq, dur);
}

void MusicPlay_Init(void)
{
    s_playing = 0;
}

uint16_t MusicPlay_TrackCount(void)
{
    /* 列表只显示上传到外部 Flash 的曲目（无内置默认占位） */
    uint16_t m = MusicStore_TrackCount();
    if (m > 16U) m = 16U;
    return m;
}

int MusicPlay_GetTitle(uint16_t track, char *buf, uint16_t buflen)
{
    return MusicStore_GetName(track, buf, buflen);
}

uint32_t MusicPlay_TrackNoteCount(uint16_t track)
{
    {
        MusicTrackEntry_t e;
        if (MusicStore_GetTrack(track, &e) == 0) return e.note_size / 4U;
    }
    return 0;
}

int MusicPlay_Play(uint16_t track)
{
    MusicPlay_Stop();
    if (track >= MusicPlay_TrackCount()) return -1;
    s_saved_ccr3 = (uint16_t)(TIM3->CCR3);   /* 记录进入播放前背光 CCR3 */
    TIM_SetCompare3(TIM3, 0xFFFF);           /* 播放期间背光满占空恒亮: TIM3 时基被音符占用, 固定 100% 消除音阶频率闪烁 */
    /* 预读整曲总时长 */
    {
        uint32_t n = MusicPlay_TrackNoteCount(track);
        uint32_t k;
        uint16_t fq, du;
        s_total_ms = 0;
        for (k = 0; k < n; k++) {
            if (track_note(track, k, &fq, &du) != 0) break;
            s_total_ms += du;
        }
    }
    s_track   = track;
    s_note    = 0;
    s_elapsed = 0;
    s_note_rem= 0;
    s_beat_t0 = SystemTime_Millis();
    s_playing = 1;
    return 0;
}

void MusicPlay_Stop(void)
{
    if (s_playing) restore_base();
    s_playing = 0;
    s_note = 0; s_note_rem = 0; s_elapsed = 0;
    s_finish_ms = 0;
}

uint8_t MusicPlay_IsPlaying(void)           { return s_playing; }
uint16_t MusicPlay_CurTrack(void)           { return s_track; }
uint32_t MusicPlay_ElapsedMs(void)          { return s_elapsed; }
uint32_t MusicPlay_TotalMs(void)
{
    /* 总时长在每次 Play 时已缓存；TrackCount 等查询不受影响 */
    return s_total_ms;
}
uint32_t MusicPlay_FinishMs(void)           { return s_finish_ms; }

uint16_t MusicPlay_ProgressPct(void)
{
    uint32_t total = s_total_ms;
    if (s_finish_ms) return 100U;   /* 自然结束：定格 100% */
    if (total == 0) return 0;
    {
        uint32_t p = s_elapsed * 100U / total;
        return (p > 100U) ? 100U : (uint16_t)p;
    }
}

static void music_play_finish_natural(void)
{
    restore_base();
    s_elapsed = s_total_ms;              /* 进度定格 100% */
    s_finish_ms = SystemTime_Millis();   /* UI 在 2.5s 后清除进度框 */
    s_playing = 0;
    s_note = 0; s_note_rem = 0;
}

void MusicPlay_Poll(void)
{
    uint16_t fq, du;
    uint32_t now, dt;

    if (!s_playing) return;
    now = SystemTime_Millis();
    dt = (uint32_t)(now - s_beat_t0);
    s_beat_t0 = now;
    if (dt == 0) return;

    /* 推进当前音符剩余 */
    if (s_note_rem > 0) {
        if (dt < s_note_rem) {
            s_note_rem = (uint16_t)(s_note_rem - dt);
            s_elapsed += dt;
            return;
        }
        dt -= s_note_rem;
        s_elapsed += s_note_rem;
        s_note_rem = 0;
    }

    /* 取下一音符；遇失败/表尾(0,0) 结束播放 */
    for (;;) {
        if (track_note(s_track, s_note, &fq, &du) != 0) { music_play_finish_natural(); return; }
        s_note++;
        if (fq == 0 && du == 0) { music_play_finish_natural(); return; }
        set_tone(fq);
        s_note_rem = du;
        if (dt < s_note_rem) {
            s_note_rem = (uint16_t)(s_note_rem - dt);
            s_elapsed += dt;
            return;
        }
        dt -= s_note_rem;
        s_elapsed += s_note_rem;
        s_note_rem = 0;
    }
}