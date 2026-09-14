#include "bl_esp01s.h"
#include "bl_tft.h"
#include "bsp_esp_uart.h"
#include "bsp_w25q128.h"
#include "flash_ops.h"
#include "board.h"
#include "shared_defs.h"
#include "pin_config.h"
#include "system_time.h"
#include "stm32f10x.h"
#include <string.h>

/*═════════════════════════════════════════════════════════════════════════════
 * 自定义 ESP 固件 OTA 串口协议（1KB/包，包级 ACK）
 *
 * 阶段1 握手： ESP -> STM32: [0xAA 0x55 0x01] + [4字节固件大小 大端]
 *             STM32 -> ESP:   0x06 (ACK)
 * 阶段2 数据包：ESP -> STM32: [0xAA] + [2字节包序号大端] + [≤1024数据]
 *                          + [2字节CRC16] + [0x55]
 *             STM32 -> ESP:   0x06 (ACK) 或 0x15 (NAK，要求重传当前包)
 * 阶段3 结束： ESP -> STM32: [0xAA 0x55 0x02] + [4字节总CRC32大端]
 *             STM32 -> ESP:   0x06 (ACK)
 * 阶段4 完成： ESP -> STM32: 0xDD
 *
 * CRC16 = Modbus 标准（多项式 0x8005/反射 0xA001，初值 0xFFFF），
 *         覆盖 包序号(2B) + 数据(NB)
 * CRC32 = 标准 IEEE 802.3（与 STM32 端 CRC32_Calculate 一致），
 *         覆盖整个固件
 *
 * 由于包内不含长度字段，STM32 依据握手得到的总大小与包序号推算每包数据长度：
 *   包总数 = (总大小 + 1023) / 1024
 *   第 i 包(0基) 数据长度 = (i == 包总数-1) ? (总大小 - i*1024) : 1024
 *═════════════════════════════════════════════════════════════════════════════*/

#define OTA_PKT_MAX     1024U
#define ACK_BYTE        0x06
#define NAK_BYTE        0x15

#define FW_BUF_SIZE     256U
#define AT_TIMEOUT_MS   800
#define AT_READY_MAX    6
#define AT_READY_POLL_MS 300

static const char *ap_ip = "192.168.4.1";

/* 写内部 Flash 缓冲（按 256 字节页写入内部 App 分区） */
static uint8_t  fw_buffer[FW_BUF_SIZE];
static uint16_t fw_buf_idx = 0;

static uint32_t fw_received = 0;
static uint32_t fw_total = 0;
static uint32_t fw_write_addr = APP_ADDR;
static uint32_t fw_crc32 = 0xFFFFFFFFU;   /* 运行中的 CRC32（最终取反） */

static int transfer_state = 0;
static int transfer_error = 0;

/* ── OTA 二进制协议 FSM ── */
typedef enum {
    S_HANDSHAKE_AA = 0,   /* 等待 0xAA */
    S_HANDSHAKE_55,       /* 等待 0x55 */
    S_HANDSHAKE_01,       /* 等待 0x01 */
    S_SIZE,               /* 读取 4 字节总大小 */
    S_PKT_AA,             /* 等待包起始 0xAA */
    S_PKT_SEQ0,           /* 序号高字节 */
    S_PKT_SEQ1,           /* 序号低字节 */
    S_PKT_DATA,           /* 数据区 */
    S_PKT_CRC0,           /* CRC 高字节 */
    S_PKT_CRC1,           /* CRC 低字节 */
    S_PKT_TAIL,           /* 等待 0x55 结束符 */
    S_END_AA,             /* 结束标记 0xAA */
    S_END_55,             /* 结束标记 0x55 */
    S_END_02,             /* 结束标记 0x02 */
    S_END_CRC,            /* 4 字节总 CRC32 */
    S_DONE_DD             /* 等待 0xDD */
} ota_state_t;

static uint8_t  ota_st = S_HANDSHAKE_AA;
static uint8_t  size_buf[4];
static uint8_t  size_idx = 0;
static uint16_t exp_seq = 0;
static uint16_t pkt_seq = 0;
static uint16_t pkt_total = 0;
static uint16_t pkt_crc = 0;
static uint8_t  pkt_buf[OTA_PKT_MAX];
static uint16_t pkt_len = 0;
static uint8_t  end_crc_idx = 0;

