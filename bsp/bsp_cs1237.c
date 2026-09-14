#ifndef BOOTLOADER_BUILD
#include "bsp_cs1237.h"
#include "pin_config.h"
#include "system_time.h"
#include "stm32f10x.h"

/* CS1237 24 位 Σ-Δ ADC 软件时序驱动
 * 引脚：PA0=DOUT(输入)，PA1=SCLK(推挽输出)
 * 时序：SCLK 上升沿芯片移出一位，MCU 在 SCLK 高电平期间采样 DOUT（MSB 在前，二进制补码）。
 * 转换完成通知：DOUT 由高变低（DRDY）。默认配置：10Hz 输出速率、PGA=128。 */

/* CS1237 24 位 Σ-Δ ADC 软件时序驱动
 * 引脚：PA0=DOUT(输入)，PA1=SCLK(推挽输出)
 * 时序：SCLK 上升沿芯片移出一位，MCU 在 SCLK 高电平期间采样 DOUT（MSB 在前，二进制补码）。
 * 转换完成通知：DOUT 由高变低（DRDY）。默认配置：10Hz 输出速率、PGA=128。
 *
 * 开机超大值根因修复：上电前几百 ms 芯片输出未建立/未接浮空，DOUT 电平随机，
 * 24 位里 bit23=1 → 符号扩展成大负数(≈-2^23)，offset(≈0) - (-2^23) → 重量≈+39000g
 * ——正是"几万克"的来源（大负原始值翻正成巨大正重量，不是算术溢出）。
 * 对策：① DOUT 用内部上拉（断线=高=无效，不再随机翻转）；② |raw| 超出有效量程
 * （±8,000,000，桥式空载远小于满量程）一律拒收；③ 逐帧调用改为"就绪才读"快查，
 * 主循环零阻塞；④ 开机/自动去皮等待真实转换周期（10Hz 节拍），有效样本 <5 个
 * 则放弃本次去皮，杜绝把毛刺当零位后连环重去皮。 */

/* 标定系数：1 LSB 对应的克数。空秤去皮后，放已知重量砝码校准。
 * 实测标定（双传感器并联）：250g 砝码 → raw 差 53248 → 250/53248≈0.004695。
 * 若换传感器/砝码不准，按公式重标：SCALE = 砝码克数 ÷ raw差值 */
#define CS1237_SCALE_G_PER_LSB  0.0047f

/* 非法样本门限：bit23 置位附近的值（含 -2^23 哨兵本身）一律视为无效。
 * 真实桥式载荷在零点附近远低于满量程，此判据不误杀。 */
#define CS1237_RAW_MIN          ((int32_t)-8000000L)
#define CS1237_RAW_MAX          ((int32_t) 8000000L)

static volatile int32_t cs1237_offset = 0;    /* 去皮偏移(raw) */

/* 简单滤波：保留最近4帧原始值取中值，压脉冲毛刺；无滑动平均/双帧确认/闲时探测 */
#define CS1237_MED_N   4
static int32_t med_buf[CS1237_MED_N];
static uint8_t med_cnt = 0;
static uint8_t med_pos = 0;

static void clk_delay(void)
{
    volatile uint32_t d;
    for (d = 0; d < 30; d++);   /* SCLK ≈ 200kHz，低于 CS1237 上限1MHz，保证时序稳定 */
}

/* 读 24 位原始值。
 * wait=0：常规轮询快查——DOUT 高(未就绪)立即返回无效，不阻塞主循环；
 * wait=1：去皮/开机建稳用——最多等 ~150ms 一个转换周期，超时返回无效。
 * 无效统一返回 0x800000 哨兵（与合法值区分：合法判据见 RAW_MIN/MAX）。 */
static int32_t cs1237_read_raw(uint8_t wait)
{
    uint32_t value = 0;
    uint8_t i;

    if (GPIO_ReadInputDataBit(PIN_CS1237_DATA_PORT, PIN_CS1237_DATA_PIN) && !wait) {
        return (int32_t)0x800000;      /* 未就绪：不等待，主循环零阻塞 */
    }

    /* 等待 DOUT 由高变低 = 转换完成可读（10Hz 下一周期 ≤100ms+抖动） */
    {
        uint32_t t0 = SystemTime_Millis();
        while (GPIO_ReadInputDataBit(PIN_CS1237_DATA_PORT, PIN_CS1237_DATA_PIN)) {
            if ((uint32_t)(SystemTime_Millis() - t0) > 150U) return (int32_t)0x800000;
        }
    }

    for (i = 0; i < 24; i++) {
        GPIO_SetBits(PIN_CS1237_CLK_PORT, PIN_CS1237_CLK_PIN);
        clk_delay();
        value = (value << 1);
        if (GPIO_ReadInputDataBit(PIN_CS1237_DATA_PORT, PIN_CS1237_DATA_PIN)) value |= 1U;
        GPIO_ResetBits(PIN_CS1237_CLK_PORT, PIN_CS1237_CLK_PIN);
        clk_delay();
    }
    /* 第 25 个时钟：状态位（忽略） */
    GPIO_SetBits(PIN_CS1237_CLK_PORT, PIN_CS1237_CLK_PIN);
    clk_delay();
    GPIO_ResetBits(PIN_CS1237_CLK_PORT, PIN_CS1237_CLK_PIN);
    clk_delay();

    /* 24 位二进制补码符号扩展：bit23=1 表示负数 */
    if (value & 0x800000UL) value |= 0xFF000000UL;
    if ((int32_t)value > CS1237_RAW_MAX || (int32_t)value < CS1237_RAW_MIN) {
        return (int32_t)0x800000;      /* 满量程附近=上电未建立/断线毛刺，拒收 */
    }
    return (int32_t)value;
}

