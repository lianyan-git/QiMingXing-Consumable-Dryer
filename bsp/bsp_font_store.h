/* bsp_font_store.h - 外部 Flash 中文字库(AB 双区 + 有效标志)
 * 分区: A=0xE10000 B=0xEE0000 (各 832KB, W25Q128 16MB @0xC20000音乐分区之后)
 * bin 布局(匹配 tools/langgen.py):
 *   头64B: magic 'FONT' | ver u16 | dict_len u16 | glyph u16=32
 *          cjk_base u32 | dict_tab u32 | dict_data u32 | flag u32 | reserved
 *   随附:  CJK区[U+4E00..U+9FA5]连续32B/字 (O(1): off = base + cjk_base + (cp-0x4E00)*32)
 *   字典表: dict_len 条 u16 unicode + u32 offset (排 dict_tab)
 *   字典数据: 每条 32B (unicon 通过表 offset)
 * AB 双区: 完整字库尾部 flag=0xA5A50001 有效; 下载未完成 flag=0 -> 用另一区 */
#ifndef BSP_FONT_STORE_H
#define BSP_FONT_STORE_H

#include <stdint.h>

#define LANG_FLASH_A      UINT32_C(0x00E10000)
#define LANG_FLASH_B      UINT32_C(0x00EE0000)
#define LANG_REGION_SIZE  UINT32_C(0x000D0000)   /* 832KB */
#define LANG_MAGIC        0x544E4F46UL           /* 'FONT' */
#define LANG_FLAG_READY   0xA5A50001UL
#define LANG_CJK_BASE     0x40U
#define LANG_CJK_LO       0x4E00U
#define LANG_CJK_HI       0x9FA5U
#define LANG_GLYPH_BYTES  32U

/* 初始化: 探测 A/B 有效字库并装载字典索引到 RAM; 0=有字库, -1=无 */
int  LangInit(void);
uint8_t LangReady(void);
/* 取字模: 0=OK 填入 out[32]; -1=未找到(不会解) */
int  LangGetGlyph(uint16_t unicode, uint8_t out[LANG_GLYPH_BYTES]);
int  LangGetAscii(uint8_t ch, uint8_t out[5]);  /* ASCII 5x7 阳码(与内置 glyph_for 同格式) */

/* 上传/更新字库 (提供 AB 交叠 + flag 原子切换): */
uint32_t LangTargetBase(void);          /* 下一个要写入的分区基址 */
int  LangBeginWrite(uint32_t binLen);   /* 擦除目标区; 0=ok */
int  LangWriteAt(uint32_t base, uint32_t offOffset, const uint8_t *buf, uint32_t len);
int  LangMarkValid(uint32_t base);      /* 整包写完后回写完整 flag(物理有效) */

#endif /* BSP_FONT_STORE_H */
