/* lang_ota.c - 语言字库 App 侧接收（擦除/页写全部在 LangOta_Poll 里跑 FSM，
 * FeedByte 仅解析 0xAA 帧。避免擦除阻塞 EspLink 字节循环导致 UART 溢出丢包。
 * 帧协议与 music_ota 同构: 握手 0xAA 0x55 0x13 [sizeBE4]，数据 0xAA [seq] ... 0x55，
 * 结束 0xAA 0x55 0x14 [crc32BE4]。AB 双区：目标区取当前有效区的另一页。 */
#include "lang_ota.h"
#include "bsp_font_store.h"
#include "sfud_flash.h"
#include "system_time.h"
#include "system_config.h"
#include "bsp_esp_uart.h"
#include <string.h>

#define MT_PKT_MAX   1024U
#define F_HS         0x13
#define F_END        0x14
#define HDR_FLAG_OFF 26U   /* 字库头部有效标志区(4B), 写入时保持擦除态, 结束帧再置有效 */

enum {
    S_IDLE = 0,
    S_SY0, S_SY1, S_TYPE,
    S_SIZE1, S_SIZE2, S_SIZE3, S_SIZE4,
    S_PKT_AA, S_PKT_SEQH, S_PKT_SEQL, S_PKT_DATA, S_PKT_CRCH, S_PKT_CRCL, S_PKT_55,
    S_ER_KICK, S_ER_WAIT,          /* 擦除下一扇区 */
    S_PG_KICK, S_PG_WAIT,          /* 页写(256B/次) */
    S_END_AA, S_END_55, S_END_TYPE, S_END_CRC1, S_END_CRC2, S_END_CRC3, S_END_CRC4
};

static uint8_t  s_st = S_IDLE;
static uint32_t s_total = 0, s_recv = 0;
static uint16_t s_seq = 0, s_exp_seq = 0;
static uint32_t s_crc = 0xFFFFFFFFUL;
static uint8_t  s_pkt[MT_PKT_MAX];
static uint32_t s_pkt_len = 0, s_pkt_need = 0;
static uint8_t  s_active = 0;
static uint32_t s_last_act = 0;
static uint8_t  s_crchi_tmp = 0;
static uint32_t s_crc_expect = 0;
static uint32_t s_base = 0;

/* Poll 写/擦除 FSM */
static uint8_t  s_hold = 0;        /* SysFlashOp 被本状态持有 */
static uint32_t s_wr_off = 0;      /* 当前包写入起点(bin 偏移) */
static uint32_t s_wr_end = 0;      /* 当前包头(不含) */
static uint32_t s_pgd_off = 0;     /* 当前页写起点(bin 偏移) */
static uint32_t s_pgn_off = 0;     /* 下一次要擦的扇区 idx */
static uint32_t s_pg_len = 0;      /* 当前页写字节数(暂存 1..256) */
static uint32_t s_need_sec = 0;    /* 本包需要的最后扇区 */
static uint32_t s_max_sec = 0xFFFFFFFFUL;   /* 已擦到最高扇区 */
static uint8_t  s_dbg_hand = 0;
static uint16_t s_dbg_nak = 0;
static uint16_t s_dbg_ack = 0;  /* 已擦到最高扇区 */
static uint8_t  s_end_stat = 0; /* 结束帧结果 */

