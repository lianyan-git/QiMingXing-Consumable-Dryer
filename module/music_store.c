/*
 * music_store.c ?? ?Ÿ³ä¹å›ºä»¶å?–é?? Flash å­˜å?¨ä?å?—è¡ï????é˜»?ŞÍ??
 * ????Œºï¼šMUSIC_FLASH_BASE=0xC20000??
 *   [????Œº0 4KB] ??¨å??å¤ {magic,version,size,crc32,checksum}
 *   [0xC21000..] ?•´??? .mubï¼??•°?åŒºï¼Œçº¿æ§??™ï?Œæ?‰æ???Œº??‰é???“¦ï¼?
 * ?‰æ?‰æ“¦/??™ç?? SysFlashOp ä¸å???•°ä¿å?/?›²çº¿æ?’ä?–ã?
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

/* MUB ???ä»¶å¤´?›ºå® 0x24ï¼36Bï¼‰ï?šmagic4+version4+track_count2+flags2+file_size4+crc4+name_off4+name_size4+track_off4+reserved4 */
#define MUB_HDR_SIZE       0x24U
#define MUB_TRACK_ENTRY    16U

typedef struct {
    uint32_t magic;      /* 'MUS1' */
    uint32_t version;
    uint32_t size;       /* .mub å­—è???? */
    uint32_t crc32;      /* ??¨æ??ä» CRC32ï¼????ç»?å¼ï¼? */
    uint32_t checksum;   /* magic^version^size^crc32 ?????ç? */
} MusicGlobalHdr_t;

static uint8_t  s_init    = 0;
static uint8_t  s_has_fw  = 0;
static uint32_t s_file_size = 0;

/* ä¸Šä?? FSMï¼ˆstï¼‰ï??0=ç©ºé—² 1=?“¦ç­‰å? 2=??‘å?™é? 3=é¡µå?™ç?‰å??
 *                4=å¤´æ“¦ç­‰å?? 5=å¤´å?™ç?‰å??ï¼ˆFinish ??è?›å?¥ï?? */
static uint8_t  s_st        = 0;
static uint8_t  s_page[256];
static uint16_t s_page_len  = 0;
static uint32_t s_up_off    = 0;   /* written bytes (relative to data base) */
static uint32_t s_up_total  = 0;
static uint8_t  s_erased_sect = 0xFF;  /* last erased data sector index (data-internal), 0xFF=none */
static uint32_t s_st_t0     = 0;
static uint32_t s_up_crc    = 0xFFFFFFFFUL;

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

/* ??Œæ¥ç?? WIP æ¸??›¶ï¼???­ä?‹åŠ¡ï¼256Bé¡µå?™æ?4KB????Œº?“¦ï¼?1~2så°?é¡¶ï? */
static int flash_wait_idle(void)
{
    uint8_t sr1;
    uint32_t t0 = SystemTime_Millis();
    for (;;) {
        if (SfudFlash_ReadSR1(&sr1) == 0 && !(sr1 & 0x01U)) return 0;
        if ((uint32_t)(SystemTime_Millis() - t0) > 4000U) return -1;
    }
}

/* ??Œæ¥å?™ä¸é¡µï¼?å¸ SysFlashOp ?‹å? ï?Œé??æ»çº¿å?™é?è?•ï?? */
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

/* ??Œæ¥æ“¦ä¸????Œºï¼?å¸? SysFlashOp ?‹å? ï?? */
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

/* è¿”å?æœå¸?”¶å­—è???•°ï¼?é¡µæ»¡?—¶è°??”¨?–¹?? Poll ?½??˜å?ç»­ä¼ å?©ä?™ï?? */
uint32_t MusicStore_Write(const uint8_t *buf, uint32_t len)
{
    if (!s_init) return len;
    while (len) {
        uint16_t take;
        if (s_page_len >= MUSIC_PAGE_SIZE) break;   /* é¡µæ»¡ï¼Œè?”å?å?©ä? */
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

/* å½“å?å????™é¡µ????œ¨????Œºç»å¯¹????Œº?·ï¼??•°?åŒº???ï¼ */
static uint8_t cur_sector(void)
{
    return (uint8_t)(s_up_off / MUSIC_SECTOR_SIZE);
}

/* é¡µç?“å?²è½??˜çŠ¶????œºï¼?æ¯ä¸»å¾ªçè???”¨ï¼ */
void MusicStore_Poll(void)
{
    uint8_t sr1;
    uint32_t now;
    int r;

    if (!s_init || s_up_total == 0) return;
    now = SystemTime_Millis();

    for (;;) {
        switch (s_st) {
        case 0:
            if (s_page_len >= MUSIC_PAGE_SIZE) { s_st = 2; continue; }   /* ?•´é¡µå????? */
            return;

        case 2:   /* ?œè????‘å?™é¡µï¼šå??ç¡ä¿æ???œ¨????Œºå·²æ? */
            if (s_erased_sect != cur_sector()) {
                s_st = 1;        /* è¿›å?¥æ“¦????? */
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

        case 1:   /* ?“¦?•°?æ???Œºï¼?ç»å¯¹????Œº?· cur_sectorï¼ */
            if (!SysFlashOp_TryBegin()) return;
            r = SfudFlash_StartEraseSector(MUSIC_DATA_BASE + (uint32_t)cur_sector() * MUSIC_SECTOR_SIZE);
            if (r == 1) { SysFlashOp_Release(); return; }
            if (r != 0) { SysFlashOp_Release(); MusicStore_AbortUpload(); return; }
            s_erased_sect = cur_sector();
            s_st_t0 = now;
            s_st = 6;   /* ?? WIP ç­‰å?? */
            continue;

        case 6:   /* ?? WIP ç­‰ï???•°?®????Œºï¼ */
            if (now - s_st_t0 > MUSIC_ERASE_TMO) { SysFlashOp_Release(); MusicStore_AbortUpload(); return; }
            if (flash_idle(&sr1) != 0) return;
            SysFlashOp_Release();
            s_st = 2;   /* ??å?™é? */
            continue;

        case 3:   /* é¡µå?? WIP ç­ */
            if (now - s_st_t0 > MUSIC_PAGE_TMO) { SysFlashOp_Release(); MusicStore_AbortUpload(); return; }
            if (flash_idle(&sr1) != 0) return;
            SysFlashOp_Release();
            s_up_off  += s_page_len;
            s_page_len = 0;
            s_st = 0;
            continue;

        default:
            s_st = 0;
            continue;
        }
    }
}

/* Finishï¼šå?¨å???½?? + ??™å?¨å±å¤´ï¼???Œæ¥ç?­ä?‹åŠ¡ï¼Œè?”å? 0=??å??ï¼ */
int MusicStore_Finish(uint32_t full_crc, uint32_t size)
{
    MusicGlobalHdr_t h;
    uint8_t b[sizeof(h)];

    if (!s_init || size != s_up_total || size == 0) return -1;


    /* æ®‹ç?™å?Šé¡µ?½??˜ï????Œæ­¥ï¼? */
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

    /* ??™å?¨å±å¤´ï¼?????Œº0ï¼‰â”â”å???“¦??å?™ï?Œä?è???›¿?¢?—§?›ºä» */
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

void MusicStore_AbortUpload(void)
{
    s_st = 0; s_page_len = 0; s_up_off = 0; s_up_total = 0;
    s_has_fw = (s_file_size > 0);
}

/* ---- ??—è¡¨ / è¯»å?? ---- */

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