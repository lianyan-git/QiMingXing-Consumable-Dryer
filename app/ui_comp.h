#ifndef __UI_COMP_H__
#define __UI_COMP_H__

#include <stdint.h>

/* ── 精简组件层：只含基础元素（label/button/card/progress/header），
 *    不实现 sgl 的全部 widget，控制 ROM ≤ 20K 约束。 ── */

/* 主题色（与 ui_manager 共享） */
#define UI_BG           TFT_COLOR(0x0E, 0x12, 0x1A)
#define UI_CARD         TFT_COLOR(0x1A, 0x22, 0x2E)
#define UI_CARD_HI      TFT_COLOR(0x26, 0x30, 0x40)
#define UI_CARD_EDGE    TFT_COLOR(0x33, 0x40, 0x52)
#define UI_TEXT         TFT_COLOR(0xEC, 0xEF, 0xF3)
#define UI_TEXT_DIM     TFT_COLOR(0x9A, 0xA5, 0xB5)
#define UI_TITLE_BG     TFT_COLOR(0x0A, 0x0E, 0x14)
#define UI_ACCENT       TFT_COLOR(0x2E, 0xA6, 0xFF)
#define UI_ACCENT2      TFT_COLOR(0xFF, 0x5C, 0x6E)
#define UI_OK           TFT_COLOR(0x3E, 0xD8, 0x6E)
#define UI_WARN         TFT_COLOR(0xFF, 0xC1, 0x2E)
#define UI_CYAN         TFT_COLOR(0x29, 0xE0, 0xD8)
#define UI_PURPLE       TFT_COLOR(0xB0, 0x7B, 0xF0)

#define TFT_COLOR(r,g,b)  ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

/* 文本标签 */
void ui_label(uint16_t x, uint16_t y, const char *text,
              uint16_t color, uint16_t bg, uint8_t size);

/* 水平居中的标签 */
void ui_label_center(uint16_t y, const char *text,
                     uint16_t color, uint16_t bg, uint8_t size);

/* 按钮（圆角 + 选中高亮描边） */
void ui_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
               const char *text, uint8_t selected);

/* 圆角卡片容器 */
void ui_card(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
             uint16_t fill, uint8_t selected);

/* 进度条 */
void ui_progress(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 uint8_t pct, uint16_t color);

/* 页面标题栏（深色底 + 分隔线 + 居中标题） */
void ui_header(const char *title, uint16_t accent);

#endif /* __UI_COMP_H__ */
