#include "bsp_tft_st7789.h"

#include "bsp_spi1_bus.h"
#include "bsp_tft_port.h"

#ifndef BOOTLOADER_BUILD
#endif

#include "bsp_font_store.h"

#include "bsp_font_store.h"

#include <stddef.h>
#include <string.h>

#define TFT_SPI_TIMEOUT_MS 20U
/* ST7789 1.14寸 135x240（GRAM 240x320）旋转 90°：X=40 / Y=52
 * 与 bootloader bl_tft.c 完全一致 */
#define TFT_X_OFFSET       40U
#define TFT_Y_OFFSET       52U
#define TFT_PIXEL_CHUNK    128U
/* 小于此像素数的矩形用轮询 SPI（DMA 启动/等待开销大于小传输收益） */
#define TFT_DMA_MIN_PIXELS 32U


static TftStatus_t begin_transaction(void)
{
    if (Spi1Bus_Acquire(SPI1_BUS_OWNER_TFT, TFT_SPI_TIMEOUT_MS) != SPI1_BUS_OK) {
        return TFT_ERROR_BUS;
    }
    if (Spi1Bus_Select(SPI1_BUS_OWNER_TFT) != SPI1_BUS_OK) {
        Spi1Bus_Release(SPI1_BUS_OWNER_TFT);
        return TFT_ERROR_BUS;
    }
    return TFT_OK;
}

static void end_transaction(void)
{
    Spi1Bus_Deselect(SPI1_BUS_OWNER_TFT);
    Spi1Bus_Release(SPI1_BUS_OWNER_TFT);
}

static TftStatus_t write_part(int data_mode, const uint8_t *bytes, uint32_t length)
{
    TftPort_SetDataMode(data_mode);
    if (Spi1Bus_Transfer(bytes, NULL, length, TFT_SPI_TIMEOUT_MS) != SPI1_BUS_OK) {
        return TFT_ERROR_BUS;
    }
    return TFT_OK;
}

static TftStatus_t write_part_dma(const uint8_t *bytes, uint32_t length)
{
    TftPort_SetDataMode(1);
    if (Spi1Bus_TransferDma(bytes, length, TFT_SPI_TIMEOUT_MS) != SPI1_BUS_OK) {
        return TFT_ERROR_BUS;
    }
    return TFT_OK;
}

static TftStatus_t write_command(uint8_t command,
                                 const uint8_t *data, uint32_t data_length)
{
    TftStatus_t result = write_part(0, &command, 1U);
    if ((result == TFT_OK) && (data_length != 0U)) {
        result = write_part(1, data, data_length);
    }
    return result;
}

static TftStatus_t set_window(uint16_t x, uint16_t y,
                              uint16_t width, uint16_t height)
{
    uint16_t x0 = (uint16_t)(x + TFT_X_OFFSET);
    uint16_t y0 = (uint16_t)(y + TFT_Y_OFFSET);
    uint16_t x1 = (uint16_t)(x0 + width - 1U);
    uint16_t y1 = (uint16_t)(y0 + height - 1U);
    uint8_t range[4];
    TftStatus_t result;

    range[0] = (uint8_t)(x0 >> 8);
    range[1] = (uint8_t)x0;
    range[2] = (uint8_t)(x1 >> 8);
    range[3] = (uint8_t)x1;
    result = write_command(0x2AU, range, sizeof(range));
    if (result != TFT_OK) return result;

    range[0] = (uint8_t)(y0 >> 8);
    range[1] = (uint8_t)y0;
    range[2] = (uint8_t)(y1 >> 8);
    range[3] = (uint8_t)y1;
    result = write_command(0x2BU, range, sizeof(range));
    if (result != TFT_OK) return result;
    return write_command(0x2CU, NULL, 0U);
}

int TFT_ClipRect(uint16_t *x, uint16_t *y, uint16_t *width, uint16_t *height)
{
    if ((x == NULL) || (y == NULL) || (width == NULL) || (height == NULL) ||
        (*width == 0U) || (*height == 0U) ||
        (*x >= TFT_WIDTH) || (*y >= TFT_HEIGHT)) {
        return 0;
    }
    if (*width > (uint16_t)(TFT_WIDTH - *x)) {
        *width = (uint16_t)(TFT_WIDTH - *x);
    }
    if (*height > (uint16_t)(TFT_HEIGHT - *y)) {
        *height = (uint16_t)(TFT_HEIGHT - *y);
    }
    return 1;
}

