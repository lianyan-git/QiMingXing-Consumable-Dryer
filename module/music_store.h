/*
 * music_store.h — 音乐固件外部 Flash 存储与列表
 *
 * MUSIC 分区：0xC20000..0xE00000（1.875MB）。
 *   0xC20000 扇区0：全局头 {magic 'MUS1', version, resume_size, tail_crc, checksum}
 *   0xC20400 起：整包 .mub（头部+曲目表+名称+音符；固定 MUSIC_MAX_FILE_SIZE 上限）
 *
 * 上传流程（非阻塞 FSM，经 SysFlashOp）：
 *   MusicStore_Begin()   — 记录新文件将覆盖的分区起点（此时不擦整区）
 *   MusicStore_BeginUpload(size)   — 置总长，准备按需擦扇区
 *   MusicStore_Write(woff, buf, len) — 页缓冲/擦扇区/写页 状态机轮询推进
 *   MusicStore_Finish(crc, size)    — 校验 + 写全局头
 *
 * 读取/列表：全部从外部 Flash 读（名称即汉化数据）。
 */
#ifndef MUSIC_STORE_H
#define MUSIC_STORE_H

#include <stdint.h>
#include "music_format.h"

#define MUSIC_FLASH_BASE    UINT32_C(0xC20000)
#define MUSIC_DATA_BASE     (MUSIC_FLASH_BASE + 0x1000U)          /* 0xC21000 */
#define MUSIC_MAX_FILE_SIZE UINT32_C(0x001C0000)                 /* 1.75MB，留异部余量 */
#define MUSIC_HDR_MAGIC     0x3153554DUL                         /* 'MUS1' */

void   MusicStore_Init(void);          /* 读取全局头；有有效固件则就绪 */
int    MusicStore_HasFirmware(void);
int    MusicStore_BeginUpload(uint32_t size);
uint32_t MusicStore_Write(const uint8_t *buf, uint32_t len);  /* 返回未吸收字节数(>0 时调用方应 Poll 后再续) */
uint32_t MusicStore_WrittenOff(void);                         /* 已落盘字节数(相对数据区), 供 ACK 时序判断 */
int    MusicStore_Finish(uint32_t crc32, uint32_t size);         /* 0=ok */
void   MusicStore_AbortUpload(void);
void   MusicStore_Poll(void);          /* 每主循环调用推进落盘 */

/* 读取列表/名称/音符（全部直接读外部 Flash） */
uint16_t MusicStore_TrackCount(void);
int      MusicStore_GetTrack(uint16_t idx, MusicTrackEntry_t *e);       /* 解析曲目表条目 */
int      MusicStore_GetName(uint16_t idx, char *buf, uint16_t buflen);  /* 含2字节保底 */
int      MusicStore_GetNote(uint16_t track, uint32_t k, uint16_t *freq, uint16_t *dur);

void MusicStore_Wipe(void);   /* 擦除全局头扇区=清空音乐列表(长按音乐列表触发) */
void MusicStore_PrepareWipe(void);
int  MusicStore_WipeForSize(uint32_t size);   /* 握手后擦净: 头+所需数据扇区, 擦完再 ACK(与OTA一致) */
int  MusicStore_WipeForSize(uint32_t size);   /* 握手后擦净: 头+所需数据扇区, 擦完再 ACK(与OTA一致) */  /* pre-erase old firmware area (header + old data sectors) at upload session start */

#endif /* MUSIC_STORE_H */