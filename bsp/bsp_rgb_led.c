#ifndef BOOTLOADER_BUILD
#include "bsp_rgb_led.h"
#include "pin_config.h"
#include "system_config.h"
#include "stm32f10x.h"

static uint8_t strip2_buf[24];
static uint8_t strip3_buf[24];
static uint8_t rainbow_pos = 0;

/* ── 硬 NOP 展开延时（72MHz，1 周期=13.9ns，无函数调用/循环开销，时序精确）──
 * BSRR/BRR 写 ~2 周期。NOP16 → 0码高 ~250ns；NOP44 → 1码高 ~667ns。 */
#define NOP16 \
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP(); \
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP()
#define NOP44 \
    NOP16;NOP16;NOP16; \
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP(); \
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP()
#define NOP58 \
    NOP44;NOP16; \
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP(); \
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP()

static void ws2812_send_byte(uint8_t byte, GPIO_TypeDef *port, uint16_t pin)
{
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)) {
            /* 1 码：高 ~667ns，低 ~667ns */
            port->BSRR = pin;  NOP44;
            port->BRR  = pin;  NOP44;
        } else {
            /* 0 码：高 ~250ns（必须<380），低 ~860ns */
            port->BSRR = pin;  NOP16;
            port->BRR  = pin;  NOP58;
        }
    }
}

static void ws2812_send_pixels(uint8_t *data, uint16_t num, GPIO_TypeDef *port, uint16_t pin)
{
    volatile uint32_t d;   /* volatile：防止 -O2/-Os 把复位空循环优化掉导致时序失效 */
    __disable_irq();
    port->BRR = pin;
    for (d = 0; d < 6000; d++);           /* 复位 ~80µs */
    for (uint16_t i = 0; i < num * 3; i++)
        ws2812_send_byte(data[i], port, pin);
    port->BRR = pin;
    for (d = 0; d < 6000; d++);           /* 复位锁存 */
    __enable_irq();
}

static void hsv_to_rgb(uint8_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - region * 43) * 6;
    uint8_t q = 255 - remainder;
    uint8_t t = remainder;
    switch (region) {
        case 0: *r=255; *g=t;   *b=0;   break;
        case 1: *r=q;   *g=255; *b=0;   break;
        case 2: *r=0;   *g=255; *b=t;   break;
        case 3: *r=0;   *g=q;   *b=255; break;
        case 4: *r=t;   *g=0;   *b=255; break;
        default:*r=255; *g=0;   *b=q;   break;
    }
}

/* ── 呼吸灯标准正弦表：一周期 32 点，值 0..1024（(1+sin)/2*1024），平滑无突变 ── */
static const uint16_t breath_sin[32] = {
    512,612,708,797,874,938,985,1014,1024,1014,985,938,874,797,708,612,
    512,419,316,227,150,86,39,10,0,10,39,86,150,227,316,419
};
static uint8_t breath_idx = 0;

/* 当前呼吸亮度：18..92，正弦平滑升降。每 200ms 步进 1（32 帧 ≈ 6.4s 一个呼吸周期），
 * 相邻帧亮度差小 → 平滑无突变 */
static uint8_t breath_brightness(void)
{
    uint16_t f = breath_sin[breath_idx];
    breath_idx = (uint8_t)((breath_idx + 1) & 31);
    return (uint8_t)(18 + (uint16_t)(f * 74) / 1024);
}

void RGB_Strip_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin = PIN_RGB2_PIN | PIN_RGB3_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(PIN_RGB2_PORT, &g);
    GPIO_ResetBits(PIN_RGB2_PORT, PIN_RGB2_PIN | PIN_RGB3_PIN);

    /* 上电连发几次全黑关灯 */
    uint8_t black[3] = {0, 0, 0};
    ws2812_send_pixels(black, 1, PIN_RGB2_PORT, PIN_RGB2_PIN);
    ws2812_send_pixels(black, 1, PIN_RGB3_PORT, PIN_RGB3_PIN);
    ws2812_send_pixels(black, 7, PIN_RGB3_PORT, PIN_RGB3_PIN);
}

static uint8_t rgb_scale(uint8_t c, uint8_t bright)
{
    if (bright >= 100) return c;
    return (uint8_t)(((uint16_t)c * (uint16_t)bright) / 100U);
}