TftStatus_t TFT_Init(void)
{
    static const uint8_t madctl[] = {0xA0U};   /* 与 bootloader 一致：MX|MV，旋转90°+180° */
    static const uint8_t color_mode[] = {0x05U};
    static const uint8_t porch[] = {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U};
    static const uint8_t gate_control[] = {0x35U};
    static const uint8_t vcom[] = {0x19U};
    static const uint8_t lcm_control[] = {0x2CU};
    static const uint8_t vdv_enable[] = {0x01U};
    static const uint8_t vrh[] = {0x12U};
    static const uint8_t vdv[] = {0x20U};
    static const uint8_t frame_rate[] = {0x0FU};
    static const uint8_t power[] = {0xA4U, 0xA1U};
    static const uint8_t gamma_positive[] = {
        0xD0U, 0x04U, 0x0DU, 0x11U, 0x13U, 0x2BU, 0x3FU,
        0x54U, 0x4CU, 0x18U, 0x0DU, 0x0BU, 0x1FU, 0x23U
    };
    static const uint8_t gamma_negative[] = {
        0xD0U, 0x04U, 0x0CU, 0x11U, 0x13U, 0x2CU, 0x3FU,
        0x44U, 0x51U, 0x2FU, 0x1FU, 0x1FU, 0x20U, 0x23U
    };
    static const struct {
        uint8_t command;
        const uint8_t *data;
        uint8_t length;
    } sequence[] = {
        {0x36U, madctl, (uint8_t)sizeof(madctl)},
        {0x3AU, color_mode, (uint8_t)sizeof(color_mode)},
        {0xB2U, porch, (uint8_t)sizeof(porch)},
        {0xB7U, gate_control, (uint8_t)sizeof(gate_control)},
        {0xBBU, vcom, (uint8_t)sizeof(vcom)},
        {0xC0U, lcm_control, (uint8_t)sizeof(lcm_control)},
        {0xC2U, vdv_enable, (uint8_t)sizeof(vdv_enable)},
        {0xC3U, vrh, (uint8_t)sizeof(vrh)},
        {0xC4U, vdv, (uint8_t)sizeof(vdv)},
        {0xC6U, frame_rate, (uint8_t)sizeof(frame_rate)},
        {0xD0U, power, (uint8_t)sizeof(power)},
        {0xE0U, gamma_positive, (uint8_t)sizeof(gamma_positive)},
        {0xE1U, gamma_negative, (uint8_t)sizeof(gamma_negative)},
        {0x21U, NULL, 0U},
        {0x29U, NULL, 0U}
    };
    uint32_t index;
    TftStatus_t result;

    TftPort_Init();
    TftPort_SetBacklight(1);   /* 立即点亮背光，后续任何失败都不再关 */
    if (Spi1Bus_Init() != SPI1_BUS_OK) {
        return TFT_ERROR_BUS;
    }
    TftPort_SetReset(0);
    TftPort_DelayMs(20U);
    TftPort_SetReset(1);
    TftPort_DelayMs(120U);

    result = begin_transaction();
    if (result != TFT_OK) {
        TftPort_SetBacklight(1);   /* 中途失败也开背光 */
        return result;
    }
    result = write_command(0x11U, NULL, 0U);
    end_transaction();
    if (result != TFT_OK) {
        TftPort_SetBacklight(1);
        return result;
    }
    TftPort_DelayMs(120U);

    result = begin_transaction();
    if (result != TFT_OK) {
        TftPort_SetBacklight(1);
        return result;
    }
    for (index = 0U; index < (sizeof(sequence) / sizeof(sequence[0])); ++index) {
        result = write_command(sequence[index].command,
                               sequence[index].data, sequence[index].length);
        if (result != TFT_OK) break;
    }
    end_transaction();
    if (result != TFT_OK) {
        TftPort_SetBacklight(1);
        return result;
    }
    result = TFT_FillScreen(TFT_COLOR_BLACK);
    TftPort_SetBacklight(1);   /* 无条件开背光，与 bootloader BL_TFT_Init 一致 */
    return result;
}

void TFT_SetBacklight(uint8_t brightness)
{
    TftPort_SetBacklight(brightness != 0U);
}