void CS1237_Init(void)
{
    GPIO_InitTypeDef g;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* SCLK：推挽输出，空闲低 */
    g.GPIO_Pin = PIN_CS1237_CLK_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_CS1237_CLK_PORT, &g);
    GPIO_ResetBits(PIN_CS1237_CLK_PORT, PIN_CS1237_CLK_PIN);

    /* DOUT：上拉输入。断线/未接时恒读高=永不采信，避免浮空随机位拼出满量程大数 */
    g.GPIO_Pin = PIN_CS1237_DATA_PIN;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_CS1237_DATA_PORT, &g);

    cs1237_offset = 0;
    med_cnt = 0;
    med_pos = 0;
}

/* 读重量（克）。去皮后零点=0；传感器未就绪/无效返回 -9999f 表示不可用。
 * 简单滤波：每帧存原始值，取最近4帧中值；快查(不等待)，不阻塞主循环。 */
float CS1237_ReadWeight(void)
{
    int32_t raw, med;
    int32_t tmp[CS1237_MED_N];
    uint8_t i, j;

    raw = cs1237_read_raw(0);
    if (raw == (int32_t)0x800000) return -9999.0f;   /* 无效：未到转换周期/传感器异常 */

    med_buf[med_pos] = raw;
    med_pos = (uint8_t)((med_pos + 1U) % CS1237_MED_N);
    if (med_cnt < CS1237_MED_N) med_cnt++;

    for (i = 0; i < med_cnt; i++) tmp[i] = med_buf[i];
    for (i = 0; i + 1 < med_cnt; i++) {
        for (j = 0; j + 1 < (uint8_t)(med_cnt - 1 - i); j++) {
            if (tmp[j + 1] < tmp[j]) { int32_t t = tmp[j]; tmp[j] = tmp[j + 1]; tmp[j + 1] = t; }
        }
    }
    med = (med_cnt == 4U) ? (int32_t)(((int64_t)tmp[1] + tmp[2]) / 2) : tmp[med_cnt / 2];

    /* 方向取反：下压时 raw 减小（电桥极性），翻转后加压读数为正 */
    return (float)(cs1237_offset - med) * CS1237_SCALE_G_PER_LSB;
}

void CS1237_Tare(void)
{
    int32_t buf[16], srt[16];
    int32_t raw, median, sum = 0;
    uint8_t n = 0, i, j, m = 0;

    /* 等平台稳定：最多采16个有效样本（每个最多等150ms，10Hz 芯片自动跟上节拍），
     * 连续3个样本彼此差≤800LSB即提前结束。
     * 有效样本 <5 个（芯片未建立/断线）则放弃本次去皮，保持旧零位，
     * 避免开机时把毛刺当零位——那正是"几万克+反复自动清零卡死编码器"的源头。 */
    for (i = 0; i < 16; i++) {
        raw = cs1237_read_raw(1);
        if (raw == (int32_t)0x800000) continue;
        buf[n++] = raw;
        if (n >= 3) {
            if (buf[n-1] >= buf[n-2] - 800 && buf[n-1] <= buf[n-2] + 800 &&
                buf[n-2] >= buf[n-3] - 800 && buf[n-2] <= buf[n-3] + 800) break;
        }
    }
    if (n < 5) return;
    for (i = 0; i < n; i++) srt[i] = buf[i];
    for (i = 0; i + 1 < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (srt[j] < srt[i]) { int32_t t = srt[i]; srt[i] = srt[j]; srt[j] = t; }
        }
    }
    median = srt[n / 2];
    /* 剔除距中值±8000LSB以外的离群样本后平均，抗单帧误读污染零位 */
    for (i = 0; i < n; i++) {
        if (buf[i] >= median - 8000 && buf[i] <= median + 8000) { sum += buf[i]; m++; }
    }
    cs1237_offset = (m > 0) ? (sum / m) : median;

    med_cnt = 0;        /* 零位已变，清空滤波缓冲 */
    med_pos = 0;
}
#endif /* BOOTLOADER_BUILD */
