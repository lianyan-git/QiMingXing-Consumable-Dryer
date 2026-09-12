/*
 * music_ota.h — 音乐固件 App 侧接收（ESP → UART 0xAA 帧 → 外部 Flash）
 *
 * 帧格式（与 bootloader 固件 OTA 兼容，但入口字节区分音乐）：
 *   握手：0xAA 0x55 0x11 [sizeBE4]         → 接收方 ACK 0x06 / NAK 0x15
 *   数据：0xAA [seqBE2] [data≤1024] [crc16BE2] 0x55   → ACK 0x06 / NAK 0x15
 *   结束：0xAA 0x55 0x12 [crc32BE4]       → ACK 0x06 / NAK 0x15
 *   （0x11/0x12 是音乐专用类型，与 bootloader 的 0x01/0x02 区分）
 * CRC16=Modbus(seq2+data)，CRC32=IEEE802.3 全文件。
 */
#ifndef MUSIC_OTA_H
#define MUSIC_OTA_H

#include <stdint.h>

void    MusicOta_Init(void);
void    MusicOta_FeedByte(uint8_t b);          /* EspLink_Process 在音乐接收态喂入 */
void    MusicOta_Poll(void);                    /* 推进 MusicStore 落盘 + 超时 */
void    MusicOta_Abort(void);
uint8_t MusicOta_Active(void);                  /* 1=正在接收/等待 0=空闲 */
uint8_t MusicOta_Error(void);

#endif /* MUSIC_OTA_H */