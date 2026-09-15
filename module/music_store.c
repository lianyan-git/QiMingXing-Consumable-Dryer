/*
 * music_store.c ?? ?煶乐固件�?栭?? Flash 存�?ㄤ?庡?楄★????為樆?尥??
 * ????尯：MUSIC_FLASH_BASE=0xC20000??
 *   [????尯0 4KB] ??ㄥ??澶 {magic,version,size,crc32,checksum}
 *   [0xC21000..] ?暣??? .mub锛??暟?嶅尯，线妲??欙?屾?夋???尯??夐???摝锛?
 * ?夋?夋摝/??欑?? SysFlashOp 与�???暟保�?/?洸线�?掍?栥?
 */
#include "music_store.h"
#include "music_format.h"
#include "sfud_flash.h"
#include "system_config.h"
#include "system_time.h"
#include <string.h>

#define MUSIC_SECTOR_SIZE  UINT32_C(0x1000)
#define MUSIC_PAGE_SIZE    UINT32_C(0x100)
#define MUSIC_HDR_VERSION  1U
#define MUSIC_ERASE_TMO    4000U
#define MUSIC_PAGE_TMO     1500U

/* MUB ???件头?浐瀹 0x24锛36B）�?歮agic4+version4+track_count2+flags2+file_size4+crc4+name_off4+name_size4+track_off4+reserved4 */
#define MUB_HDR_SIZE       0x24U
#define MUB_TRACK_ENTRY    16U

typedef struct {
    uint32_t magic;      /* 'MUS1' */
    uint32_t version;
    uint32_t size;       /* .mub 字�???? */
    uint32_t crc32;      /* ??ㄦ??浠 CRC32锛????缁?寮锛? */
    uint32_t checksum;   /* magic^version^size^crc32 ?????嶇? */
} MusicGlobalHdr_t;

static uint8_t  s_init    = 0;
static uint8_t  s_has_fw  = 0;
static uint32_t s_file_size = 0;

/* 上�?? FSM（st）�??0=空闲 1=?摝等�? 2=??戝?欓? 3=页�?欑?夊??
 *                4=头擦等�?? 5=头�?欑?夊??（Finish ??庤?涘?ワ?? */
static uint8_t  s_st        = 0;
static uint8_t  s_page[256];
static uint16_t s_page_len  = 0;
static uint32_t s_up_off    = 0;   /* written bytes (relative to data base) */
static uint32_t s_up_total  = 0;
static uint8_t  s_erased_sect = 0xFF;  /* last erased data sector index (data-internal), 0xFF=none */
static uint32_t s_st_t0     = 0;
static uint32_t s_up_crc    = 0xFFFFFFFFUL;

/* pre-erase at session start: header + old data sectors, so data write never stalls on erase */
static uint8_t  s_pre_active = 0;
static uint8_t  s_pre_total  = 0;
static uint8_t  s_pre_cur    = 0;

static uint32_t g_hdr_chk(const MusicGlobalHdr_t *h)
{
    return ~(h->magic ^ h->version ^ h->size ^ h->crc32);
}

static int flash_idle(uint8_t *sr1)
{
    if (SfudFlash_ReadSR1(sr1) != 0) return -1;
    return (int)(*sr1 & 0x01U);
}