void RGB_Strip2_SetPixels(uint8_t *data, uint16_t num)
{
    uint16_t total = (num > 8) ? 8 : num;
    uint8_t b = g_sys.params.rgb_led_bright;
    for (uint16_t i = 0; i < total * 3; i++) strip2_buf[i] = rgb_scale(data[i], b);
    ws2812_send_pixels(strip2_buf, total, PIN_RGB2_PORT, PIN_RGB2_PIN);
}

void RGB_Strip3_SetPixels(uint8_t *data, uint16_t num)
{
    uint16_t total = (num > 8) ? 8 : num;
    uint8_t b = g_sys.params.rgb_strip_bright;
    for (uint16_t i = 0; i < total * 3; i++) strip3_buf[i] = rgb_scale(data[i], b);
    ws2812_send_pixels(strip3_buf, total, PIN_RGB3_PORT, PIN_RGB3_PIN);
}

void RGB_Status_Red(void)   { uint8_t d[3]={0,64,0};  RGB_Strip2_SetPixels(d, 1); }
void RGB_Status_Green(void) { uint8_t d[3]={64,0,0};  RGB_Strip2_SetPixels(d, 1); }
void RGB_Status_Off(void)   { uint8_t d[3]={0,0,0};   RGB_Strip2_SetPixels(d, 1); }

/* 全灭：状态条 + 进度条的所有 LED 发全零（关灯命令，8 颗上限全部覆盖） */
void RGB_AllOff(void)
{
    uint8_t black[24] = {0};
    RGB_Strip2_SetPixels(black, 8);
    RGB_Strip3_SetPixels(black, 8);
}

void RGB_Progress_Rainbow(void)
{
    uint8_t data[21];
    /* 彩色平滑流动：7 颗灯连续渐变（相邻色相间隔12，跨度72→过渡更柔和不生硬），
     * 整体色相每帧 +10 快速流动 */
    for (int i = 0; i < 7; i++) {
        uint8_t hue = (uint8_t)(rainbow_pos + i * 12);
        uint8_t r, g, b;
        hsv_to_rgb(hue, &r, &g, &b);
        data[i*3]=g>>2; data[i*3+1]=r>>2; data[i*3+2]=b>>2;
    }
    RGB_Strip3_SetPixels(data, 7);
    rainbow_pos += 4;   /* 步进 4：渐变带流动慢一点（64帧×0.2s≈12.8s 一圈） */
}

void RGB_Progress_ColorWheel(uint8_t pos)
{
    uint8_t data[21];
    for (int i = 0; i < 7; i++) {
        uint8_t hue = (pos + i * 18) % 256;
        uint8_t r, g, b;
        hsv_to_rgb(hue, &r, &g, &b);
        data[i*3]=g>>2; data[i*3+1]=r>>2; data[i*3+2]=b>>2;
    }
    RGB_Strip3_SetPixels(data, 7);
}

static uint8_t breath_hue = 0;   /* 彩色循环相位（呼吸循环色） */

void RGB_Progress_DryingBar(uint8_t percent)
{
    uint8_t data[21];
    uint8_t leds_on = (uint8_t)((uint16_t)percent * 7U / 100U);
    uint8_t bri = breath_brightness();   /* 正弦呼吸亮度：18..108 */
    uint8_t r, g, b;
    if (leds_on > 7) leds_on = 7;

    /* 已完成部分统一用一种颜色，颜色在色环上缓慢循环 + 整条呼吸 */
    hsv_to_rgb(breath_hue, &r, &g, &b);

    for (uint8_t i = 0; i < 7; i++) {
        if (i >= 7 - leds_on) {      /* 从最后一颗倒序点亮：第7颗=第一个14% */
            data[i*3]   = (uint8_t)((uint16_t)(g>>2) * bri / 64);
            data[i*3+1] = (uint8_t)((uint16_t)(r>>2) * bri / 64);
            data[i*3+2] = (uint8_t)((uint16_t)(b>>2) * bri / 64);
        } else {                     /* 未完成部分：灭 */
            data[i*3]=0; data[i*3+1]=0; data[i*3+2]=0;
        }
    }
    RGB_Strip3_SetPixels(data, 7);

    breath_hue += 2;   /* 彩色循环：128帧×0.2s≈25.6s 完整一圈，过渡更舒缓 */
}
#endif