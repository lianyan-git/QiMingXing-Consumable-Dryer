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
static uint8_t  s_wr_pend   = 0;   /* 1=已排队写盘, 待 Poll 完成后补 ACK */
static uint32_t s_last_act  = 0;
static uint8_t  s_crchi_tmp = 0;
static uint32_t s_crc_expect = 0;

/* 调试计数（与 lang_ota 同构）：卡 0% 时据此定位断点——
 * GotHand=0 → 握手没完成；GotHand=1 且 Recv=0 → 第一个包未走完 写入→ACK；
 * Recv>0 而界面不动 → UI/ESP 侧问题。
 * s_fail=失败原因: 0无 1=10s超时 2=UART溢出中止 3=BeginUpload拒 4=写盘FSM失败
 *              5=size非法 6=END-CRC不符 7=Finish(落盘校验)失败 */
static uint8_t  s_dbg_hand = 0;
static uint16_t s_dbg_ack  = 0;
static uint16_t s_dbg_nak  = 0;
static uint8_t  s_fail     = 0;

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
    if (ok) { if (s_dbg_ack < 65535U) s_dbg_ack++; }
    else    { if (s_dbg_nak < 65535U) s_dbg_nak++; }
}

/* 收流完成/失败：退出 EspLink 二进制路由，恢复正常行协议（+MUSICOK 等行可被解析） */
static void leave_active(void)
{
    s_active = 0;
    g_sys.music_ota_active = 0;
}

/* 失败统一处理: 必须 leave_active() 退出接收态.
 * 否则 s_active 卡 1 → EspLink 将后续 ESP 全部字节丢弃(WiFi/AP/弹窗全冻结). */
static void fail(uint8_t reason)
{
    MusicStore_AbortUpload();   /* 复位写 FSM, 防止残留阻塞下次握手 */
    leave_active();
    s_error = 1;
    s_fail = reason;
    s_st = S_IDLE;
    s_wr_pend = 0;
    g_sys.music_popup = 5;   /* 失败态: 帧循环自动收起 */
}

void MusicOta_Init(void)
{
    s_st = S_IDLE; s_active = 0; s_error = 0; s_wr_pend = 0; s_fail = 0;
}

void MusicOta_Abort(void)
{
    if (s_active) s_fail = 2;   /* 会话进行中被外部清掉: 仅溢出中止路径会发生 */
    MusicStore_AbortUpload();
    s_st = S_IDLE; s_active = 0; s_error = 1; s_wr_pend = 0;
}

uint8_t MusicOta_Active(void) { return s_active; }
uint8_t MusicOta_Error(void)   { return s_error; }
uint8_t  MusicOta_GotHand(void) { return s_dbg_hand; }
uint16_t MusicOta_GetAck(void)  { return s_dbg_ack; }
uint16_t MusicOta_GetNak(void)  { return s_dbg_nak; }
uint32_t MusicOta_GetRecv(void) { return s_recv; }
uint32_t MusicOta_GetTotal(void) { return s_total; }
uint8_t  MusicOta_GetState(void) { return s_st; }
uint8_t  MusicOta_GetFail(void)  { return s_fail; }

