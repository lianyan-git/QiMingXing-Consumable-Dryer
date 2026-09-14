#include "ui_comp.h"
#include "bsp_tft_st7789.h"
#include <string.h>

/* 圆角矩形（四角圆弧像素 + FillRect 主体） */
void ui_fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint16_t color, uint8_t radius);

static void fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            uint16_t color, uint8_t radius)
{
    int16_t dx, dy;
    if (radius == 0U) { TFT_FillRect(x, y, w, h, color); return; }
    TFT_FillRect(x + radius, y, w - 2U * radius, h, color);
    TFT_FillRect(x, y + radius, radius, h - 2U * radius, color);
    TFT_FillRect(x + w - radius, y + radius, radius, h - 2U * radius, color);
    for (dy = 0; dy < radius; dy++) {
        uint8_t r2 = (uint8_t)(radius - 1);
        int16_t d = (int16_t)(r2 - dy);
        uint16_t seg = 0;
        for (dx = 0; dx < radius; dx++) {
            int16_t ddx = (int16_t)(r2 - dx);
            if (ddx * ddx + d * d <= (int16_t)(r2 * r2)) seg = (uint16_t)(dx + 1);
        }
        if (seg > 0) {
            TFT_FillRect(x, y + dy, seg, 1, color);
            TFT_FillRect(x + w - seg, y + dy, seg, 1, color);
            TFT_FillRect(x, y + h - 1 - dy, seg, 1, color);
            TFT_FillRect(x + w - seg, y + h - 1 - dy, seg, 1, color);
        }
    }
}

void ui_label(uint16_t x, uint16_t y, const char *text,
              uint16_t color, uint16_t bg, uint8_t size)
{
    TFT_DrawString(x, y, text, color, bg, size);
}

void ui_label_center(uint16_t y, const char *text,
                     uint16_t color, uint16_t bg, uint8_t size)
{
    uint16_t w = (uint16_t)(strlen(text) * 6U * size);
    uint16_t x = (w >= TFT_WIDTH) ? 0U : (uint16_t)((TFT_WIDTH - w) / 2U);
    TFT_DrawString(x, y, text, color, bg, size);
}

void ui_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
               const char *text, uint8_t selected)
{
    fill_round_rect(x, y, w, h, selected ? UI_ACCENT : UI_CARD, 6);
    if (selected) {
        TFT_DrawString(x + 8, y + (uint16_t)((h - 8U) / 2U), text, UI_TEXT, UI_ACCENT, 1);
    } else {
        TFT_DrawString(x + 8, y + (uint16_t)((h - 8U) / 2U), text, UI_TEXT_DIM, UI_CARD, 1);
    }
}

void ui_card(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
             uint16_t fill, uint8_t selected)
{
    fill_round_rect(x, y, w, h, selected ? UI_CARD_HI : fill, 6);
    TFT_FillRect(x, y, w, 2, selected ? UI_ACCENT : UI_CARD_EDGE);
    TFT_FillRect(x, y + h - 2, w, 2, selected ? UI_ACCENT : UI_CARD_EDGE);
    TFT_FillRect(x, y, 2, h, selected ? UI_ACCENT : UI_CARD_EDGE);
    TFT_FillRect(x + w - 2, y, 2, h, selected ? UI_ACCENT : UI_CARD_EDGE);
}

void ui_progress(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 uint8_t pct, uint16_t color)
{
    if (pct > 100) pct = 100;
    TFT_FillRect(x, y, w, 1, UI_CARD_EDGE);
    TFT_FillRect(x, y + h - 1, w, 1, UI_CARD_EDGE);
    TFT_FillRect(x, y, 1, h, UI_CARD_EDGE);
    TFT_FillRect(x + w - 1, y, 1, h, UI_CARD_EDGE);
    TFT_FillRect(x + 1, y + 1, (uint16_t)((w - 2U) * pct / 100U), h - 2U, color);
}

void ui_header(const char *title, uint16_t accent)
{
    TFT_FillRect(0, 0, TFT_WIDTH, 24, UI_TITLE_BG);
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    ui_label_center(6, title, accent, UI_TITLE_BG, 2);
}
