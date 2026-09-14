#include "bl_main.h"
#include "shared_defs.h"
#include "flash_ops.h"
#include "upgrade_flag.h"
#include "bl_tft.h"
#include "bl_esp01s.h"
#include "bsp_w25q128.h"
#include "bsp_esp_uart.h"
#include "board.h"
#include "pin_config.h"
#include "system_time.h"
#include "stm32f10x.h"
#include <string.h>

static void Delay_ms(uint16_t ms);

/* 上电兜底：长按编码器按键（PB5，按下为低电平）直接进入下载模式，
 * 无论 App 是否有效都生效，避免 OTA 写入坏固件后“变砖”无法再进升级界面。 */
static int EncoderButton_HeldAtBoot(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin  = PIN_ENC_BTN_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;   /* 内部上拉，按下为低电平 */
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(PIN_ENC_BTN_PORT, &gpio);

    /* 延时 20ms 让电源和 GPIO 内部上下拉稳定 */
    Delay_ms(20);

    /* 连续采样 8 次，间隔 1ms，全部低电平才认为真按住 */
    uint8_t held = 1;
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (GPIO_ReadInputDataBit(PIN_ENC_BTN_PORT, PIN_ENC_BTN_PIN) != 0) {
            held = 0;
            break;
        }
        Delay_ms(1);
    }
    return held;
}

#ifdef BOOTLOADER_BUILD
int main(void)
{
    BootloaderV2_Run();
    for (;;) { Watchdog_Kick(); }
}
#endif

#define MAX_RETRY                    3
#define ERASE_PROGRESS_PAGES        16
#define FLASH_CHUNK                 256U

static uint32_t fw_total = 0;
static uint32_t fw_crc = 0;

/* 校验已直写到内部 App 分区的固件：
 * 1. 重算内部 Flash [0, fw_total) 的 CRC32，与线上累积值 fw_crc 一致；
 * 2. 校验 APP_ADDR 处向量表（SP=0x2000xxxx，PC=0x0800xxxx 且在 App 区内）。 */
static int verify_app_downloaded(void)
{
    uint8_t buf[FLASH_CHUNK];
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t rd = 0U;
    uint32_t rem = fw_total;

    if (fw_total == 0 || fw_total > APP_SIZE) return -1;

    while (rem > 0U) {
        uint32_t rl = (rem > sizeof(buf)) ? sizeof(buf) : rem;
        Watchdog_Kick();
        if (Flash_Read(APP_ADDR + rd, buf, rl) != 0) return -1;
        for (uint32_t i = 0; i < rl; i++) {
            crc ^= buf[i];
            for (uint8_t j = 0; j < 8; j++)
                crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1));
        }
        rd += rl;
        rem -= rl;
    }
    if ((uint32_t)~crc != fw_crc) return -1;

    {
        uint8_t vt[8];
        Flash_Read(APP_ADDR, vt, sizeof(vt));
        uint32_t sp = (uint32_t)vt[0] | ((uint32_t)vt[1] << 8) |
                      ((uint32_t)vt[2] << 16) | ((uint32_t)vt[3] << 24);
        uint32_t pc = (uint32_t)vt[4] | ((uint32_t)vt[5] << 8) |
                      ((uint32_t)vt[6] << 16) | ((uint32_t)vt[7] << 24);
        if ((sp & 0x2FFE0000) != 0x20000000) return -1;
        if ((pc & 0xFF000000) != 0x08000000) return -1;
        if (pc < APP_ADDR || pc >= APP_ADDR + APP_SIZE) return -1;
    }
    return 0;
}

void BootloaderV2_Run(void)
{
    SystemCoreClockUpdate();

    SystemTime_Init();
    Watchdog_Init();   /* 约 4 秒窗口 */

    UpgradeFlag_t flag;
    int have_flag = (UpgradeFlag_Read(&flag) == 0);

    /* 强制进入升级模式：App 内上电长按编码器已写 FORCE_BOOT 标志，
     * 复位后直接开 AP 收固件（覆盖所有其它判定）。 */
    if (have_flag && flag.status == UPGRADE_STATUS_FORCE_BOOT) {
        UpgradeFlag_Clear();
        BootloaderV2_EnterUpgradeMode();
    }

    /* 板级初始化（GPIO、JTAG 等），放在 App 验证之后避免影响 Flash 读取 */
    Board_EarlyInit();

    /* 兜底：上电长按编码器（PB5）强制进入下载模式，坏固件也不会变砖。 */
    if (EncoderButton_HeldAtBoot()) {
        BootloaderV2_EnterUpgradeMode();
    }

    /* 验证 App 有效性（重试 5 次，避免上电时钟不稳误判） */
    {
        int app_valid = 0;
        int retry;
        for (retry = 0; retry < 5; retry++) {
            if (BootloaderV2_VerifyApp() == 0) { app_valid = 1; break; }
            Delay_ms(10);
        }
        if (!app_valid) {
            /* App 无效：自动进入升级模式（内部会先初始化 SPI1 再点亮屏幕，
             * 否则黑屏无法显示升级界面）。升级流程不返回或复位。 */
            BootloaderV2_EnterUpgradeMode();
        }
        if (app_valid) {
            BootloaderV2_JumpToApp();
        }
    }
    BootloaderV2_ShowError();
    for (;;) {
        Watchdog_Kick();
    }
}