/* 逐字节喂入 */
void MusicOta_FeedByte(uint8_t b)
{
    s_last_act = SystemTime_Millis();

    /* 写盘 FSM 进行中: 忽略新字节(与 lang_ota 一致, ESP 等我们 ACK 不会发包) */
    if (MusicStore_WriteBusy()) return;

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
        if (s_total == 0 || s_total > MUSIC_MAX_FILE_SIZE) { ack(0); fail(5); break; }
        /* 与语言上传同构: 握手立即 ACK, 擦除延迟到每包 Poll 懒做 */
        if (MusicStore_BeginUpload(s_total) != 0) { ack(0); fail(3); break; }
        s_recv = 0; s_exp_seq = 0; s_crc = 0xFFFFFFFFUL;
        s_pkt_len = 0; s_pkt_need = 0; s_active = 1; s_error = 0; s_fail = 0;
        s_dbg_hand = 1; s_dbg_ack = 0; s_dbg_nak = 0;   /* 调试计数按本次会话清零 */
        EspUart_ResetOverflow();  /* 新会话起点: 丢弃会话建立前粘滞的旧溢出标志, 防止刚握手就被误杀 */
        g_sys.music_popup = 3;                  /* 弹窗切上传中 */
        g_sys.music_upload_total = s_total;
        g_sys.music_upload_recv = 0;
        g_sys.music_upload_pct = 0;
        ack(1);                                 /* 立即 ACK 放行 ESP 发数据 */
        s_st = S_PKT_AA;
        break;

    case S_PKT_AA:
        s_pkt_len = 0;
        s_st = (b == 0xAA) ? S_PKT_SEQH : S_IDLE;
        break;
    case S_PKT_SEQH: s_seq = (uint16_t)b << 8; s_st = S_PKT_SEQL; break;
    case S_PKT_SEQL:
        s_seq |= b;
        if (s_seq != s_exp_seq) { ack(0); s_st = S_PKT_AA; break; }  /* 序号错: NAK 让 ESP 重传同 seq 包 */
        {
            uint32_t remain = s_total - s_recv;
            s_pkt_need = (remain > MT_PKT_MAX) ? MT_PKT_MAX : remain;
        }
        if (s_pkt_need == 0) { ack(0); fail(5); break; }
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
            ack(0); s_st = S_PKT_AA; break;   /* CRC 错: NAK 让 ESP 重传 */
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
            /* 与语言上传同构: 包排入 MusicStore 写 FSM, 由 Poll 落盘后再补 ACK */
            if (MusicStore_WritePacket(s_pkt, s_pkt_len) != 0) { ack(0); fail(4); break; }
            s_wr_pend = 1;   /* 必须置位: 否则 Poll 永不推进 MusicStore FSM → 不 ACK、进度卡 0% */
            s_st = S_IDLE;   /* 写盘期间不解析新字节(FeedByte busy 让出, Poll 完成后回 S_PKT_AA) */
        } else {
            ack(0); s_st = S_PKT_AA;   /* 尾字节错: NAK 让 ESP 重传 */
        }
        break;

    case S_END_AA: s_st = (b == 0xAA) ? S_END_55 : S_IDLE; break;
    case S_END_55: s_st = (b == 0x55) ? S_END_TYPE : (b == 0xAA ? S_END_AA : S_IDLE); break;
    case S_END_TYPE:
        if (b != F_END) { ack(0); fail(6); break; }
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
            ack(0); s_error = 1; s_fail = 6; s_st = S_IDLE; break;
        }
        if (MusicStore_Finish(~s_crc, s_total) != 0) {
            leave_active();
            ack(0); s_error = 1; s_fail = 7; s_st = S_IDLE; break;
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
    int pw;

    /* 写盘 FSM 推进: 本包写完(返回1)才补 ACK 并恢复收流; 失败则终止 */
    if (s_wr_pend) {
        pw = MusicStore_Poll();
        if (pw == 1) {
            s_wr_pend = 0;
            s_recv += s_pkt_len;
            s_exp_seq++;
            g_sys.music_upload_recv = s_recv;
            g_sys.music_upload_pct = (uint8_t)(s_recv * 100U / s_total);
            s_last_act = SystemTime_Millis();   /* 写盘耗时不计入超时 */
            ack(1);
            s_st = (s_recv >= s_total) ? S_END_AA : S_PKT_AA;
        } else if (pw < 0) {
            fail(4);
        }
        return;   /* 写盘期间不检查空闲超时(ESP 等我们 ACK, 不会发包) */
    }

    if (s_active && (uint32_t)(SystemTime_Millis() - s_last_act) > 10000U) {
        MusicStore_AbortUpload();
        leave_active();
        s_error = 1; s_st = S_IDLE; s_wr_pend = 0; s_fail = 1;
        g_sys.music_popup = 5;    /* 下载失败提示 */
    }
}