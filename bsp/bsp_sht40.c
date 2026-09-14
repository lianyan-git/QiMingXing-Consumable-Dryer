/*
 * bsp_sht40.c
 * SHT40/SHT41 温湿度传感器驱动（软件 I2C @100kHz，PB10=SCL / PB11=SDA）
 *   - 接口与 AHT20 兼容：SHT40_Init() 0=OK；SHT40_Read() 0/-1/-2 三态
 *   - 高精度 8.26ms 测量，按时间片轮询：返回 -2=进行中，>=20ms 后读结果
 *   - I2C 地址自动探测：0x44（ADDR 接地，常见）/ 0x45（ADDR 悬空或接高）
 *   - CRC-8（poly 0x31）校验，任一失败判读失败
 *   非阻塞：测量由上次读取末尾触发。
 */
#ifndef BOOTLOADER_BUILD
#include "bsp_sht40.h"
#include "pin_config.h"
#include "system_time.h"
#include "stm32f10x.h"

/* SHT40 温度校准偏移（单位 0.1℃，显示值 = 测量值 + 偏移）。
 * 传感器为独立小板+底部铜箔挖空（热隔离良好），出厂校准 ±0.2℃，默认 0（不加偏移）。
 * 若用参考温度计实测后需要微调再改此值（湿度不加偏移）。
 * 注：NTC 链路（B=3950/R25/上拉均为假定值）常见 1~3℃ 个体偏差，已单独在 bsp_ntc.c 校准。 */
#define SHT40_TEMP_OFFSET_10C  (0)


static uint8_t s_wr = 0x80;   /* addr<<1，probe 设置 */
static uint8_t s_rd = 0x81;

#define SHT40_CMD_MEAS_HR  0xFD   /* 高精度 8.26ms，I2C 模式 */
#define SHT40_CMD_SOFTRES  0x94   /* 软复位，>1.1us 脉冲/命令 */
#define SHT40_MEAS_WAIT_MS 20U    /* 8.26ms + 裕度 */

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
    /* ACK 位 */
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
    if (ack) sda_low(); else sda_high();
    i2c_delay();
    scl_high(); i2c_delay(); scl_low(); i2c_delay();
    sda_high();
    return byte;
}

static void sht_delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    for (i = 0; i < (uint32_t)ms * 12000U; i++) __NOP();
}

/* 发送一条单字节命令（START, addr|W, cmd, STOP），返回 0=ACK */
static uint8_t sht40_cmd(uint8_t cmd)
{
    i2c_start();
    if (i2c_write_byte(s_wr)) { i2c_stop(); return 1; }
    if (i2c_write_byte(cmd))  { i2c_stop(); return 1; }
    i2c_stop();
    return 0;
}

static void sht40_soft_reset(void)
{
    i2c_start();
    if (i2c_write_byte(s_wr) == 0) i2c_write_byte(SHT40_CMD_SOFTRES);
    i2c_stop();
    sht_delay_ms(5);
}

/* 地址探测：依次尝试 0x44 / 0x45（发软复位观察 ACK） */
static uint8_t sht40_probe(void)
{
    static const uint8_t tries[2][2] = { {0x88, 0x89}, {0x8A, 0x8B} };
    uint8_t k;
    for (k = 0; k < 2; k++) {
        i2c_start();
        if (i2c_write_byte(tries[k][0]) == 0) {   /* 设备 ACK */
            s_wr = tries[k][0];
            s_rd = tries[k][1];
            i2c_stop();
            return 0;
        }
        i2c_stop();
    }
    return 1;
}

int SHT40_Init(void)
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

    sht_delay_ms(40);      /* 上电稳定 */

    if (sht40_probe() != 0) return -1;   /* 无应答（0x44/0x45 都不 ACK） */
    sht40_soft_reset();
    return 0;
}

/* SHT4x CRC-8: poly 0x31, init 0xFF, 无反射 */
static uint8_t sht40_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    uint8_t i, bit;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint8_t sht_measuring = 0;
static uint32_t sht_trig_ms = 0;

static uint8_t sht40_trigger(void)
{
    /* 若设备消失（总线上电掉）重新探测一次地址 */
    if (sht40_cmd(SHT40_CMD_MEAS_HR) != 0) {
        if (sht40_probe() != 0) return 1;
        if (sht40_cmd(SHT40_CMD_MEAS_HR) != 0) return 1;
    }
    sht_measuring = 1;
    sht_trig_ms = SystemTime_Millis();
    return 0;
}

int SHT40_Read(float *temperature, float *humidity)
{
    uint8_t data[6];
    uint8_t i;

    if (temperature == 0 || humidity == 0) return -1;

    if (!sht_measuring) {
        if (sht40_trigger() != 0) return -1;
        return -2;
    }
    if ((uint32_t)(SystemTime_Millis() - sht_trig_ms) < SHT40_MEAS_WAIT_MS) return -2;

    sht_measuring = 0;

    /* 读 6 字节；SHT4x 测量期间不拉伸时钟（I2C 模式下数据就绪前 NACK，
     * 但 20ms > 8.26ms 已足够），失败时下轮重新触发 */
    i2c_start();
    if (i2c_write_byte(s_rd)) {
        /* NACK：总线掉线/地址变更，下轮重试 probe */
        i2c_stop();
        sht_measuring = 0;
        (void)sht40_trigger();
        return -1;
    }
    for (i = 0; i < 6; i++) {
        data[i] = i2c_read_byte(i < 5 ? 1 : 0);
    }
    i2c_stop();

    if (sht40_crc8(data, 2) != data[2]) return -1;
    if (sht40_crc8(&data[3], 2) != data[5]) return -1;

    {
        uint32_t raw_temp = ((uint32_t)data[0] << 8) | (uint32_t)data[1];
        uint32_t raw_hum  = ((uint32_t)data[3] << 8) | (uint32_t)data[4];
        *temperature = -45.0f + 175.0f * (float)raw_temp / 65535.0f;
        *temperature += (float)SHT40_TEMP_OFFSET_10C / 10.0f;   /* 校准偏移 */
        *humidity    =  -6.0f + 125.0f * (float)raw_hum  / 65535.0f;
    }

    if (*temperature < -40.0f || *temperature > 125.0f) return -1;
    if (*humidity < 0.0f || *humidity > 100.0f) return -1;

    (void)sht40_trigger();   /* 布防下一次测量 */
    return 0;
}
#endif /* BOOTLOADER_BUILD */
