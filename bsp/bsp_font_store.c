#include "bsp_font_store.h"
#include "sfud_flash.h"
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

static void wait_idle(void)
{
    uint8_t sr1;
    do { SfudFlash_ReadSR1(&sr1); } while (sr1 & 0x01U);
}

static uint32_t head_flag(uint32_t base)
{
uint8_t h[30];
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
uint8_t h[30];
    uint32_t cjk, dict_tab;
    uint16_t dlen;

    memset(&s_lang, 0, sizeof(s_lang));
    if (a == LANG_FLAG_READY)      base = LANG_FLASH_A;
    else if (b == LANG_FLAG_READY) base = LANG_FLASH_B;
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

int LangBeginWrite(uint32_t binLen)
{
    uint32_t base = LangTargetBase();
    uint32_t a;
    (void)binLen;
    if (base + LANG_REGION_SIZE > UINT32_C(0x01000000)) return -1;
    for (a = 0; a < LANG_REGION_SIZE; a += 0x1000U) {
        if (SfudFlash_StartEraseSector(base + a) != 0) return -1;
        wait_idle();
    }
    return 0;
}

int LangWriteAt(uint32_t base, uint32_t off, const uint8_t *buf, uint32_t len)
{
    while (len) {
        uint32_t n = (len > 256U) ? 256U : len;
        const uint8_t *w = buf;
        if (off < HDR_FLAG + 4U && off + n > HDR_FLAG) {
            /* 头部 flag 区保留擦除态, 提交阶段才置有效 */
            uint8_t p[256];
            uint8_t k;
            memcpy(p, buf, n);
            for (k = 0; k < 4U; k++) {
                uint32_t idx = HDR_FLAG - off + k;
                if (idx < n) p[idx] = 0xFFU;
            }
            w = p;
        }
        if (SfudFlash_StartWritePage(base + off, w, n) != 0) return -1;
        wait_idle();
        off += n; buf += n; len -= n;
    }
    return 0;
}

int LangMarkValid(uint32_t base)
{
    uint8_t cur[4];
    uint8_t pg[256];
    uint32_t pg_addr = base + (HDR_FLAG & 0xFFFFFF00U);   /* 256B 对齐页 */
    uint8_t i = (uint8_t)(HDR_FLAG & 0xFFU);
    if (SfudFlash_Read(base + HDR_FLAG, cur, 4) != 0) return -1;
    if (cur[0] != 0xFFU || cur[1] != 0xFFU || cur[2] != 0xFFU || cur[3] != 0xFFU) return -1;  /* flag 区须未被写 */
    if (SfudFlash_Read(pg_addr, pg, sizeof(pg)) != 0) return -1;
    /* LANG_FLAG_READY=0xA5A50001 小端存储: 低址字节在低地址 → 01 00 A5 A5 */
    pg[i] = 0x01U; pg[i + 1] = 0x00U; pg[i + 2] = 0xA5U; pg[i + 3] = 0xA5U;
    if (SfudFlash_StartWritePage(pg_addr, pg, sizeof(pg)) != 0) return -1;   /* 写页须 256B 对齐 */
    wait_idle();
    return 0;
}