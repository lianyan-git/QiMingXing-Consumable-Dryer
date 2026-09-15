/* lang_ota.h - 语言字库 App 侧接收(0xAA 二进制帧 -> LANG 分区, AB 双区+flag)
 * 协议与 music_ota 同构: 握手 0xAA 0x55 0x13 [sizeBE4] -> 0x06/0x15,
 * 数据 0xAA [seqBE2] [data] [crc16BE2] 0x55, 结束 0xAA 0x55 0x14 [crc32BE4]. */
#ifndef LANG_OTA_H
#define LANG_OTA_H

#include <stdint.h>

void LangOta_Init(void);
void LangOta_Abort(void);
uint8_t LangOta_Active(void);
void LangOta_FeedByte(uint8_t b);
void LangOta_Poll(void);
uint8_t  LangOta_GotHand(void);
uint16_t LangOta_GetNak(void);
uint16_t LangOta_GetAck(void);
uint32_t LangOta_GetRecv(void);
uint8_t  LangOta_GetPct(void);
uint8_t  LangOta_EndStat(void);  /* 结束帧结果: 0=未到 1=OK 2=END-CRC不一致仍提交 3=写flag失败 4=数据不完整 */

#endif /* LANG_OTA_H */