static uint16_t crc16_modbus(uint16_t crc, uint8_t b)
{
    crc ^= b;
    for (int i = 0; i < 8; i++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    return crc;
}

static void ack(uint8_t ok)
{
    uint8_t c = ok ? 0x06 : 0x15;
    if (ok) s_dbg_ack++; else s_dbg_nak++;
    EspUart_Write(&c, 1, 100);
}

void  LangOta_Init(void)  { s_st = S_IDLE; s_active = 0; }
void  LangOta_Abort(void) { if (s_hold) { SysFlashOp_Release(); s_hold = 0; } s_st = S_IDLE; s_active = 0; }
uint8_t LangOta_Active(void) { return s_active; }

static void lang_fail(void)
{
    if (s_hold) { SysFlashOp_Release(); s_hold = 0; }
    s_st = S_IDLE;
    s_active = 0;
}

/* 包完整校验通过后，安排擦除/写页；由 Poll 完成后再 ack */
static void start_flash(void)
{
    s_wr_off = s_recv;
    s_wr_end = s_recv + s_pkt_len;
    s_need_sec = (s_wr_end - 1U) >> 12;
    s_pgn_off = s_max_sec + 1U;
    s_pgd_off = s_wr_off;
    if (s_pgn_off <= s_need_sec) s_st = S_ER_KICK;
    else s_st = S_PG_KICK;
}

void LangOta_FeedByte(uint8_t b)
{
    s_last_act = SystemTime_Millis();

    /* 擦除/页写运行中: 只更新时间, 不动作(不破坏 FSM) */
    if (s_st == S_ER_KICK || s_st == S_ER_WAIT ||
        s_st == S_PG_KICK || s_st == S_PG_WAIT) return;

    switch (s_st) {
    case S_IDLE:        s_st = (b == 0xAA) ? S_SY0 : S_IDLE; break;
    case S_SY0:         s_st = (b == 0x55) ? S_TYPE : (b == 0xAA ? S_SY0 : S_IDLE); break;
    case S_TYPE:
        if (b == F_HS) s_st = S_SIZE1;
        else s_st = (b == 0xAA) ? S_SY0 : S_IDLE;
        break;
    case S_SIZE1: s_total = (uint32_t)b << 24; s_st = S_SIZE2; break;
    case S_SIZE2: s_total |= (uint32_t)b << 16; s_st = S_SIZE3; break;
    case S_SIZE3: s_total |= (uint32_t)b << 8;  s_st = S_SIZE4; break;
    case S_SIZE4:
        s_total |= (uint32_t)b;
        if (s_total == 0 || s_total > LANG_REGION_SIZE) { ack(0); lang_fail(); break; }
        s_base = LangTargetBase();
        s_recv = 0; s_exp_seq = 0; s_crc = 0xFFFFFFFFUL;
        s_pkt_len = 0; s_pkt_need = 0;
        s_max_sec = 0xFFFFFFFFUL;
        s_active = 1; s_dbg_hand = 1; s_end_stat = 0;
        EspUart_ResetOverflow();  /* 新会话起点: 清粘滞旧溢出标志, 防刚握手即被 overflow 中止误杀 */
        s_max_sec = 0xFFFFFFFFUL;
        /* 握手立即 ACK: ESP 不再空等, 浏览器进度立刻走;
         * 擦除放到每个数据包抵达时由 Poll 异步懒擦(4KB/次 ~75ms), 不占用 ESP 缓冲 */
        ack(1);
        s_st = S_PKT_AA;
        break;
    case S_PKT_AA:   s_pkt_len = 0; s_st = (b == 0xAA) ? S_PKT_SEQH : S_PKT_AA; break;
    case S_PKT_SEQH: s_seq = (uint16_t)b << 8; s_st = S_PKT_SEQL; break;
    case S_PKT_SEQL:
        s_seq |= b;
        if (s_seq != s_exp_seq) { ack(0); s_st = S_PKT_AA; break; }  /* 让 ESP 重传同 seq 包 */
        { uint32_t remain = s_total - s_recv;
          s_pkt_need = (remain > MT_PKT_MAX) ? MT_PKT_MAX : remain; }
        if (s_pkt_need == 0) { ack(0); lang_fail(); break; }
        s_st = S_PKT_DATA;
        break;
    case S_PKT_DATA:
        s_pkt[s_pkt_len++] = b;
        if (s_pkt_len >= s_pkt_need) s_st = S_PKT_CRCH;
        break;
    case S_PKT_CRCH: s_crchi_tmp = b; s_st = S_PKT_CRCL; break;
    case S_PKT_CRCL:
    {
        uint16_t crc = 0xFFFF;
        uint8_t crclo = b;
        crc = crc16_modbus(crc, (uint8_t)(s_seq >> 8));
        crc = crc16_modbus(crc, (uint8_t)(s_seq & 0xFF));
        { uint32_t i; for (i = 0; i < s_pkt_len; i++) crc = crc16_modbus(crc, s_pkt[i]); }
        if ((uint8_t)(crc >> 8) != s_crchi_tmp || (uint8_t)(crc & 0xFF) != crclo) {
            ack(0); s_st = S_PKT_AA; break;   /* 校验失败让 ESP 重传 */
        }
        s_st = S_PKT_55;
        break;
    }
    case S_PKT_55:
        if (b == 0x55) {
            uint32_t i; int j;
            for (i = 0; i < s_pkt_len; i++) {
                s_crc ^= s_pkt[i];
                for (j = 0; j < 8; j++) s_crc = (s_crc & 1) ? (s_crc >> 1) ^ 0xEDB88320UL : (s_crc >> 1);
            }
            start_flash();       /* 交给 Poll FSM 擦除/写入, 完成再 ack */
        } else { ack(0); s_st = S_PKT_AA; }
        break;
    case S_END_AA: s_st = (b == 0xAA) ? S_END_55 : S_END_AA; break;
    case S_END_55: s_st = (b == 0x55) ? S_END_TYPE : S_END_AA; break;
    case S_END_TYPE:
        if (b != F_END) { ack(0); s_st = S_END_AA; break; }  /* 容忍杂字节, 等重发 */
        s_crc_expect = 0; s_st = S_END_CRC1;
        break;
    case S_END_CRC1: s_crc_expect = (uint32_t)b << 24; s_st = S_END_CRC2; break;
    case S_END_CRC2: s_crc_expect |= (uint32_t)b << 16; s_st = S_END_CRC3; break;
    case S_END_CRC3: s_crc_expect |= (uint32_t)b << 8;  s_st = S_END_CRC4; break;
    case S_END_CRC4:
        s_crc_expect |= (uint32_t)b;
    if (s_recv != s_total) { s_end_stat = 4; ack(0); s_st = S_END_AA; break; }  /* 数据不完整不提交 */
    if (~s_crc != s_crc_expect) {
        /* 总 CRC32 不一致 → 拒绝提交(不再写 VALID): 损坏文件不得被当成成功字库。
         * 逐包 CRC16 只保证传输完整, 不保证文件本身正确; 此前仅记录 s_end_stat=2
         * 仍继续 LangMarkValid, 损坏字库会被 UI 当成功启用(P1 修复)。 */
        s_end_stat = 2;
        ack(0);
        lang_fail();
        break;
    }
    if (LangMarkValid(s_base) != 0) { s_end_stat = 3; ack(0); s_st = S_END_AA; break; }  /* flag 写失败重试 */
        if (s_end_stat == 0) s_end_stat = 1;
        g_sys.lang_download_done = 1;
        g_sys.lang_ap_active = 0;
        g_sys.lang_upload_pct = 100;
        g_sys.ui_force_redraw = 1;
        ack(1);
        s_st = S_IDLE;
        s_active = 0;
        break;
    default: break;
    }
}

uint8_t  LangOta_GotHand(void) { return s_dbg_hand; }
uint16_t LangOta_GetNak(void)  { return s_dbg_nak; }
uint16_t LangOta_GetAck(void)  { return s_dbg_ack; }
uint32_t LangOta_GetRecv(void) { return s_recv; }
uint8_t  LangOta_EndStat(void) { return s_end_stat; }

uint8_t LangOta_GetPct(void)
{
    if (!s_active || s_total == 0) return g_sys.lang_upload_pct;
    return (uint8_t)(s_recv * 100U / s_total);
}

void LangOta_Poll(void)
{
    uint8_t sr1;
    uint32_t n;

    /* #2: 收包状态(S_PKT_AA..S_PKT_55)等 ESP 下一包时若链路断开, 20s 超时统一终止。
     * 原来只有 S_END 组带超时, 收包态没有 → s_active 永久卡 1:
     * EspLink 的 "上传中丢弃其它字节" 门会连带冻结整条 ESP 链路。
     * 擦/写状态(S_ER 与 S_PG 组)不查——那是本地 SPI 忙, 非等待对端。 */
    if (s_active && s_st >= S_PKT_AA && s_st <= S_PKT_55 &&
        (uint32_t)(SystemTime_Millis() - s_last_act) > 20000U) {
        lang_fail();
        return;
    }

    switch (s_st) {
    case S_ER_KICK:
        if (!s_hold) {
            if (!SysFlashOp_TryBegin()) return;
            s_hold = 1;
        }
        {
            int r = SfudFlash_StartEraseSector(s_base + (s_pgn_off << 12));
            if (r == 1) { /* busy */ return; }
            if (r != 0) { SysFlashOp_Release(); s_hold = 0; ack(0); lang_fail(); return; }
            s_st = S_ER_WAIT;
        }
        return;
    case S_ER_WAIT:
        if (SfudFlash_ReadSR1(&sr1) != 0) return;
        if (sr1 & 0x01U) return;
        SysFlashOp_Release(); s_hold = 0;
        s_max_sec = s_pgn_off;
        s_pgn_off++;
        if (s_pgn_off <= s_need_sec) s_st = S_ER_KICK;
        else s_st = S_PG_KICK;
        return;
    case S_PG_KICK:
        if (s_pgd_off >= s_wr_end) {   /* 写完成 */
            s_recv = s_wr_end;
            s_exp_seq++;
            g_sys.lang_upload_pct = (uint8_t)(s_recv * 100U / (s_total ? s_total : 1U));
            ack(1);
            if (s_active)
                s_st = (s_recv >= s_total) ? S_END_AA : S_PKT_AA;
            return;
        }
        if (!s_hold) {
            if (!SysFlashOp_TryBegin()) return;
            s_hold = 1;
        }
        n = s_wr_end - s_pgd_off;
        if (n > 256U) n = 256U;
        {
            const uint8_t *w = s_pkt + (s_pgd_off - s_wr_off);
            uint8_t tmp[256];
            if (s_pgd_off < HDR_FLAG_OFF + 4U && s_pgd_off + n > HDR_FLAG_OFF) {
                uint16_t k;
                memcpy(tmp, w, n);
                for (k = 0; k < n; k++) {
                    uint32_t abs = s_pgd_off + k;
                    if (abs >= HDR_FLAG_OFF && abs < HDR_FLAG_OFF + 4U) tmp[k] = 0xFFU;
                }
                w = tmp;
            }
            int r = SfudFlash_StartWritePage(s_base + s_pgd_off, w, (uint16_t)n);
            if (r == 1) return;                     /* busy */
            if (r != 0) { SysFlashOp_Release(); s_hold = 0; ack(0); lang_fail(); return; }
        }
        s_pg_len = n;
        s_st = S_PG_WAIT;
        return;
    case S_PG_WAIT:
        if (SfudFlash_ReadSR1(&sr1) != 0) return;
        if (sr1 & 0x01U) return;
        SysFlashOp_Release(); s_hold = 0;
        s_pgd_off += s_pg_len;
        s_st = S_PG_KICK;
        return;
    case S_END_AA:
    case S_END_55:
    case S_END_TYPE:
    case S_END_CRC1:
    case S_END_CRC2:
    case S_END_CRC3:
    case S_END_CRC4:
        if (s_active && (uint32_t)(SystemTime_Millis() - s_last_act) > 20000U) lang_fail();
        return;
    case S_IDLE:
        if (s_active && (uint32_t)(SystemTime_Millis() - s_last_act) > 20000U) lang_fail();
        return;
    default:
        return;
    }
}