void BootloaderV2_EnterUpgradeMode(void)
{
    int retry;

    /* ── 先初始化屏幕（SPI1 + TFT），再操作 ESP ─────────────
     * ESP_WaitReady 是阻塞握手，若 ESP 异常会卡住；屏幕必须在此之前就绪，
     * 否则永远黑屏、无法显示升级界面。 */
    W25Q128_SetServiceCallback(Watchdog_Kick);
    W25Q128_Init();
    BL_TFT_Init();
    BL_TFT_ShowUpgradeScreen();

    /* 初始化 ESP 串口并上电（P-MOS: 低=开）。OTA 触发放在重试循环内，
     * 失败后重试，避免 ESP 启动慢导致一次就失败。 */
    BL_ESP01S_Init();
    EspUart_SetEnabled(1);   /* P-MOS: 低=开，确保 ESP01S 供电 */

    for (retry = 0; retry < MAX_RETRY; retry++) {
        fw_total = 0;
        fw_crc = 0;

        /* 注意：不在进入升级模式时擦除 App 分区。擦除推迟到
         * ESP 握手收到固件大小（上传真正开始）时才执行，
         * 避免误入 BL 模式就清除可运行 App。 */

        BL_ESP01S_ResetTransfer();
        BL_TFT_ShowStatus("WAIT");

        /* 通知 ESP 进入 OTA 模式（自定义固件开 AP + 网页，并回 OK），
         * 之后 BL_ESP01S_Process 按二进制 OTA 协议接收固件：
         * 握手 -> 1KB/包(带 CRC16 包级 ACK) -> 总 CRC32 -> 完成。 */
        if (BL_ESP01S_StartOta() != 0) {
            BL_TFT_ShowError("ESP No AT");
            Delay_ms(2000);
            continue;
        }
        {
            const char *ip = BL_ESP01S_GetIP();
            BL_TFT_ShowAPInfo("QiMingXing", "12345678", ip);
        }
        BL_TFT_ShowStatus("DOWNLOAD TO FLASH");

        uint32_t last_watchdog = SystemTime_Millis();
        uint32_t last_progress_tick = 0;
        uint32_t last_rx_time = 0;
        uint32_t last_recv = 0;
        uint32_t state1_since = 0;   /* 握手完成后开始计时首包 */
        uint8_t last_progress_step = 0;
        int transfer_done = 0;

        /* 上传循环：显示接收进度条（每 10% 刷新一次） */
        while (!transfer_done) {
            uint32_t now = SystemTime_Millis();
            if ((int32_t)(now - last_watchdog) >= 100) {
                Watchdog_Kick();
                last_watchdog = now;
            }

            BL_ESP01S_Process();

            /* 跟踪最后收到数据的时间（用于超时完成） */
            uint32_t recv_now = BL_ESP01S_GetReceivedSize();
            if (recv_now != last_recv) {
                last_recv = recv_now;
                last_rx_time = now;
            }

            int esp_state = BL_ESP01S_GetTransferState();
            if (esp_state == 1) {
                if (state1_since == 0) state1_since = now;   /* 握手完成时刻 */
                fw_total = BL_ESP01S_GetTotalSize();
                uint32_t recv = BL_ESP01S_GetReceivedSize();
                uint32_t total = BL_ESP01S_GetTotalFirmwareSize();   /* X-Total-Size 总字节数 */
                if (total > 0) {
                    uint8_t progress = (uint8_t)(recv * 100U / total);
                    /* 进度条差分局部刷新：每次 1% 增量只有几像素，人眼不可见；
                     * 状态文字保持 "DOWNLOAD TO FLASH" 不动，避免文字重绘闪烁。 */
                    if (progress != last_progress_step &&
                        (int32_t)(now - last_progress_tick) > 40) {
                        last_progress_step = progress;
                        last_progress_tick = now;
                        BL_TFT_ShowProgressBar(progress);
                    }
                }
                /* 握手后 20s 内一个完整包都没收到（首包重传也失败）：中止，不再死等 */
                if (last_recv == 0 && state1_since != 0 && (int32_t)(now - state1_since) > 20000) {
                    BL_ESP01S_AbortTransfer();
                    transfer_done = 1;
                    break;
                }
                /* 超时无新数据：如果数据已收全，直接完成（不需等浏览器 /done）；
                 * 否则中止重试。 */
                if (last_recv > 0 && (int32_t)(now - last_rx_time) > 10000) {
                    uint32_t exp = BL_ESP01S_GetTotalFirmwareSize();
                    if (exp > 0 && last_recv >= exp) {
                        BL_ESP01S_FinishTransfer();
                        transfer_done = 1;
                        break;
                    }
                    BL_ESP01S_AbortTransfer();
                    transfer_done = 1;
                    break;
                }
            } else if (esp_state == 2) {
                transfer_done = 1;
            } else if (esp_state < 0) {
                BL_TFT_ShowError("Upload Error!");
                Delay_ms(2000);
                break;
            }
        }

        if (!transfer_done) continue;
        if (BL_ESP01S_GetTransferState() < 0) {
            /* 超时/异常中止：残缺固件不得进入引导，直接重试 */
            BL_TFT_ShowError("Upload Error!");
            Delay_ms(2000);
            continue;
        }

        /* 上传完成：从 bl_esp01s 重新读取真实固件大小 */
        fw_total = BL_ESP01S_GetTotalSize();
        fw_crc = BL_ESP01S_GetFirmwareCrc32();

        BL_TFT_ShowStatus("DOWNLOAD OK");
        Watchdog_Kick();

        if (verify_app_downloaded() == 0) {
            /* 固件已直写入内部 App 分区且校验通过：关闭网页，复位重启，
             * 重启后 Bootloader 验证 App 有效即跳转。不再走外部 Flash 拷贝流程。 */
            BL_TFT_ShowUpgradeScreen();
            BL_TFT_ShowProgressBar(100);
            BL_TFT_ShowStatus("Download OK");
            Watchdog_Kick();

            BL_ESP01S_CloseWeb();
            BL_TFT_ShowStatus("Restarting...");
            Delay_ms(2000);
            NVIC_SystemReset();
            return;
        } else {
            BL_TFT_ShowError("Verify Failed!");
            Delay_ms(2500);
            if (retry < MAX_RETRY - 1) {
                BL_TFT_ShowStatus("Retrying...");
                Delay_ms(1500);
            }
        }
    }

    BL_TFT_ShowError("Update Failed!");
    for (;;) {
        Watchdog_Kick();
    }
}

