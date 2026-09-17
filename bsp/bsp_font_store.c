#include "bsp_font_store.h"
#include "sfud_flash.h"
#include "system_time.h"
#include <string.h>

#define LANG_DICT_MAX 4096

typedef struct {
    uint32_t base;          /* 当前有效分区基址 */
    uint32_t cjk_base;      /* CJK 区绝对地址 */
    uint32_t ascii_off;
    uint32_t dict_tab;      /* 字典表绝对地址(du[] + doff[] 连续) */
    uint16_t dlen;          /* 字典条目数 */
    uint8_t  ready;
} LangCtx_t;

static LangCtx_t s_lang;

/* 头 64B 布局 (little-endian) */
#define HDR_MAGIC      0
#define HDR_VER        4
#define HDR_DLEN       6
#define HDR_GLYPH      8
#define HDR_CJK_BASE   10
#define HDR_DICT_TAB   14
#define HDR_DICT_DATA  18
#define HDR_ASCII_OFF  22
#define HDR_FLAG       26

/* flash 忙等待带 5s 超时与错误传播(#3): 原实现无超时且忽略读返回值,
 * SPI/Flash 异常会在"上传完成写 flag"处永久卡死主循环。 */
static int wait_idle(void)
{
    uint8_t sr1;
    uint32_t t0 = SystemTime_Millis();
    do {
        if (SfudFlash_ReadSR1(&sr1) != 0) return -1;
        if ((uint32_t)(SystemTime_Millis() - t0) > 5000U) return -1;
    } while (sr1 & 0x01U);
    return 0;
}

/* 头 30..33: 32 位代际号(serial)。修复 #1: 旧逻辑 A 恒优先, 上传写 B 提交后
 * A 仍 READY → 重启回到旧字库, 新库永不生效。现给 Ready 标记随页写带 serial =
 * max(A,B)+1; 两区都 READY 时取 serial 大者。旧库该区字段为擦除态 FF → 记 0。 */
static uint32_t head_serial(uint32_t base)
{
    uint8_t h[36];
    uint32_t s = 0;
    if (SfudFlash_Read(base, h, sizeof(h)) != 0) return 0;
    memcpy(&s, h + 30, 4);
    if (s == 0xFFFFFFFFUL) return 0;
    return s;
}

static uint32_t head_flag(uint32_t base)
{
    uint8_t h[36];
    uint32_t magic, flag;
    if (SfudFlash_Read(base, h, sizeof(h)) != 0) return 0UL;
    memcpy(&magic, h + HDR_MAGIC, 4);
    memcpy(&flag, h + HDR_FLAG, 4);
    if (magic != LANG_MAGIC) return 0UL;
    return flag;
}

int LangInit(void)
{
    uint32_t a = head_flag(LANG_FLASH_A);
    uint32_t b = head_flag(LANG_FLASH_B);
    uint32_t base = 0;
    uint8_t h[36];
    uint32_t cjk, dict_tab;
    uint16_t dlen;

    memset(&s_lang, 0, sizeof(s_lang));
    if (a == LANG_FLAG_READY && b == LANG_FLAG_READY) {
        /* 双区都有效: 取代际号新者(修复上传后旧区压制新区的 BUG) */
        base = (head_serial(LANG_FLASH_B) > head_serial(LANG_FLASH_A)) ? LANG_FLASH_B : LANG_FLASH_A;
    } else if (a == LANG_FLAG_READY)      base = LANG_FLASH_A;
    else if (b == LANG_FLAG_READY)        base = LANG_FLASH_B;
    if (base == 0) return -1;

    if (SfudFlash_Read(base, h, sizeof(h)) != 0) return -1;
    memcpy(&dlen, h + HDR_DLEN, 2);
    memcpy(&cjk, h + HDR_CJK_BASE, 4);
        memcpy(&s_lang.ascii_off, h + HDR_ASCII_OFF, 4);
    memcpy(&dict_tab, h + HDR_DICT_TAB, 4);
    if (dlen > LANG_DICT_MAX) dlen = LANG_DICT_MAX;
    s_lang.base     = base;
    s_lang.cjk_base = base + cjk;
    s_lang.dict_tab = base + dict_tab;
    s_lang.dlen     = dlen;
    s_lang.ready    = 1;
    return 0;
}

uint8_t LangReady(void) { return s_lang.ready; }