static uint32_t crc_byte(uint32_t crc, uint8_t b)
{
    crc ^= b;
    for (int i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
    return crc;
}

/* ??屾ョ?? WIP 娓??浂锛???��?嬪姟锛256B页�?欐?4KB????尯?摝锛?1~2s灏?顶�? */
static int flash_wait_idle(void)
{
    uint8_t sr1;
    uint32_t t0 = SystemTime_Millis();
    for (;;) {
        if (SfudFlash_ReadSR1(&sr1) == 0 && !(sr1 & 0x01U)) return 0;
        if ((uint32_t)(SystemTime_Millis() - t0) > 4000U) return -1;
    }
}

/* ??屾ュ?欎搁〉锛?甯 SysFlashOp ?嬪?狅?岄??婊线�?欓?嶈?曪?? */
static int flash_write_page_sync(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    int r;
    uint32_t t0 = SystemTime_Millis();
    for (;;) {
        r = SysFlashOp_TryBegin();
        if (r || (uint32_t)(SystemTime_Millis() - t0) > 2000U) break;
    }
    if (!r) return -1;
    for (;;) {
        r = SfudFlash_StartWritePage(addr, buf, len);
        if (r == 1) { SysFlashOp_Release(); continue; }
        if (r != 0) { SysFlashOp_Release(); return -1; }
        break;
    }
    if (flash_wait_idle() != 0) { SysFlashOp_Release(); return -1; }
    SysFlashOp_Release();
    return 0;
}

/* ??屾ユ摝涓????尯锛?甯? SysFlashOp ?嬪?狅?? */
static int flash_erase_sync(uint32_t addr)
{
    int r;
    uint32_t t0 = SystemTime_Millis();
    for (;;) {
        r = SysFlashOp_TryBegin();
        if (r || (uint32_t)(SystemTime_Millis() - t0) > 2000U) break;
    }
    if (!r) return -1;
    for (;;) {
        r = SfudFlash_StartEraseSector(addr);
        if (r == 1) { SysFlashOp_Release(); continue; }
        if (r != 0) { SysFlashOp_Release(); return -1; }
        break;
    }
    if (flash_wait_idle() != 0) { SysFlashOp_Release(); return -1; }
    SysFlashOp_Release();
    return 0;
}

void MusicStore_Init(void)
{
    MusicGlobalHdr_t h;
    uint8_t b[sizeof(h)];
    s_init = 1;
    s_has_fw = 0;
    s_file_size = 0;
    s_st = 0; s_page_len = 0; s_up_off = 0; s_up_total = 0;
    memset(b, 0xFF, sizeof(b));
    if (SfudFlash_Read(MUSIC_FLASH_BASE, b, sizeof(h)) == 0) {
        memcpy(&h, b, sizeof(h));
        if (h.magic == MUSIC_HDR_MAGIC && h.version == MUSIC_HDR_VERSION &&
            h.checksum == g_hdr_chk(&h) && h.size > 0 &&
            h.size <= MUSIC_MAX_FILE_SIZE) {
            s_has_fw = 1;
            s_file_size = h.size;

        }
    }
}

int MusicStore_HasFirmware(void)   { return s_init && s_has_fw; }
uint32_t MusicStore_FileSize(void) { return s_file_size; }
uint32_t MusicStore_WrittenOff(void) { return s_up_off; }

int MusicStore_BeginUpload(uint32_t size)
{
    if (!s_init || size == 0 || size > MUSIC_MAX_FILE_SIZE) return -1;
    s_up_total = size;
    s_up_off   = 0;
    s_page_len = 0;
    s_up_crc   = 0xFFFFFFFFUL;
    s_st       = 0;
    s_erased_sect = 0xFF;
    s_has_fw   = 0;
    return 0;
}

/* 返�?炴滃惛?敹字�???暟锛?页满?椂璋??敤?柟?? Poll ?惤??樺?嶇画传�?╀?欙?? */
uint32_t MusicStore_Write(const uint8_t *buf, uint32_t len)
{
    if (!s_init) return len;
    if (s_pre_active) return len;   /* pre-erase not done: reject data (caller spills and waits for Poll) */
    while (len) {
        uint16_t take;
        if (s_page_len >= MUSIC_PAGE_SIZE) break;   /* 页满，�?斿?炲?╀? */
        take = (uint16_t)(MUSIC_PAGE_SIZE - s_page_len);
        if ((uint32_t)take > len) take = (uint16_t)len;
        memcpy(s_page + s_page_len, buf, take);
        for (uint32_t i = 0; i < take; i++) s_up_crc = crc_byte(s_up_crc, buf[i]);
        s_page_len = (uint16_t)(s_page_len + take);
        buf += take;
        len -= take;
    }
    return len;
}

/* 当�?嶅????欓〉????湪????尯绝对????尯?彿锛??暟?嶅尯???锛 */
static uint8_t cur_sector(void)
{
    return (uint8_t)(s_up_off / MUSIC_SECTOR_SIZE);
}

/* 页�?撳?茶惤??樼姸????満锛?每主循鐜�???敤锛 */
void MusicStore_Poll(void)
{
    uint8_t sr1;
    uint32_t now;
    int r;

    if (!s_init) return;
    now = SystemTime_Millis();

    for (;;) {
        switch (s_st) {
        case 0:
            if (s_pre_active) { s_st = 7; continue; }   /* pre-erase in progress */
            if (s_page_len > 0) { s_st = 2; continue; } /* 有数据即写(含不足一页的残页, 否则最后一包 ACK 永不发) */
            return;

        case 2:   /* ?滆????戝?欓〉：�??纭保�???湪????尯已�? */
            if (s_erased_sect != cur_sector()) {
                s_st = 1;        /* 进�?ユ摝????? */
                continue;
            }
            if (!SysFlashOp_TryBegin()) return;
            r = SfudFlash_StartWritePage(MUSIC_DATA_BASE + s_up_off,
                                         s_page, (uint32_t)s_page_len);
            if (r == 1) { SysFlashOp_Release(); return; }
            if (r != 0) { SysFlashOp_Release(); MusicStore_AbortUpload(); return; }
            s_st    = 3;
            s_st_t0 = now;
            continue;

        case 1:   /* ?摝?暟?嶆???尯锛?绝对????尯?彿 cur_sector锛 */
            if (!SysFlashOp_TryBegin()) return;
            r = SfudFlash_StartEraseSector(MUSIC_DATA_BASE + (uint32_t)cur_sector() * MUSIC_SECTOR_SIZE);
            if (r == 1) { SysFlashOp_Release(); return; }
            if (r != 0) { SysFlashOp_Release(); MusicStore_AbortUpload(); return; }
            s_erased_sect = cur_sector();
            s_st_t0 = now;
            s_st = 6;   /* ?? WIP 等�?? */
            continue;

        case 6:   /* ?? WIP 等�???暟?嵁????尯锛 */
            if (now - s_st_t0 > MUSIC_ERASE_TMO) { SysFlashOp_Release(); MusicStore_AbortUpload(); return; }
            if (flash_idle(&sr1) != 0) return;
            SysFlashOp_Release();
            s_st = 2;   /* ??炲?欓? */
            continue;

        case 3:   /* 页�?? WIP 绛 */
            if (now - s_st_t0 > MUSIC_PAGE_TMO) { SysFlashOp_Release(); MusicStore_AbortUpload(); return; }
            if (flash_idle(&sr1) != 0) return;
            SysFlashOp_Release();
            s_up_off  += s_page_len;
            s_page_len = 0;
            s_st = 0;
            continue;

        case 7:   /* pre-erase: kick current sector erase (header -> old data sectors) */
            if (!SysFlashOp_TryBegin()) return;
            {
                uint32_t a = (s_pre_cur == 0) ? MUSIC_FLASH_BASE
                             : MUSIC_DATA_BASE + ((uint32_t)(s_pre_cur - 1U)) * MUSIC_SECTOR_SIZE;
                r = SfudFlash_StartEraseSector(a);
                if (r == 1) { SysFlashOp_Release(); return; }
                if (r != 0) { SysFlashOp_Release(); s_pre_active = 0; s_st = 0; continue; }
                s_st_t0 = now;
                s_st = 8;
            }
            continue;

        case 8:   /* pre-erase WIP wait */
            if (now - s_st_t0 > MUSIC_ERASE_TMO) { SysFlashOp_Release(); s_pre_active = 0; s_st = 0; return; }
            if (flash_idle(&sr1) != 0) return;
            SysFlashOp_Release();
            s_pre_cur++;
            if (s_pre_cur >= s_pre_total) {
                s_pre_active = 0;
                s_has_fw = 0;
                s_file_size = 0;
                s_st = 0;
            }
            continue;

        default:
            s_st = 0;
            continue;
        }
    }
}

/* Finish：�?ㄥ???惤?? + ??欏?ㄥ卞ご锛???屾ョ?��?嬪姟，�?斿? 0=??愬??锛 */
int MusicStore_Finish(uint32_t full_crc, uint32_t size)
{
    MusicGlobalHdr_t h;
    uint8_t b[sizeof(h)];

    if (!s_init || size != s_up_total || size == 0) return -1;


    /* 残�?欏?婇〉?惤??橈????屾�ワ�? */
    if (s_page_len > 0) {
        if (s_erased_sect != cur_sector()) {
            if (flash_erase_sync(MUSIC_DATA_BASE + (uint32_t)cur_sector() * MUSIC_SECTOR_SIZE) != 0)
                return -1;
            s_erased_sect = cur_sector();
        }
        if (flash_write_page_sync(MUSIC_DATA_BASE + s_up_off, s_page, s_page_len) != 0)
            return -1;
        s_up_off += s_page_len;
        s_page_len = 0;
    }

    /* ??欏?ㄥ卞ご锛?????尯0）鈹鈹�???摝??庡?欙?屼?濊???浛?崲?棫?浐浠 */
    h.magic = MUSIC_HDR_MAGIC;
    h.version = MUSIC_HDR_VERSION;
    h.size = size;
    h.crc32 = full_crc;
    h.checksum = g_hdr_chk(&h);
    memcpy(b, &h, sizeof(h));
    if (flash_erase_sync(MUSIC_FLASH_BASE) != 0) return -1;
    if (flash_write_page_sync(MUSIC_FLASH_BASE, b, sizeof(h)) != 0) return -1;

    s_has_fw = 1;
    s_file_size = size;
    s_up_off = 0; s_up_total = 0; s_st = 0;
    return 0;
}

void MusicStore_Wipe(void)
{
    if (!s_init) return;
    s_st = 0; s_up_total = 0; s_page_len = 0; s_up_off = 0;
    s_pre_active = 0; s_pre_total = 0; s_pre_cur = 0;
    /* 只擦全局头扇区: 头失效后 TrackCount=0, 列表即空; 数据区留待下次上传按需再擦 */
    if (flash_erase_sync(MUSIC_FLASH_BASE) == 0) {
        s_has_fw = 0;
        s_file_size = 0;
    }
}

void MusicStore_AbortUpload(void)
{
    s_st = 0; s_page_len = 0; s_up_off = 0; s_up_total = 0;
    s_has_fw = (s_file_size > 0);
}


int MusicStore_WipeForSize(uint32_t size)
{
    uint32_t n = (size + MUSIC_SECTOR_SIZE - 1U) / MUSIC_SECTOR_SIZE;
    uint32_t i;
    if (n > 32U) n = 32U;   /* 上限 128KB, 实际 .mub 很小 */
    if (flash_erase_sync(MUSIC_FLASH_BASE) != 0) return -1;   /* 头扇区 */
    for (i = 0; i < n; i++) {
        if (flash_erase_sync(MUSIC_DATA_BASE + i * MUSIC_SECTOR_SIZE) != 0) return -1;
        s_has_fw = 0; s_file_size = 0;
    }
    s_pre_active = 0; s_pre_total = 0; s_pre_cur = 0;
    return 0;
}

void MusicStore_PrepareWipe(void)
{
    uint32_t n;
    if (!s_init) return;
    if (!s_has_fw || s_file_size == 0) return;   /* no old firmware: nothing to pre-erase */
    n = (s_file_size + MUSIC_SECTOR_SIZE - 1U) / MUSIC_SECTOR_SIZE;
    if (n > 32U) n = 32U;
    s_pre_total = (uint8_t)n + 1U;   /* +1 header sector */
    s_pre_cur   = 0;
    s_pre_active = 1;
    s_st = 7;
}

/* ---- ??楄〃 / 读�?? ---- */

uint16_t MusicStore_TrackCount(void)
{
    uint8_t m[4], b[2];
    uint16_t n;
    uint32_t want = MUSIC_FILE_MAGIC;
    if (!s_has_fw) return 0;
    if (SfudFlash_Read(MUSIC_DATA_BASE, m, 4) != 0) return 0;
    if (m[0] != (uint8_t)(want & 0xFF) || m[1] != (uint8_t)((want >> 8) & 0xFF) ||
        m[2] != (uint8_t)((want >> 16) & 0xFF) || m[3] != (uint8_t)((want >> 24) & 0xFF))
        return 0;
    if (SfudFlash_Read(MUSIC_DATA_BASE + 8, b, 2) != 0) return 0;
    n = (uint16_t)(b[0] | (b[1] << 8));
    if (n > MUSIC_MAX_TRACKS) n = MUSIC_MAX_TRACKS;
    return n;
}

int MusicStore_GetTrack(uint16_t idx, MusicTrackEntry_t *e)
{
    uint8_t b[MUB_TRACK_ENTRY];
    uint32_t off;
    if (!s_has_fw || idx >= MusicStore_TrackCount() || !e) return -1;
    off = MUB_HDR_SIZE + (uint32_t)idx * MUB_TRACK_ENTRY;
    if (SfudFlash_Read(MUSIC_DATA_BASE + off, b, MUB_TRACK_ENTRY) != 0) return -1;
    e->note_off = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    e->note_size = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    e->name_off = (uint32_t)b[8] | ((uint32_t)b[9] << 8) | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);
    e->name_len = (uint16_t)(b[12] | (b[13] << 8));
    e->reserved = 0;
    return 0;
}