int BootloaderV2_WriteFirmware(uint32_t offset, uint8_t *data, uint16_t len)
{
    if (offset + len > APP_SIZE) return -1;
    return Flash_Write(APP_ADDR + offset, data, len);
}

int BootloaderV2_VerifyFirmware(void)
{
    /* 用户要求不校验大小/CRC，只验证向量表有效即可引导 */
    uint32_t sp = *(__IO uint32_t*)APP_ADDR;
    if ((sp & 0x2FFE0000) != 0x20000000) return -1;

    uint32_t pc = *(__IO uint32_t*)(APP_ADDR + 4);
    if ((pc & 0xFF000000) != 0x08000000) return -1;

    return 0;
}

int BootloaderV2_VerifyApp(void)
{
    uint32_t sp = *(__IO uint32_t*)APP_ADDR;
    if ((sp & 0x2FFE0000) != 0x20000000) return -1;

    uint32_t pc = *(__IO uint32_t*)(APP_ADDR + 4);
    if ((pc & 0xFF000000) != 0x08000000) return -1;

    return 0;
}

__asm void set_msp(uint32_t sp)
{
    MSR msp, r0
    BX lr
}

void BootloaderV2_JumpToApp(void)
{
    /* 跳转前关闭 ESP01S 电源。先拉低 PA9（UART TX）→ 主动拉低 ESP RX，
     * 克服 ESP 内部上拉电阻（40-50kΩ 到 VCC）导致的回灌供电。
     * 仅输入浮空不够——ESP 内部上拉会通过 RX 引脚向 VCC 灌电。 */
    {
        GPIO_InitTypeDef gpio;
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

        /* PA9 推挽输出低电平，克服 ESP RX 内部上拉 */
        gpio.GPIO_Pin = PIN_ESP_TX_PIN;
        gpio.GPIO_Mode = GPIO_Mode_Out_PP;
        gpio.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_Init(PIN_ESP_TX_PORT, &gpio);
        GPIO_ResetBits(PIN_ESP_TX_PORT, PIN_ESP_TX_PIN);

        /* ESP_EN 置高，关断 P-MOS */
        gpio.GPIO_Pin = PIN_ESP_EN_PIN;
        gpio.GPIO_Mode = GPIO_Mode_Out_PP;
        gpio.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_Init(PIN_ESP_EN_PORT, &gpio);
        GPIO_SetBits(PIN_ESP_EN_PORT, PIN_ESP_EN_PIN);
    }

    __disable_irq();
    RCC_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    SCB->VTOR = APP_ADDR;

    uint32_t sp = *(__IO uint32_t*)APP_ADDR;
    uint32_t pc = *(__IO uint32_t*)(APP_ADDR + 4);

    set_msp(sp);
    { void (*jump)(void) = (void (*)(void))pc; jump(); }
}

void BootloaderV2_ShowError(void)
{
    BL_TFT_Init();
    BL_TFT_ShowError("No Valid APP!");
}

static void Delay_ms(uint16_t ms)
{
    /* 每毫秒约 SystemCoreClock/10000 次循环（每条循环约 10 周期） */
    uint32_t per_ms = (SystemCoreClock + 9999U) / 10000U;
    for (volatile uint32_t i = 0; i < (uint32_t)ms * per_ms; i++) {
        if ((i & 0x1FFFFU) == 0U) Watchdog_Kick();
    }
}

uint32_t CRC32_Calculate(uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}