/* 排 dict_tab: 先 dlen 条 u16(unicode 小端), 紧随 dlen 条 u32(相对 base 偏移) */
int LangGetGlyph(uint16_t cp, uint8_t out[LANG_GLYPH_BYTES])
{
    uint32_t off = 0;
    uint8_t b[4];
    int lo, hi, m;
    if (!s_lang.ready) return -1;
    if (cp >= LANG_CJK_LO && cp <= LANG_CJK_HI) {
        off = s_lang.cjk_base + ((uint32_t)(cp - LANG_CJK_LO) * LANG_GLYPH_BYTES);
    } else {
        lo = 0; hi = (int)s_lang.dlen - 1; m = -1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            uint16_t u;
            if (SfudFlash_Read(s_lang.dict_tab + (uint32_t)mid * 2, b, 2) != 0) return -1;
            u = (uint16_t)(b[0] | (b[1] << 8));
            if (u < cp) lo = mid + 1;
            else if (u > cp) hi = mid - 1;
            else { m = mid; break; }
        }
        if (m < 0) return -1;
        if (SfudFlash_Read(s_lang.dict_tab + (uint32_t)s_lang.dlen * 2 + (uint32_t)m * 4, b, 4) != 0) return -1;
        off = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
              ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        off += s_lang.base;
    }
    return (SfudFlash_Read(off, out, LANG_GLYPH_BYTES) == 0) ? 0 : -1;
}

int LangGetAscii(uint8_t ch, uint8_t out[5])
{
    if (!s_lang.ready) return -1;
    if (ch < 0x20U || ch > 0x7EU) return -1;
    return (SfudFlash_Read(s_lang.base + s_lang.ascii_off + ((uint32_t)(ch - 0x20U)) * 5U,
                           out, 5) == 0) ? 0 : -1;
}


uint32_t LangTargetBase(void)
{
    if (s_lang.ready && s_lang.base == LANG_FLASH_B) return LANG_FLASH_A;
    if (s_lang.ready) return LANG_FLASH_B;
    return LANG_FLASH_A;          /* 尚无字库: 写 A */
}

/* 主修复 #1: 新区提交成功后把另一区 flag 清零失效。NOR 编程只能 1→0,
 * 对任意当前态写 00 恒安全(读-改-写整页幂等), 不依赖 langgen 对保留区的填充。
 * 失效后 LangInit 只剩一个 READY → 必然选中新库; serial 仅是并列崩溃窗口的次级保险。 */
static int invalidate_other(uint32_t keep_base)
{
    uint32_t other = (keep_base == LANG_FLASH_A) ? LANG_FLASH_B : LANG_FLASH_A;
    uint8_t pg[256];
    if (head_flag(other) == 0UL) return 0;            /* 本就无效/无魔数: 无需操作 */
    if (SfudFlash_Read(other, pg, sizeof(pg)) != 0) return -1;
    pg[26] = 0x00U; pg[27] = 0x00U; pg[28] = 0x00U; pg[29] = 0x00U;
    pg[30] = 0x00U; pg[31] = 0x00U; pg[32] = 0x00U; pg[33] = 0x00U;
    if (SfudFlash_StartWritePage(other, pg, sizeof(pg)) != 0) return -1;
    (void)wait_idle();                                 /* 失效失败不反噬: 新库已 READY 可用 */
    return 0;
}

int LangMarkValid(uint32_t base)
{
    uint8_t cur[4];
    uint8_t pg[256];
    uint32_t pg_addr = base + (HDR_FLAG & 0xFFFFFF00U);   /* 256B 对齐页 */
    uint32_t serial;
    uint8_t i = (uint8_t)(HDR_FLAG & 0xFFU);
    uint8_t fa, fb;
    if (SfudFlash_Read(base + HDR_FLAG, cur, 4) != 0) return -1;
    if (cur[0] != 0xFFU || cur[1] != 0xFFU || cur[2] != 0xFFU || cur[3] != 0xFFU) return -1;  /* flag 区须未被写 */
    if (SfudFlash_Read(pg_addr, pg, sizeof(pg)) != 0) return -1;
    /* LANG_FLAG_READY=0xA5A50001 小端存储: 低址字节在低地址 → 01 00 A5 A5 */
    pg[i] = 0x01U; pg[i + 1] = 0x00U; pg[i + 2] = 0xA5U; pg[i + 3] = 0xA5U;
    fa = (head_flag(LANG_FLASH_A) == LANG_FLAG_READY) ? 1U : 0U;
    fb = (head_flag(LANG_FLASH_B) == LANG_FLAG_READY) ? 1U : 0U;
    {
        uint32_t sa = fa ? head_serial(LANG_FLASH_A) : 0U;
        uint32_t sb = fb ? head_serial(LANG_FLASH_B) : 0U;
        serial = ((sa > sb) ? sa : sb) + 1U;
    }
    pg[30] = (uint8_t)(serial & 0xFFU);
    pg[31] = (uint8_t)((serial >> 8) & 0xFFU);
    pg[32] = (uint8_t)((serial >> 16) & 0xFFU);
    pg[33] = (uint8_t)((serial >> 24) & 0xFFU);
    if (SfudFlash_StartWritePage(pg_addr, pg, sizeof(pg)) != 0) return -1;   /* 写页须 256B 对齐 */
    if (wait_idle() != 0) return -1;
    (void)invalidate_other(base);                      /* #1: 旧区失效(尽力而为) */
    return 0;
}