static void Delay_ms(uint16_t ms);
static void send_byte(uint8_t b);
static void flush_fw_buffer(void);
static void write_fw_byte(uint8_t b);
static uint16_t ota_crc16(uint16_t seq, const uint8_t *data, uint16_t len);

/* ── 基本 AT 交互（探测 ESP 就绪 / 触发 OTA）── */
static void ESP_Send(const char *s)
{
    EspUart_Write((const uint8_t*)s, strlen(s), 1000);
    uint8_t crlf[] = { '\r', '\n' };
    EspUart_Write(crlf, 2, 1000);
}

static int ESP_WaitResponse(const char *expected, uint32_t timeout_ms)
{
    char buf[64];
    uint16_t bi = 0;
    uint32_t start = SystemTime_Millis();
    uint8_t byte;
    while ((int32_t)(SystemTime_Millis() - start) < (int32_t)timeout_ms) {
        if (EspUart_ReadByte(&byte) != 0) {
            if (bi < sizeof(buf) - 1) buf[bi++] = (char)byte;
            buf[bi] = '\0';
            if (strstr(buf, expected)) return 0;
            if (strstr(buf, "ERROR")) return -1;
        }
        Watchdog_Kick();
    }
    return -1;
}

static int ESP_SendCmd(const char *cmd, const char *expected, uint32_t timeout_ms)
{
    int retry;
    for (retry = 0; retry < 2; retry++) {
        EspUart_ClearRx();
        ESP_Send(cmd);
        if (ESP_WaitResponse(expected, timeout_ms) == 0) return 0;
    }
    return -1;
}

/* 等待自定义 ESP 固件就绪：反复发 AT 直到收到 OK（不再 AT+RST，避免重启抖动） */
static int ESP_WaitReady(void)
{
    int attempts;
    for (attempts = 0; attempts < AT_READY_MAX; attempts++) {
        Delay_ms(AT_READY_POLL_MS);
        EspUart_ClearRx();
        if (ESP_SendCmd("AT", "OK", AT_TIMEOUT_MS) == 0) return 0;
    }
    return -1;
}

void BL_ESP01S_Init(void)
{
    EspUart_Init();
}

/* 触发 OTA：让 ESP 开 AP + 网页，回复 OK 后进入 UART 接收状态 */
int BL_ESP01S_StartOta(void)
{
    if (ESP_WaitReady() != 0) return -1;
    if (ESP_SendCmd("AT+OTAAP", "OK", 1500) != 0) return -1;
    return 0;
}

/* 关闭网页并让 ESP 进入低功耗休眠：固件已收完，由 STM32 主动发 AT+OTACLOSE，
 * 而不是让 ESP 自己决定何时休眠。失败不致命——拷贝阶段会硬断电 ESP。 */
int BL_ESP01S_CloseWeb(void)
{
    return ESP_SendCmd("AT+OTACLOSE", "OK", 1500);
}

const char* BL_ESP01S_GetIP(void)
{
    return ap_ip;
}

void BL_ESP01S_ResetTransfer(void)
{
    fw_received = 0;
    fw_total = 0;
    fw_write_addr = APP_ADDR;
    fw_crc32 = 0xFFFFFFFFU;
    fw_buf_idx = 0;
    transfer_state = 0;
    transfer_error = 0;

    ota_st = S_HANDSHAKE_AA;
    size_idx = 0;
    exp_seq = 0;
    pkt_len = 0;
    end_crc_idx = 0;
}

int BL_ESP01S_GetTransferState(void)
{
    if (transfer_error) return -1;
    if (transfer_state == 1) return 1;
    if (transfer_state == 2) return 2;
    return 0;
}

uint32_t BL_ESP01S_GetReceivedSize(void) { return fw_received; }
uint32_t BL_ESP01S_GetTotalSize(void)     { return fw_total; }
uint32_t BL_ESP01S_GetTotalFirmwareSize(void) { return fw_total; }
uint32_t BL_ESP01S_GetFirmwareCrc32(void) { return ~fw_crc32; }

static void send_byte(uint8_t b)
{
    EspUart_Write(&b, 1, 1000);
}