TftStatus_t TFT_FillRect(uint16_t x, uint16_t y,
                         uint16_t width, uint16_t height, uint16_t color)
{
    static uint8_t pixels[TFT_PIXEL_CHUNK * 2U];
    uint32_t remaining;
    uint32_t index;
    TftStatus_t result;

    if (!TFT_ClipRect(&x, &y, &width, &height)) return TFT_ERROR_ARGUMENT;
    for (index = 0U; index < TFT_PIXEL_CHUNK; ++index) {
        pixels[index * 2U] = (uint8_t)(color >> 8);
        pixels[index * 2U + 1U] = (uint8_t)color;
    }

    result = begin_transaction();
    if (result != TFT_OK) return result;
    result = set_window(x, y, width, height);
    remaining = (uint32_t)width * (uint32_t)height;
    if (remaining >= TFT_DMA_MIN_PIXELS) {
        while ((result == TFT_OK) && (remaining != 0U)) {
            uint32_t count = (remaining > TFT_PIXEL_CHUNK) ? TFT_PIXEL_CHUNK : remaining;
            result = write_part_dma(pixels, count * 2U);
            remaining -= count;
        }
    } else {
        while ((result == TFT_OK) && (remaining != 0U)) {
            uint32_t count = (remaining > TFT_PIXEL_CHUNK) ? TFT_PIXEL_CHUNK : remaining;
            result = write_part(1, pixels, count * 2U);
            remaining -= count;
        }
    }
    end_transaction();
    return result;
}

TftStatus_t TFT_FillScreen(uint16_t color)
{
    return TFT_FillRect(0U, 0U, TFT_WIDTH, TFT_HEIGHT, color);
}

TftStatus_t TFT_DrawBitmap(uint16_t x, uint16_t y,
                           uint16_t width, uint16_t height,
                           const uint16_t *bitmap,
                           uint16_t transparent, uint16_t background)
{
    static uint8_t row_buf[TFT_WIDTH * 2U];
    TftStatus_t result;
    uint16_t px;
    uint32_t row_i, col;

    if (bitmap == 0) return TFT_ERROR_ARGUMENT;
    if (!TFT_ClipRect(&x, &y, &width, &height)) return TFT_ERROR_ARGUMENT;

    result = begin_transaction();
    if (result != TFT_OK) return result;
    result = set_window(x, y, width, height);

    for (row_i = 0U; (result == TFT_OK) && (row_i < height); ++row_i) {
        for (col = 0U; col < width; ++col) {
            px = bitmap[row_i * width + col];
            px = (px == transparent) ? background : px;
            /* RGB565 高字节在前，显式拆字节避免小端序反了 */
            row_buf[col * 2U] = (uint8_t)(px >> 8);
            row_buf[col * 2U + 1U] = (uint8_t)px;
        }
        result = write_part_dma(row_buf, width * 2U);
    }
    end_transaction();
    return result;
}

TftStatus_t TFT_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    return TFT_FillRect(x, y, 1U, 1U, color);
}

