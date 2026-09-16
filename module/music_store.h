/*
 * music_store.h — 音乐固件外部 Flash 存储与列表
 *
 * MUSIC 分区：0xC20000..0xE00000（1.875MB）。
 *   0xC20000 扇区0：全局头 {magic 'MUS1', version, resume_size, tail_crc, checksum}
 *   0xC21000 起：整包 .mub（头部+曲目表+名称+音符；固定 MUSIC_MAX_FILE_SIZE 上限）
 *
 * 上传流程（与 LangOta/bootloader OTA 同构, 逐包 Poll FSM）：
 *   MusicStore_BeginUpload(size)   — 记录总长, 握手立即 ACK 放行 ESP
 *   MusicStore_WritePacket(pkt,len) — 拷贝整包进缓冲, 置写 FSM（擦/写由 Poll 推进）
 *   MusicStore_Poll()              — 每主循环推进: 返回 1=本包写完, 0=进行中, -1=失败
 *   MusicStore_Finish(crc, size)    — 写全局头
 *   MusicStore_AbortUpload()        — 复位
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
#define MUSIC_PKT_MAX       1024U                                /* 与 mt_pkt_max 一致 */

void   MusicStore_Init(void);          /* 读取全局头；有有效固件则就绪 */
int    MusicStore_HasFirmware(void);
int    MusicStore_BeginUpload(uint32_t size);
int    MusicStore_WritePacket(const uint8_t *pkt, uint32_t len); /* 0=ok, -1=拒绝 */
int    MusicStore_Poll(void);          /* 返回 1=本包写完, 0=进行中, -1=失败 */
uint8_t MusicStore_WriteBusy(void);     /* 1=写 FSM 正在推进(收流暂停) */
int    MusicStore_Finish(uint32_t crc32, uint32_t size);         /* 0=ok */
void   MusicStore_AbortUpload(void);

/* 读取列表/名称/音符（全部直接读外部 Flash） */
uint16_t MusicStore_TrackCount(void);
int      MusicStore_GetTrack(uint16_t idx, MusicTrackEntry_t *e);       /* 解析曲目表条目 */
int      MusicStore_GetName(uint16_t idx, char *buf, uint16_t buflen);  /* 含2字节保底 */
int      MusicStore_GetNote(uint16_t track, uint32_t k, uint16_t *freq, uint16_t *dur);

void MusicStore_Wipe(void);   /* 擦除全局头扇区=清空音乐列表(长按音乐列表触发) */

#endif /* MUSIC_STORE_H */