int MusicStore_GetName(uint16_t idx, char *buf, uint16_t buflen)
{
    MusicTrackEntry_t e;
    uint16_t i;
    uint8_t ch;
    if (!s_has_fw || idx >= MusicStore_TrackCount() || buflen < 2) return -1;
    if (MusicStore_GetTrack(idx, &e) != 0) return -1;
    if (e.name_off >= s_file_size || e.name_off < MUB_HDR_SIZE) return -1;
    if (e.name_len > buflen - 1) e.name_len = (uint16_t)(buflen - 1);
    for (i = 0; i < e.name_len; i++) {
        if (SfudFlash_Read(MUSIC_DATA_BASE + e.name_off + i, &ch, 1) != 0) { buf[i] = 0; return -1; }
        buf[i] = (char)ch;
    }
    buf[i] = 0;
    return 0;
}

int MusicStore_GetNote(uint16_t track, uint32_t k, uint16_t *freq, uint16_t *dur)
{
    MusicTrackEntry_t e;
    uint8_t b[4];
    if (!s_has_fw || track >= MusicStore_TrackCount()) return -1;
    if (MusicStore_GetTrack(track, &e) != 0) return -1;
    if ((k + 1U) * 4U > e.note_size) return -1;
    if (SfudFlash_Read(MUSIC_DATA_BASE + e.note_off + k * 4U, b, 4) != 0) return -1;
    if (freq) *freq = (uint16_t)(b[0] | (b[1] << 8));
    if (dur)  *dur  = (uint16_t)(b[2] | (b[3] << 8));
    return 0;
}