/* OTA 数据包校验通过后，直接写入内部 App 分区（已先整区擦除）。 */
static void flush_fw_buffer(void)
{
    if (fw_buf_idx > 0) {
        /* 防越界：写入不得超出 App 分区 */
        if ((uint32_t)(fw_write_addr - APP_ADDR) + fw_buf_idx > APP_SIZE) {
            transfer_error = 1;
            fw_buf_idx = 0;
            BL_TFT_ShowStatus("SIZE ERR");
            return;
        }
        /* 内部 Flash 已由握手后的 erase_app_for_ota() 整区擦除保证可编程；写失败即终止传输 */
        if (Flash_Write(fw_write_addr, fw_buffer, fw_buf_idx) != 0) {
            transfer_error = 1;
            fw_buf_idx = 0;
            BL_TFT_ShowStatus("WERR");
            return;
        }
        fw_write_addr += fw_buf_idx;
        fw_buf_idx = 0;
    }
}

static void write_fw_byte(uint8_t byte)
{
    fw_buffer[fw_buf_idx++] = byte;
    if (fw_buf_idx >= FW_BUF_SIZE) {
        flush_fw_buffer();
    }
}

/* Modbus CRC16：覆盖 包序号(2B) + 数据(NB) */
static uint16_t ota_crc16(uint16_t seq, const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint8_t  sb[2] = { (uint8_t)(seq >> 8), (uint8_t)(seq & 0xFF) };
    for (int i = 0; i < 2; i++) {
        crc ^= sb[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

/* 收到固件握手（上传即将开始）后整区擦除内部 App 分区。
 * 误入 BL 模式且未上传固件时不会擦除，保留可运行 App。 */
static int erase_app_for_ota(void)
{
    for (uint32_t addr = APP_ADDR; addr < APP_ADDR + APP_SIZE; addr += FLASH_PAGE_SIZE) {
        Watchdog_Kick();
        if (Flash_ErasePage(addr) != 0) return -1;
    }
    return 0;
}

/* 处理一个 OTA 字节（FSM 驱动） */
static void feed_ota_byte(uint8_t b)
{
    switch (ota_st) {
    case S_HANDSHAKE_AA:
        if (b == 0xAA) ota_st = S_HANDSHAKE_55;
        break;
    case S_HANDSHAKE_55:
        ota_st = (b == 0x55) ? S_HANDSHAKE_01 : S_HANDSHAKE_AA;
        break;
    case S_HANDSHAKE_01:
        if (b == 0x01) { size_idx = 0; ota_st = S_SIZE; }
        else ota_st = S_HANDSHAKE_AA;
        break;
    case S_SIZE:
        size_buf[size_idx++] = b;
        if (size_idx == 4) {
            fw_total = ((uint32_t)size_buf[0] << 24) | ((uint32_t)size_buf[1] << 16) |
                       ((uint32_t)size_buf[2] << 8)  | (uint32_t)size_buf[3];
            if (fw_total == 0 || fw_total > APP_SIZE) {
                transfer_error = 1;   /* 大小非法：直接拒绝，防止越界写内部 Flash */
                break;
            }
            /* 先整区擦除，再回握手 ACK。ESP 侧握手等待已放宽到 10s，
             * 擦除期间 ESP 不发数据包 → 不会发生 USART 溢出/残包。
             * 必须擦完再 ACK，否则 ESP 提前发包会撞上擦除丢字节。 */
            BL_TFT_ShowStatus("ERASE...");
            if (erase_app_for_ota() != 0) {
                transfer_error = 1;
                BL_TFT_ShowStatus("ERASE FAIL");
                break;
            }
            BL_TFT_ShowStatus("ERASE OK");
            /* 复位写入上下文 */
            fw_received = 0;
            fw_write_addr = APP_ADDR;
            fw_crc32 = 0xFFFFFFFFU;
            exp_seq = 0;
            pkt_len = 0;
            transfer_state = 1;
            send_byte(ACK_BYTE);          /* 阶段1 ACK：擦除完成，等 ESP 发数据包 */
            ota_st = S_PKT_AA;
        }
        break;

    case S_PKT_AA:
        /* 容错：噪声/溢出残留字节直接忽略，等 ESP 重传的 0xAA 起包。 */
        if (b == 0xAA) ota_st = S_PKT_SEQ0;
        break;
    case S_PKT_SEQ0:
        pkt_seq = (uint16_t)b << 8;
        ota_st = S_PKT_SEQ1;
        break;
    case S_PKT_SEQ1:
        pkt_seq |= b;
        {
            uint32_t pc = (fw_total + (OTA_PKT_MAX - 1U)) / OTA_PKT_MAX;
            pkt_total = (exp_seq == (uint16_t)(pc - 1U)) ?
                        (uint16_t)(fw_total - (uint32_t)exp_seq * OTA_PKT_MAX) :
                        (uint16_t)OTA_PKT_MAX;
        }
        pkt_len = 0;
        ota_st = S_PKT_DATA;
        break;
    case S_PKT_DATA:
        pkt_buf[pkt_len++] = b;
        if (pkt_len >= pkt_total) ota_st = S_PKT_CRC0;
        break;
    case S_PKT_CRC0:
        pkt_crc = (uint16_t)b << 8;
        ota_st = S_PKT_CRC1;
        break;
    case S_PKT_CRC1:
        pkt_crc |= b;
        ota_st = S_PKT_TAIL;
        break;
    case S_PKT_TAIL:
        if (b == 0x55) {
            uint16_t calc = ota_crc16(pkt_seq, pkt_buf, pkt_len);
            if (calc == pkt_crc && pkt_seq == exp_seq) {
                for (uint16_t i = 0; i < pkt_len; i++) {
                    write_fw_byte(pkt_buf[i]);
                    fw_crc32 ^= pkt_buf[i];
                    for (uint8_t j = 0; j < 8; j++)
                        fw_crc32 = (fw_crc32 >> 1) ^ (0xEDB88320U & -(fw_crc32 & 1));
                }
                fw_received += pkt_len;
                send_byte(ACK_BYTE);
                exp_seq++;
                uint32_t pc = (fw_total + (OTA_PKT_MAX - 1U)) / OTA_PKT_MAX;
                if (exp_seq >= (uint16_t)pc) {
                    flush_fw_buffer();     /* 刷写最后一包可能不足一页的尾巴 */
                    ota_st = S_END_AA;
                } else {
                    ota_st = S_PKT_AA;
                }
            } else {
                send_byte(NAK_BYTE);       /* CRC 错误或序号错：要求重传当前包 */
                ota_st = S_PKT_AA;
            }
        } else {
            /* 尾字节非 0x55：多半是重传期间的残串/噪声，丢弃并等下一个 0xAA 起包，
             * 不要直接置 transfer_error 报 Upload Error。 */
            ota_st = S_PKT_AA;
        }
        break;

    case S_END_AA:
        ota_st = (b == 0xAA) ? S_END_55 : S_HANDSHAKE_AA;
        break;
    case S_END_55:
        ota_st = (b == 0x55) ? S_END_02 : S_HANDSHAKE_AA;
        break;
    case S_END_02:
        if (b == 0x02) { end_crc_idx = 0; ota_st = S_END_CRC; }
        else ota_st = S_HANDSHAKE_AA;
        break;
    case S_END_CRC:
        end_crc_idx++;
        if (end_crc_idx == 4) {
            /* 总 CRC32 由 ESP 在阶段3 发出；按项目约定仅校验向量表、不校验 CRC，
             * 这里收到 4 字节后直接回 ACK。 */
            send_byte(ACK_BYTE);
            ota_st = S_DONE_DD;
        }
        break;
    case S_DONE_DD:
        if (b == 0xDD) {
            transfer_state = 2;            /* 阶段4 完成 */
        }
        break;
    }
}

void BL_ESP01S_Process(void)
{
    uint8_t byte;
    while (EspUart_ReadByte(&byte) != 0) {
        feed_ota_byte(byte);
        if (transfer_error) break;
    }
}

/* 强制结束上传（超时兜底）：flush 并标记完成 */
void BL_ESP01S_FinishTransfer(void)
{
    flush_fw_buffer();
    transfer_state = 2;
    BL_TFT_ShowProgressBar(100);
}

/* 中止上传（超时无新数据等异常）：标记错误，不进入引导 */
void BL_ESP01S_AbortTransfer(void)
{
    transfer_error = 1;
}

static void Delay_ms(uint16_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 7200; i++) {
        if ((i & 0x1FFFFU) == 0U) Watchdog_Kick();
    }
}
