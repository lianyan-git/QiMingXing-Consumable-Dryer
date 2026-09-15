/*
 * music_ota.c — 音乐固件 App 侧接收（0xAA 二进制帧 → MusicStore 外部 Flash）
 * 字节推进由 EspLink_Process 在“音乐接收态”喂入；Poll 推进落盘与超时。
 * 帧格式：
 *   握手：0xAA 0x55 0x11 [sizeBE4]           → 0x06 / 0x15
 *   数据：0xAA [seqBE2] [data] [crc16BE2] 0x55
 *   结束：0xAA 0x55 0x12 [crc32BE4]          → 0x06 / 0x15
 */
#include "music_ota.h"
#include "music_store.h"
#include "music_format.h"
#include "bsp_esp_uart.h"
#include "system_config.h"
#include "system_time.h"
#include <string.h>

#define MT_PKT_MAX   1024U
#define F_HS         0x11
#define F_END        0x12

enum {
    S_IDLE = 0,
    S_SY0,          /* 收到 0xAA */
    S_SY1,          /* 收到 0x55 */
    S_TYPE,         /* 类型字节 */
    S_SIZE1, S_SIZE2, S_SIZE3, S_SIZE4,
    S_PKT_AA, S_PKT_SEQH, S_PKT_SEQL, S_PKT_DATA, S_PKT_CRCH, S_PKT_CRCL, S_PKT_55,
    S_END_AA, S_END_55, S_END_TYPE, S_END_CRC1, S_END_CRC2, S_END_CRC3, S_END_CRC4
};

static uint8_t  s_st        = S_IDLE;
static uint32_t s_total     = 0;
static uint32_t s_recv      = 0;
static uint16_t s_seq       = 0;
static uint16_t s_exp_seq   = 0;
static uint32_t s_crc       = 0xFFFFFFFFUL;   /* 累计全文件 CRC32（最终取反） */
static uint8_t  s_pkt[MT_PKT_MAX];
static uint32_t s_pkt_len   = 0;
static uint32_t s_pkt_need  = 0;
static uint8_t  s_active    = 0;
static uint8_t  s_error     = 0;
static uint32_t s_last_act  = 0;
static uint8_t  s_crchi_tmp = 0;
static uint32_t s_crc_expect = 0;

/* spill：上一包未吸收字节，落盘后补 ACK */
static uint8_t  s_spill[MT_PKT_MAX];
static uint32_t s_spill_len = 0;
static uint32_t s_spill_off = 0;
static uint8_t  s_ack_pend  = 0;

