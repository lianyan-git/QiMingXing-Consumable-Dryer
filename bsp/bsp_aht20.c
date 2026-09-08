#ifndef BOOTLOADER_BUILD
#include "bsp_aht20.h"
#include "pin_config.h"
#include "system_time.h"
#include "stm32f10x.h"

/* AHT20 软件 I2C（bit-bang），引脚 PB10=SCL / PB11=SDA */
#define AHT20_I2C_ADDR_WR  0x70   /* 0x38 << 1 */
#define AHT20_I2C_ADDR_RD  0x71
#define AHT20_STATUS_CAL   0x08   /* 状态寄存器 bit3：校准完成(SW-已校准) */

static void sda_high(void)  { GPIO_SetBits(PIN_AHT20_SDA_PORT, PIN_AHT20_SDA_PIN); }
static void sda_low(void)   { GPIO_ResetBits(PIN_AHT20_SDA_PORT, PIN_AHT20_SDA_PIN); }
static void scl_high(void)  { GPIO_SetBits(PIN_AHT20_SCL_PORT, PIN_AHT20_SCL_PIN); }
static void scl_low(void)   { GPIO_ResetBits(PIN_AHT20_SCL_PORT, PIN_AHT20_SCL_PIN); }

static uint8_t sda_read(void)
{
    return GPIO_ReadInputDataBit(PIN_AHT20_SDA_PORT, PIN_AHT20_SDA_PIN) ? 1 : 0;
}

static void i2c_delay(void)
{
    volatile uint8_t d;
    for (d = 0; d < 8; d++) __NOP();
}

static void i2c_start(void)
{
    sda_high(); scl_high(); i2c_delay();
    sda_low();  i2c_delay();
    scl_low();  i2c_delay();
}

static void i2c_stop(void)
{
    scl_low();  i2c_delay();
    sda_low();  i2c_delay();
    scl_high(); i2c_delay();
    sda_high(); i2c_delay();
}

static void i2c_sda_in(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = PIN_AHT20_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_AHT20_SDA_PORT, &g);
}

static void i2c_sda_out(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin = PIN_AHT20_SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_AHT20_SDA_PORT, &g);
    sda_high();
}

static uint8_t i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (byte & 0x80) sda_high(); else sda_low();
        byte <<= 1;
        i2c_delay();
        scl_high(); i2c_delay(); scl_low(); i2c_delay();
    }
    /* ACK */
    sda_high();
    i2c_sda_in();
    i2c_delay();
    scl_high(); i2c_delay();
    uint8_t ack = sda_read();
    scl_low(); i2c_delay();
    i2c_sda_out();
    return ack == 0 ? 0 : 1;
}

static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i, byte = 0;
    i2c_sda_in();
    for (i = 0; i < 8; i++) {
        scl_high(); i2c_delay();
        byte = (byte << 1) | sda_read();
        scl_low(); i2c_delay();
    }
    i2c_sda_out();
    if (ack) { sda_low(); } else { sda_high(); }
    i2c_delay();
    scl_high(); i2c_delay(); scl_low(); i2c_delay();
    sda_high();
    return byte;
}

/* 近似毫秒延时（72MHz，volatile 每次迭代约 6~8 周期，12000 迭代 ≈ 1.0~1.3ms，
 * 略偏长对传感器安全无害）。仅初始化阶段使用，读测量窗口用 SystemTime 精确判断。 */
static void aht_delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    for (i = 0; i < (uint32_t)ms * 12000U; i++) __NOP();
}

/* 读取状态寄存器(0x71→1字节)：bit7 忙、bit3 校准完成 */
static uint8_t aht20_read_status(void)
{
    uint8_t st = 0xFF;
    i2c_start();
    if (i2c_write_byte(AHT20_I2C_ADDR_WR)) { i2c_stop(); return 0xFF; }
    if (i2c_write_byte(0x71)) { i2c_stop(); return 0xFF; }
    /* 重复起始后读 1 字节 */
    i2c_start();
    if (i2c_write_byte(AHT20_I2C_ADDR_RD)) { i2c_stop(); return 0xFF; }
    st = (uint8_t)i2c_read_byte(0);
    i2c_stop();
    return st;
}

/* 软复位 0xBA：后需等 20ms 才能进行其他操作 */
static void aht20_soft_reset(void)
{
    i2c_start();
    if (i2c_write_byte(AHT20_I2C_ADDR_WR) == 0) i2c_write_byte(0xBA);
    i2c_stop();
    aht_delay_ms(20);
}

