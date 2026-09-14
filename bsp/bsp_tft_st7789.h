#ifndef BSP_TFT_ST7789_H
#define BSP_TFT_ST7789_H

#include <stdint.h>

#define TFT_WIDTH  240U
#define TFT_HEIGHT 135U

#define TFT_COLOR_BLACK    UINT16_C(0x0000)
#define TFT_COLOR_WHITE    UINT16_C(0xFFFF)
#define TFT_COLOR_RED      UINT16_C(0xF800)
#define TFT_COLOR_GREEN    UINT16_C(0x07E0)
#define TFT_COLOR_BLUE     UINT16_C(0x001F)
#define TFT_COLOR_YELLOW   UINT16_C(0xFFE0)
#define TFT_COLOR_CYAN     UINT16_C(0x07FF)
#define TFT_COLOR_MAGENTA  UINT16_C(0xF81F)
#define TFT_COLOR_GRAY     UINT16_C(0x8410)
#define TFT_COLOR_DARKGRAY UINT16_C(0x4208)
#define TFT_COLOR_ORANGE   UINT16_C(0xFC00)

/* Compatibility names used by the existing application drafts. */
#define COLOR_BLACK    TFT_COLOR_BLACK
#define COLOR_WHITE    TFT_COLOR_WHITE
#define COLOR_RED      TFT_COLOR_RED
#define COLOR_GREEN    TFT_COLOR_GREEN
#define COLOR_BLUE     TFT_COLOR_BLUE
#define COLOR_YELLOW   TFT_COLOR_YELLOW
#define COLOR_CYAN     TFT_COLOR_CYAN
#define COLOR_MAGENTA  TFT_COLOR_MAGENTA
#define COLOR_GRAY     TFT_COLOR_GRAY
#define COLOR_DARKGRAY TFT_COLOR_DARKGRAY
#define COLOR_ORANGE   TFT_COLOR_ORANGE

typedef enum {
    TFT_OK = 0,
    TFT_ERROR_ARGUMENT = -1,
    TFT_ERROR_BUS = -2
} TftStatus_t;

TftStatus_t TFT_Init(void);
void TFT_SetBacklight(uint8_t brightness);
void TFT_SetBrightness(uint8_t pct);
void Backlight_Init(void);
TftStatus_t TFT_FillScreen(uint16_t color);
/* RGB565 位图绘制：bitmap 为 RGB565 像素数组（高字节在前），
 * 遇 transparent 颜色像素则填充 background（实现透明效果）。 */
TftStatus_t TFT_DrawBitmap(uint16_t x, uint16_t y,
                           uint16_t width, uint16_t height,
                           const uint16_t *bitmap,
                           uint16_t transparent, uint16_t background);
TftStatus_t TFT_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
TftStatus_t TFT_FillRect(uint16_t x, uint16_t y,
                         uint16_t width, uint16_t height, uint16_t color);
TftStatus_t TFT_DrawChar(uint16_t x, uint16_t y, char character,
                         uint16_t color, uint16_t background, uint8_t scale);
TftStatus_t TFT_DrawString(uint16_t x, uint16_t y, const char *text,
                           uint16_t color, uint16_t background, uint8_t scale);
TftStatus_t TFT_DrawStringZh(uint16_t x, uint16_t y, const char *text,
                             uint16_t color, uint16_t background);
TftStatus_t TFT_DrawProgress(uint16_t x, uint16_t y,
                             uint16_t width, uint16_t height,
                             uint8_t percent,
                             uint16_t foreground, uint16_t background);

/* Pure geometry helper used by both the driver and host-side boundary tests. */
int TFT_ClipRect(uint16_t *x, uint16_t *y, uint16_t *width, uint16_t *height);

#endif /* BSP_TFT_ST7789_H */