static uint16_t crc16_modbus(uint16_t crc, uint8_t b)
{
    crc ^= b;
    for (int i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    return crc;
}

static void ack(uint8_t ok)
{
    uint8_t c = ok ? 0x06 : 0x15;
    EspUart_Write(&c, 1, 100);
}

/* 收流完成/失败：退出 EspLink 二进制路由，恢复正常行协议（+MUSICOK 等行可被解析） */
static void leave_active(void)
{
    s_active = 0;
    g_sys.music_ota_active = 0;
}

/* 失败统一处理: 必须 leave_active() 退出接收态.
 * 否则 s_active 卡 1 → EspLink 将后续 ESP 全部字节丢弃(WiFi/AP/弹窗全冻结). */
static void fail(void)
{
    leave_active();
    s_error = 1;
    s_st = S_IDLE;
    s_ack_pend = 0;
    s_spill_len = 0;
    s_spill_off = 0;
    g_sys.music_popup = 5;   /* 失败态: 帧循环自动收起 */
}

void MusicOta_Init(void)
{
    s_st = S_IDLE; s_active = 0; s_error = 0;
}

void MusicOta_Abort(void)
{
    MusicStore_AbortUpload();
    s_st = S_IDLE; s_active = 0; s_error = 1;
}

uint8_t MusicOta_Active(void) { return s_active; }
uint8_t MusicOta_Error(void)   { return s_error; }

/* 逐字节喂入 */
void MusicOta_FeedByte(uint8_t b)
{
    s_last_act = SystemTime_Millis();

    if (s_ack_pend) return;   /* 上一包尚未落盘：忽略新字节 */

    switch (s_st) {
    case S_IDLE:
        s_st = (b == 0xAA) ? S_SY0 : S_IDLE;
        break;
    case S_SY0:
        s_st = (b == 0x55) ? S_TYPE : (b == 0xAA ? S_SY0 : S_IDLE);
        break;
    case S_TYPE:
        if (b == F_HS) s_st = S_SIZE1;
        else s_st = (b == 0xAA) ? S_SY0 : S_IDLE;
        break;
    case S_SIZE1: s_total = (uint32_t)b << 24; s_st = S_SIZE2; break;
    case S_SIZE2: s_total |= (uint32_t)b << 16; s_st = S_SIZE3; break;
    case S_SIZE3: s_total |= (uint32_t)b << 8;  s_st = S_SIZE4; break;
    case S_SIZE4:
        s_total |= (uint32_t)b;
        if (s_total == 0 || s_total > MUSIC_MAX_FILE_SIZE) { ack(0); fail(); break; }
        if (MusicStore_BeginUpload(s_total) != 0) { ack(0); fail(); break; }
        /* 与 OTA 一致: 掌手后先擦净目标扇区(头+数据), 擦完才 ACK, ESP 才分包传 */
        if (MusicStore_WipeForSize(s_total) != 0) { ack(0); fail(); break; }
        s_recv = 0; s_exp_seq = 0; s_crc = 0xFFFFFFFFUL;
        s_pkt_len = 0; s_pkt_need = 0; s_active = 1; s_error = 0;
        g_sys.music_popup = 3;                  /* 弹窗切上传中 */
        g_sys.music_upload_total = s_total;
        g_sys.music_upload_recv = 0;
        g_sys.music_upload_pct = 0;
        ack(1);
        s_st = S_PKT_AA;
        break;

    case S_PKT_AA:
        s_pkt_len = 0;
        s_st = (b == 0xAA) ? S_PKT_SEQH : S_IDLE;
        break;
    case S_PKT_SEQH: s_seq = (uint16_t)b << 8; s_st = S_PKT_SEQL; break;
    case S_PKT_SEQL:
        s_seq |= b;
        if (s_seq != s_exp_seq) { ack(0); fail(); break; }
        {
            uint32_t remain = s_total - s_recv;
            s_pkt_need = (remain > MT_PKT_MAX) ? MT_PKT_MAX : remain;
        }
        if (s_pkt_need == 0) { ack(0); fail(); break; }
        s_st = S_PKT_DATA;
        break;
    case S_PKT_DATA:
        s_pkt[s_pkt_len++] = b;
        if (s_pkt_len >= s_pkt_need) s_st = S_PKT_CRCH;
        break;
    case S_PKT_CRCH:
        s_crchi_tmp = b;
        s_st = S_PKT_CRCL;
        break;
    case S_PKT_CRCL:
    {
        uint16_t crc = 0xFFFF;
        uint8_t crclo = b;
        crc = crc16_modbus(crc, (uint8_t)(s_seq >> 8));
        crc = crc16_modbus(crc, (uint8_t)(s_seq & 0xFF));
        for (uint32_t i = 0; i < s_pkt_len; i++) crc = crc16_modbus(crc, s_pkt[i]);
        if ((uint8_t)(crc >> 8) != s_crchi_tmp || (uint8_t)(crc & 0xFF) != crclo) {
            ack(0); fail(); break;
        }
        s_st = S_PKT_55;
        break;
    }
    case S_PKT_55:
        if (b == 0x55) {
            /* 累计全文件 CRC32 */
            for (uint32_t i = 0; i < s_pkt_len; i++) {
                s_crc ^= s_pkt[i];
                for (int j = 0; j < 8; j++) s_crc = (s_crc & 1) ? (s_crc >> 1) ^ 0xEDB88320UL : (s_crc >> 1);
            }
            {
                uint32_t left = MusicStore_Write(s_pkt, s_pkt_len);
                if (left > 0) {
                    memcpy(s_spill, s_pkt + (s_pkt_len - left), left);
                    s_spill_off = 0; s_spill_len = left;
                }
                s_ack_pend = 1;   /* 本包全部落盘后才补 ACK, 保证 ESP 严格等 ack 发包 */
            }
            s_recv += s_pkt_len;
            s_exp_seq++;
            g_sys.music_upload_recv = s_recv;
            g_sys.music_upload_pct = (uint8_t)(s_recv * 100U / s_total);
            s_st = (s_recv >= s_total) ? S_END_AA : S_PKT_AA;
        } else {
            ack(0); fail();
        }
        break;

    case S_END_AA: s_st = (b == 0xAA) ? S_END_55 : S_IDLE; break;
    case S_END_55: s_st = (b == 0x55) ? S_END_TYPE : (b == 0xAA ? S_END_AA : S_IDLE); break;
    case S_END_TYPE:
        if (b != F_END) { ack(0); fail(); break; }
        s_crc_expect = 0;
        s_st = S_END_CRC1;
        break;
    case S_END_CRC1: s_crc_expect = (uint32_t)b << 24; s_st = S_END_CRC2; break;
    case S_END_CRC2: s_crc_expect |= (uint32_t)b << 16; s_st = S_END_CRC3; break;
    case S_END_CRC3: s_crc_expect |= (uint32_t)b << 8;  s_st = S_END_CRC4; break;
    case S_END_CRC4:
        s_crc_expect |= (uint32_t)b;
        if (~s_crc != s_crc_expect) {
            leave_active();
            ack(0); s_error = 1; s_st = S_IDLE; break;
        }
        if (MusicStore_Finish(~s_crc, s_total) != 0) {
            leave_active();
            ack(0); s_error = 1; s_st = S_IDLE; break;
        }
        g_sys.music_upload_pct = 100;
        g_sys.music_popup = 4;      /* 完成态：帧循环 1.8s 后自动收起 */
        g_sys.ui_force_redraw = 1;
        ack(1);
        leave_active();
        s_error = 0;
        s_st = S_IDLE;
        break;

    default:
        s_st = S_IDLE;
        break;
    }
}

void MusicOta_Poll(void)
{
    MusicStore_Poll();

    /* spill 续写; 本包数据全部落盘(缓冲空且写进度追上)后才补 ACK */
    if (s_ack_pend) {
        if (s_spill_len > 0) {
            uint32_t left = MusicStore_Write(s_spill + s_spill_off, s_spill_len - s_spill_off);
            if (left == 0) {
                s_spill_len = 0; s_spill_off = 0;
            } else {
                s_spill_off = s_spill_len - left;
            }
        }
        if (s_spill_len == 0 && MusicStore_WrittenOff() >= s_recv) {
            s_ack_pend = 0;
            ack(1);
        }
    }

    if (s_active && (uint32_t)(SystemTime_Millis() - s_last_act) > 10000U) {
        MusicStore_AbortUpload();
        leave_active();
        s_error = 1; s_st = S_IDLE;
        s_ack_pend = 0; s_spill_len = 0;
        g_sys.music_popup = 5;    /* 下载失败提示 */
    }
}