static uint8_t aht_measuring = 0;   /* 已发 0xAC 触发，等待结果 */
static uint32_t aht_trig_ms = 0;

/* 发送一次测量触发（0xAC 33 00）。成功返回 0。 */
static uint8_t aht20_trigger(void)
{
    i2c_start();
    if (i2c_write_byte(AHT20_I2C_ADDR_WR)) { i2c_stop(); return 1; }
    if (i2c_write_byte(0xAC)) { i2c_stop(); return 1; }
    if (i2c_write_byte(0x33)) { i2c_stop(); return 1; }
    if (i2c_write_byte(0x00)) { i2c_stop(); return 1; }
    i2c_stop();
    aht_measuring = 1;
    aht_trig_ms = SystemTime_Millis();
    return 0;
}

int AHT20_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    g.GPIO_Pin = PIN_AHT20_SCL_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_AHT20_SCL_PORT, &g);
    scl_high();

    i2c_sda_out();
    sda_high();
    i2c_delay();

    aht_measuring = 0;   /* 丢弃未完成的测量状态 */

    /* 上电后至少等 40ms 再发命令（数据手册要求，避免上电时序不稳导致读到噪声） */
    aht_delay_ms(40);

    /* 软复位一次，进入已知状态；随后 0xBE 08 00 初始化，
     * 务必读状态寄存器确认校准位 bit3=1，未就绪则软复位重试（最多3次） */
    {
        uint8_t try;
        aht20_soft_reset();
        for (try = 0; try < 3; try++) {
            /* 发送初始化命令 0xBE 08 00 */
            i2c_start();
            if (i2c_write_byte(AHT20_I2C_ADDR_WR)) { i2c_stop(); aht20_soft_reset(); continue; }
            if (i2c_write_byte(0xBE)) { i2c_stop(); aht20_soft_reset(); continue; }
            if (i2c_write_byte(0x08)) { i2c_stop(); aht20_soft_reset(); continue; }
            if (i2c_write_byte(0x00)) { i2c_stop(); aht20_soft_reset(); continue; }
            i2c_stop();

            /* 等待校准完成（典型 ~10ms） */
            aht_delay_ms(10);

            /* 读状态寄存器，确认校准位已置 1；否则软复位重试 */
            if ((aht20_read_status() & AHT20_STATUS_CAL) != 0) return 0;
            aht20_soft_reset();
        }
    }
    return -1;
}

int AHT20_Read(float *temperature, float *humidity)
{
    uint8_t data[6];
    uint8_t i;

    if (temperature == 0 || humidity == 0) return -1;

    if (!aht_measuring) {
        /* 触发测量 0xAC 33 00，本次仅触发，结果待下次（≥80ms 后）读取。
         * 返回 -2=进行中（不算传感器失败），只有真实读写出错才返回 -1 */
        if (aht20_trigger() != 0) return -1;
        return -2;
    }

    /* 测量时间典型 75~80ms，未到时间返回进行中 */
    if ((uint32_t)(SystemTime_Millis() - aht_trig_ms) < 80U) return -2;

    aht_measuring = 0;

    /* 读取 6 字节 */
    i2c_start();
    if (i2c_write_byte(AHT20_I2C_ADDR_RD)) { i2c_stop(); return -1; }
    for (i = 0; i < 6; i++) {
        data[i] = i2c_read_byte(i < 5 ? 1 : 0);
    }
    i2c_stop();

    /* 校验状态位（bit7 忙，bit3 校准完成） */
    if (data[0] & 0x80) return -1;
    if (!(data[0] & 0x08)) { AHT20_Init(); return -1; }

    uint32_t raw_hum = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)data[3] >> 4);
    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | (uint32_t)data[5];

    *humidity = (float)raw_hum * 100.0f / 1048576.0f;
    *temperature = (float)raw_temp * 200.0f / 1048576.0f - 50.0f;

    /* 合理性校验：温度 -40~125°C，湿度 0~100% */
    if (*temperature < -40.0f || *temperature > 125.0f) return -1;
    if (*humidity < 0.0f || *humidity > 100.0f) return -1;

    /* 读完立即布防下一次测量：主循环 100ms 节拍 >= 80ms 测量时间，
     * 实现每 100ms 刷新一轮数据（原触发/读取交替方式只有一半的刷新率）。 */
    (void)aht20_trigger();
    return 0;
}
#endif /* BOOTLOADER_BUILD */