/* 无字库(引导页自举)时的 ASCII 后备 5x7(与外部字库同一数据) */
static const uint8_t ascii_fallback[95][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x23, 0x13, 0x08, 0x64, 0x62 },
    { 0x36, 0x49, 0x29, 0x1A, 0x40 },
    { 0x00, 0x06, 0x04, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x08, 0x08, 0x08, 0x08, 0x08 },
    { 0x00, 0x60, 0x60, 0x00, 0x00 },
    { 0x20, 0x10, 0x08, 0x04, 0x02 },
    { 0x3E, 0x51, 0x49, 0x45, 0x3E },
    { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    { 0x42, 0x61, 0x51, 0x49, 0x46 },
    { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    { 0x18, 0x14, 0x12, 0x7F, 0x10 },
    { 0x27, 0x45, 0x45, 0x45, 0x39 },
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 },
    { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 },
    { 0x06, 0x49, 0x49, 0x29, 0x1E },
    { 0x00, 0x36, 0x36, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x7E, 0x11, 0x11, 0x11, 0x7E },
    { 0x7F, 0x49, 0x49, 0x49, 0x36 },
    { 0x3E, 0x41, 0x41, 0x41, 0x22 },
    { 0x7F, 0x41, 0x41, 0x22, 0x1C },
    { 0x7F, 0x49, 0x49, 0x49, 0x41 },
    { 0x7F, 0x09, 0x09, 0x09, 0x01 },
    { 0x3E, 0x41, 0x49, 0x49, 0x7A },
    { 0x7F, 0x08, 0x08, 0x08, 0x7F },
    { 0x00, 0x41, 0x7F, 0x41, 0x00 },
    { 0x20, 0x40, 0x41, 0x3F, 0x01 },
    { 0x7F, 0x08, 0x14, 0x22, 0x41 },
    { 0x7F, 0x40, 0x40, 0x40, 0x40 },
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
    { 0x7F, 0x04, 0x08, 0x10, 0x7F },
    { 0x3E, 0x41, 0x41, 0x41, 0x3E },
    { 0x7F, 0x09, 0x09, 0x09, 0x06 },
    { 0x3E, 0x41, 0x51, 0x21, 0x5E },
    { 0x7F, 0x09, 0x19, 0x29, 0x46 },
    { 0x46, 0x49, 0x49, 0x49, 0x31 },
    { 0x01, 0x01, 0x7F, 0x01, 0x01 },
    { 0x3F, 0x40, 0x40, 0x40, 0x3F },
    { 0x1F, 0x20, 0x40, 0x20, 0x1F },
    { 0x3F, 0x40, 0x38, 0x40, 0x3F },
    { 0x63, 0x14, 0x08, 0x14, 0x63 },
    { 0x07, 0x08, 0x70, 0x08, 0x07 },
    { 0x61, 0x51, 0x49, 0x45, 0x43 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x40, 0x40, 0x40, 0x40, 0x40 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x7E, 0x11, 0x11, 0x11, 0x7E },
    { 0x7F, 0x49, 0x49, 0x49, 0x36 },
    { 0x3E, 0x41, 0x41, 0x41, 0x22 },
    { 0x7F, 0x41, 0x41, 0x22, 0x1C },
    { 0x7F, 0x49, 0x49, 0x49, 0x41 },
    { 0x7F, 0x09, 0x09, 0x09, 0x01 },
    { 0x3E, 0x41, 0x49, 0x49, 0x7A },
    { 0x7F, 0x08, 0x08, 0x08, 0x7F },
    { 0x00, 0x41, 0x7F, 0x41, 0x00 },
    { 0x20, 0x40, 0x41, 0x3F, 0x01 },
    { 0x7F, 0x08, 0x14, 0x22, 0x41 },
    { 0x7F, 0x40, 0x40, 0x40, 0x40 },
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
    { 0x7F, 0x04, 0x08, 0x10, 0x7F },
    { 0x3E, 0x41, 0x41, 0x41, 0x3E },
    { 0x7F, 0x09, 0x09, 0x09, 0x06 },
    { 0x3E, 0x41, 0x51, 0x21, 0x5E },
    { 0x7F, 0x09, 0x19, 0x29, 0x46 },
    { 0x46, 0x49, 0x49, 0x49, 0x31 },
    { 0x01, 0x01, 0x7F, 0x01, 0x01 },
    { 0x3F, 0x40, 0x40, 0x40, 0x3F },
    { 0x1F, 0x20, 0x40, 0x20, 0x1F },
    { 0x3F, 0x40, 0x38, 0x40, 0x3F },
    { 0x63, 0x14, 0x08, 0x14, 0x63 },
    { 0x07, 0x08, 0x70, 0x08, 0x07 },
    { 0x61, 0x51, 0x49, 0x45, 0x43 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00 },
};

TftStatus_t TFT_DrawChar(uint16_t x, uint16_t y, char character,
                         uint16_t color, uint16_t background, uint8_t scale)
{
    uint8_t columns[5];
    uint8_t column;
    uint8_t row;
    TftStatus_t result;

    if (scale == 0U) return TFT_ERROR_ARGUMENT;
    result = TFT_FillRect(x, y, (uint16_t)(6U * scale),
                          (uint16_t)(8U * scale), background);
    if (result != TFT_OK) return result;
#ifndef BOOTLOADER_BUILD
    if (LangGetAscii((uint8_t)character, columns) != 0) {
        uint8_t chx = (uint8_t)character;
        if (chx >= 0x20U && chx <= 0x7EU) memcpy(columns, ascii_fallback[chx - 0x20U], 5);
        else memset(columns, 0, 5);   /* 引导页/无字库自举 */
    }
#else
    /* Bootloader 无外部字库: 直接使用内部 ASCII 回退表 */
    {
        uint8_t chx = (uint8_t)character;
        if (chx >= 0x20U && chx <= 0x7EU) memcpy(columns, ascii_fallback[chx - 0x20U], 5);
        else memset(columns, 0, 5);
    }
#endif
    for (column = 0U; column < 5U; ++column) {
        for (row = 0U; row < 7U; ++row) {
            if ((columns[column] & (uint8_t)(1U << row)) != 0U) {
                result = TFT_FillRect((uint16_t)(x + column * scale),
                                      (uint16_t)(y + row * scale),
                                      scale, scale, color);
                if (result != TFT_OK) return result;
            }
        }
    }
    return TFT_OK;
}

