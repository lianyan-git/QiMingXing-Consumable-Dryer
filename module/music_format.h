/*
 * music_format.h — 音乐固件（.mub）容器格式 + 内置曲目接口
 *
 * 上传的“音乐固件”是一个自描述二进制：头部 + 曲目表 + 名称区 + 音符数据。
 * STM32 收到整个文件后写入外部 Flash（MUSIC 分区 @0xC20000），
 * 列表/名称/播放全部从外部 Flash 读取（名称即汉化数据，随固件存于外部）。
 *
 * 布局（全部小端）：
 *   偏移       大小   说明
 *   0x00        u32   magic 'MUB1' (0x314255 4D)
 *   0x04        u32   version = 1
 *   0x08        u16   track_count (1..16)
 *   0x0A        u16   flags (0)
 *   0x0C        u32   file_size（整个 .mub 字节数）
 *   0x10        u32   crc32（对整个文件从 0x00 起算，crc 字段本身按 0 参与计算）
 *   0x14        u32   name_table_off（相对文件头）
 *   0x18        u32   name_table_size（UTF-8，名称以 \0 分隔）
 *   0x1C        u32   track_table_off（相对文件头）
 *   0x20        u32   reserved
 *    --- 曲目表：track_count × 16B ---
 *   note_off(u32 相对文件头), note_size(u32), name_off(u32 相对名称区), name_len(u16), reserved(u16)
 *    --- 名称区 ---
 *   每个曲目名称 UTF-8（含结尾 \0）
 *    --- 音符数据区 ---
 *   每曲：连续 n 个 {u16 freq_hz, u16 dur_ms}；freq=0 表示休止符
 */
#ifndef MUSIC_FORMAT_H
#define MUSIC_FORMAT_H

#include <stdint.h>

#define MUSIC_FILE_MAGIC      0x3142554DUL   /* 'MUB1' */
#define MUSIC_FILE_VERSION    1U
#define MUSIC_MAX_TRACKS      16U
#define MUSIC_MAX_FILE_SIZE   UINT32_C(0x001C0000)

typedef struct {
    uint32_t note_off;
    uint32_t note_size;
    uint32_t name_off;
    uint16_t name_len;
    uint16_t reserved;
} MusicTrackEntry_t;

#endif /* MUSIC_FORMAT_H */