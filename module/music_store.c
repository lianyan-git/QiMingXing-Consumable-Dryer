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
#include "board.h"       /* Watchdog_Kick */
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

static uint32_t s_up_off    = 0;   /* written bytes (relative to data base) */
static uint32_t s_up_total  = 0;

/* 逐包写 FSM（与 lang_ota 同构）：状态经 Poll 每个主循环推进一次
 * 0=空闲 1=擦kick 2=擦wait 3=页kick 4=页wait；busy 让出不等锁 */
static uint8_t  s_st        = 0;
static uint8_t  s_hold      = 0;       /* SysFlashOp 已被本模块持有 */
static uint8_t  s_pkt[1024];
static uint32_t s_wr_off    = 0;       /* 本包写入起点(相对数据区) */
static uint32_t s_wr_end    = 0;       /* 本包尾(不含) */
static uint32_t s_pgd_off   = 0;       /* 当前页写点(相对数据区) */
static uint32_t s_pg_len    = 0;       /* 当前页长(1..256) */
static uint32_t s_pgn_off   = 0;       /* 当前擦的扇区序号 */
static uint32_t s_need_sec  = 0;       /* 本包需要的最后扇区 */
static uint32_t s_max_sec   = 0xFFFFFFFFUL;   /* 已擦到最高扇区 */

static uint32_t g_hdr_chk(const MusicGlobalHdr_t *h)
{
    return ~(h->magic ^ h->version ^ h->size ^ h->crc32);
}

/* ??屾ョ?? WIP 娓??浂锛???��?嬪姟锛256B页�?欐?4KB????尯?摝锛?1~2s灏?顶�? */
static int flash_wait_idle(void)
{
    uint8_t sr1;
    uint32_t t0 = SystemTime_Millis();
    for (;;) {
        Watchdog_Kick();
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
        Watchdog_Kick();
        r = SysFlashOp_TryBegin();
        if (r || (uint32_t)(SystemTime_Millis() - t0) > 2000U) break;
    }
    if (!r) return -1;
    for (;;) {
        Watchdog_Kick();
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
    s_up_off = 0; s_up_total = 0;
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
    s_has_fw   = 0;
    s_st = 0; s_hold = 0; s_max_sec = 0xFFFFFFFFUL;
    return 0;
}

/* 整包拷贝进缓冲，置写 FSM。返回 0=已排入（写由 Poll 推进）。 */
int MusicStore_WritePacket(const uint8_t *pkt, uint32_t len)
{
    if (!s_init || len == 0 || len > MUSIC_PKT_MAX) return -1;
    if (s_st != 0) return -1;                     /* 上一包未写完: 拒绝(调用方应等 Poll) */
    if (s_up_off + len > s_up_total) return -1;
    memcpy(s_pkt, pkt, len);
    s_wr_off  = s_up_off;
    s_wr_end  = s_up_off + len;
    s_need_sec = (s_wr_end - 1U) >> 12;
    s_pgn_off  = (s_max_sec == 0xFFFFFFFFUL) ? (s_wr_off >> 12) : (s_max_sec + 1U);
    s_pgd_off  = s_wr_off;
    s_st = 1;                                     /* 进入擦/写 FSM */
    return 0;
}

uint8_t MusicStore_WriteBusy(void)
{
    return (s_st != 0);
}

/* 每主循环推进一次写 FSM。返回 1=本包写完, 0=进行中, -1=失败 */
int MusicStore_Poll(void)
{
    uint8_t sr1;
    int r;

    if (!s_init || s_st == 0) return 0;

    switch (s_st) {
    case 1:   /* 擦 kick：擦本包覆盖的所有未擦扇区 */
        if (s_pgn_off > s_need_sec) { s_st = 3; return 0; }
        if (!s_hold) {
            if (!SysFlashOp_TryBegin()) return 0;
            s_hold = 1;
        }
        r = SfudFlash_StartEraseSector(MUSIC_DATA_BASE + (s_pgn_off << 12));
        if (r == 1) return 0;                      /* busy：等下一轮 */
        if (r != 0) { SysFlashOp_Release(); s_hold = 0; s_st = 0; return -1; }
        s_st = 2;
        return 0;
    case 2:   /* 擦 wait */
        if (SfudFlash_ReadSR1(&sr1) != 0) return 0;
        if (sr1 & 0x01U) return 0;
        SysFlashOp_Release(); s_hold = 0;
        s_max_sec = s_pgn_off;
        s_pgn_off++;
        s_st = 1;
        return 0;
    case 3:   /* 页 kick */
        if (s_pgd_off >= s_wr_end) {               /* 本包写完 */
            s_up_off = s_wr_end;
            s_st = 0;
            return 1;
        }
        if (!s_hold) {
            if (!SysFlashOp_TryBegin()) return 0;
            s_hold = 1;
        }
        s_pg_len = s_wr_end - s_pgd_off;
        if (s_pg_len > 256U) s_pg_len = 256U;
        r = SfudFlash_StartWritePage(MUSIC_DATA_BASE + s_pgd_off,
                                     s_pkt + (s_pgd_off - s_wr_off), (uint16_t)s_pg_len);
        if (r == 1) return 0;                      /* busy */
        if (r != 0) { SysFlashOp_Release(); s_hold = 0; s_st = 0; return -1; }
        s_st = 4;
        return 0;
    case 4:   /* 页 wait */
        if (SfudFlash_ReadSR1(&sr1) != 0) return 0;
        if (sr1 & 0x01U) return 0;
        SysFlashOp_Release(); s_hold = 0;
        s_pgd_off += s_pg_len;
        s_st = 3;
        return 0;
    default:
        s_st = 0;
        return -1;
    }
}

/* Finish: 校验已完成字节数 + 写全局头, 全程同步。返回 0=成功 */
int MusicStore_Finish(uint32_t full_crc, uint32_t size)
{
    MusicGlobalHdr_t h;
    uint8_t b[sizeof(h)];

    if (!s_init || size != s_up_total || size == 0) return -1;
    if (s_up_off != s_up_total) return -1;   /* 数据未全部落盘: 拒绝提交 */

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
    s_up_off = 0; s_up_total = 0;
    return 0;
}

void MusicStore_Wipe(void)
{
    if (!s_init) return;
    s_up_total = 0; s_up_off = 0;
    /* 只擦全局头扇区: 头失效后 TrackCount=0, 列表即空; 数据区留待下次上传按需再擦 */
    if (flash_erase_sync(MUSIC_FLASH_BASE) == 0) {
        s_has_fw = 0;
        s_file_size = 0;
    }
}

void MusicStore_AbortUpload(void)
{
    s_up_off = 0; s_up_total = 0;
    s_has_fw = (s_file_size > 0);
    /* 复位写 FSM: 若上次上传在半途被中断(关闭/失败/超时), s_st 残留非0
     * 会导致 MusicStore_WriteBusy() 恒真, 下次握手字节全被 FeedByte 吞掉→卡0 */
    s_st = 0;
    s_hold = 0;
    s_max_sec = 0xFFFFFFFFUL;
    s_wr_off = s_wr_end = s_pgd_off = s_pg_len = 0;
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