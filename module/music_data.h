/*
 * music_data.h — 内置音乐曲目表
 *
 * “音乐固件”库文件：所有内置曲目（音符表 + 中文名称）都写在这里。
 * 由编译参数 `music` 控制是否编译内置曲目（build define: music）。
 * 之后要新增/替换内置音乐，只改本文件即可。
 */
#ifndef MUSIC_DATA_H
#define MUSIC_DATA_H

#include <stdint.h>

/* 内置曲目数（music 宏未定义时为 0） */
uint16_t MusicLib_BuiltinCount(void);

/* k 为音符下标，从 0 起；返回该曲音符数 */
uint32_t MusicLib_BuiltinNoteCount(uint16_t track);
int      MusicLib_BuiltinGetNote(uint16_t track, uint32_t k, uint16_t *freq, uint16_t *dur);
const char *MusicLib_BuiltinTitle(uint16_t track, uint16_t *len);

#endif /* MUSIC_DATA_H */