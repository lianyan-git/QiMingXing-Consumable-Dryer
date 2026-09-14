/*
 * bl_tft.h
 * Bootloader TFT驱动 - 精简版
 * 只支持基本显示功能，代码量极小
 */

#ifndef __BL_TFT_H
#define __BL_TFT_H

#include <stdint.h>

#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFC00
#define COLOR_LIME      0x7FE0   /* 黄绿 */
#define COLOR_GRAY      0x8410
#define COLOR_DARK      0x4208
#define COLOR_TITLE_BG  0x0821   /* 标题栏深青灰背景 */
#define COLOR_ERR_BG    0x9000   /* 错误条深红背景 */

// 初始化
void BL_TFT_Init(void);

// 显示升级界面
void BL_TFT_ShowUpgradeScreen(void);

// 复位升级界面“已绘制”标志，使下次进入时整屏绘制一次（之后仍只局部刷新）
void BL_TFT_ResetUpgradeScreen(void);

// 显示状态文本
void BL_TFT_ShowStatus(const char *text);

// 显示AP信息
void BL_TFT_ShowAPInfo(const char *ssid, const char *pass, const char *ip);

// 显示进度条
void BL_TFT_ShowProgressBar(uint8_t percent);

// 显示错误
void BL_TFT_ShowError(const char *text);

// 清屏
void BL_TFT_Clear(uint16_t color);

// 画字符串
void BL_TFT_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint8_t size);

// 画单个字符
void BL_TFT_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint8_t size);

// 填充矩形
void BL_TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

#endif /* __BL_TFT_H */