TftStatus_t TFT_DrawString(uint16_t x, uint16_t y, const char *text,
                           uint16_t color, uint16_t background, uint8_t scale)
{
    TftStatus_t result = TFT_OK;
    uint16_t advance;

    if ((text == NULL) || (scale == 0U)) return TFT_ERROR_ARGUMENT;
    advance = (uint16_t)(6U * scale);
    while ((*text != '\0') && (x < TFT_WIDTH)) {
        result = TFT_DrawChar(x, y, *text, color, background, scale);
        if (result != TFT_OK) return result;
        x = (uint16_t)(x + advance);
        ++text;
    }
    return result;
}

#ifndef BOOTLOADER_BUILD
/* 中文 16x16 字符串绘制：支持 UTF-8 编码的中文 + ASCII 混排。
 * 中文每字 16x16，ASCII 用 TFT_DrawChar scale 1 (6x8) 绘制并占 16px 宽。 */

TftStatus_t TFT_DrawStringZh(uint16_t x, uint16_t y, const char *text,
                             uint16_t color, uint16_t background)
{
    TftStatus_t result = TFT_OK;
    uint8_t i, r, c;
    uint16_t cp;

    if (text == NULL) return TFT_ERROR_ARGUMENT;
    while (*text != '\0') {
        const uint8_t *p = (const uint8_t *)text;
        if (p[0] >= 0xE0U && p[1] != '\0' && p[2] != '\0') {
            cp = (uint16_t)(((uint16_t)(p[0] & 0x0FU) << 12) |
                            ((uint16_t)(p[1] & 0x3FU) << 6) |
                            (uint16_t)(p[2] & 0x3FU));
            {
                uint8_t extg[LANG_GLYPH_BYTES];
                uint8_t have = 0;
                if (LangReady() && LangGetGlyph(cp, extg) == 0) {
                    /* 外部宋体字库(完整 GBK): 与内部字库同格式(阴码/高位先) */
                    result = TFT_FillRect(x, y, 16U, 16U, background);
                    if (result != TFT_OK) return result;
                    for (r = 0U; r < 16U; r++) {
                        for (c = 0U; c < 16U; c++) {
                            if ((extg[r * 2U + (c >> 3)] &
                                 (uint8_t)(0x80U >> (c & 7U))) == 0U) {
                                result = TFT_FillRect((uint16_t)(x + c), (uint16_t)(y + r), 1U, 1U, color);
                                if (result != TFT_OK) return result;
                            }
                        }
                    }
                    have = 1;
                }
                if (!have) {
                    TFT_FillRect(x, y, 16U, 16U, background);
                    for (i = 0U; i < 4U; i++) {
                        TFT_FillRect(x, (uint16_t)(y + i * 4U), 2U, 1U, color);
                        TFT_FillRect((uint16_t)(x + 14U), (uint16_t)(y + i * 4U), 2U, 1U, color);
                    }
                }
            }
            x = (uint16_t)(x + 17U);
            text += 3;
        } else {
            /* ASCII：scale2(12×16) 与中文同高，16px 格内水平居中，顶部对齐 */
            TFT_FillRect(x, y, 16U, 16U, background);
            result = TFT_DrawChar((uint16_t)(x + 2U), y, *text, color, background, 2);
            if (result != TFT_OK) return result;
            x = (uint16_t)(x + 16U);
            ++text;
        }
    }
    return result;
}
#endif /* BOOTLOADER_BUILD */

TftStatus_t TFT_DrawProgress(uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             uint8_t percent,
                             uint16_t foreground, uint16_t background)
{
    uint16_t filled;
    TftStatus_t result;

    if (percent > 100U) percent = 100U;
    result = TFT_FillRect(x, y, width, height, background);
    if (result != TFT_OK) return result;
    filled = (uint16_t)(((uint32_t)width * percent) / 100U);
    if (filled == 0U) return TFT_OK;
    return TFT_FillRect(x, y, filled, height, foreground);
}
