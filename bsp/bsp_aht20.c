#ifndef BOOTLOADER_BUILD
#include "bsp_aht20.h"
#include "pin_config.h"
#include "stm32f10x.h"

/* AHT20 软件 I2C（bit-bang），引脚 PB10=SCL / PB11=SDA */
#define AHT20_I2C_ADDR_WR  0x70   /* 0x38 << 1 */
#define AHT20_I2C_ADDR_RD  0x71

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

    /* 发送初始化命令 0xBE 08 00 */
    i2c_start();
    if (i2c_write_byte(AHT20_I2C_ADDR_WR)) { i2c_stop(); return -1; }
    if (i2c_write_byte(0xBE)) { i2c_stop(); return -1; }
    if (i2c_write_byte(0x08)) { i2c_stop(); return -1; }
    if (i2c_write_byte(0x00)) { i2c_stop(); return -1; }
    i2c_stop();

    /* 等待校准完成（典型 ~10ms） */
    {
        volatile uint32_t t = 0;
        for (t = 0; t < 150000; t++) __NOP();
    }
    return 0;
}

int AHT20_Read(float *temperature, float *humidity)
{
    uint8_t data[6];
    uint8_t i;
    volatile uint32_t t;

    if (temperature == 0 || humidity == 0) return -1;

    /* 触发测量 0xAC 33 00 */
    i2c_start();
    if (i2c_write_byte(AHT20_I2C_ADDR_WR)) { i2c_stop(); return -1; }
    if (i2c_write_byte(0xAC)) { i2c_stop(); return -1; }
    if (i2c_write_byte(0x33)) { i2c_stop(); return -1; }
    if (i2c_write_byte(0x00)) { i2c_stop(); return -1; }
    i2c_stop();

    /* 等待测量完成（典型 ~10ms，最长 ~80ms；72MHz 下 ~200k≈16ms） */
    for (t = 0; t < 200000; t++) __NOP();

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

    return 0;
}
#endif /* BOOTLOADER_BUILD */
