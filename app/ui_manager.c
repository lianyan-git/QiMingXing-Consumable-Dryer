#ifndef BOOTLOADER_BUILD
#include "ui_manager.h"
#include "system_config.h"
#include "bsp_tft_st7789.h"
#include "bsp_aht20.h"
#include "bsp_ntc.h"
#include "bsp_fan.h"
#include "bsp_rgb_led.h"
#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define CARD_H         30
#define CARD_GAP       4
#define ACCENT_W       3
#define SEL_FRAME_W    2
#define BTN_Y          218
/* ── 主题色（可运行时切换，变量替代 #define） ── */
#define TFT_COLOR(r,g,b)  ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

/* 亮色主题默认值 */
static uint16_t UI_BG           = TFT_COLOR(0xE8, 0xEA, 0xED);
static uint16_t UI_CARD         = TFT_COLOR(0xFF, 0xFF, 0xFF);
static uint16_t UI_CARD_HI      = TFT_COLOR(0xF0, 0xF4, 0xFF);
static uint16_t UI_TITLE_BG     = TFT_COLOR(0x00, 0x00, 0x00);
static uint16_t CARD_BG_TEMP    = TFT_COLOR(0xFF, 0xF3, 0xE0);
static uint16_t CARD_BG_HUMI    = TFT_COLOR(0xE1, 0xF5, 0xFE);
static uint16_t CARD_BG_WEIGHT  = TFT_COLOR(0xF3, 0xE5, 0xF5);
static uint16_t CARD_BG_PTC     = TFT_COLOR(0xFF, 0xEB, 0xEE);
static uint16_t CARD_BG_TIME    = TFT_COLOR(0xE8, 0xF5, 0xE9);

void theme_apply(void)
{
    if (g_sys.theme == 0) {  /* 亮色 */
        UI_BG        = TFT_COLOR(0xE8, 0xEA, 0xED);
        UI_CARD      = TFT_COLOR(0xFF, 0xFF, 0xFF);
        UI_CARD_HI   = TFT_COLOR(0xF0, 0xF4, 0xFF);
        UI_TITLE_BG  = TFT_COLOR(0x00, 0x00, 0x00);
        CARD_BG_TEMP = TFT_COLOR(0xFF, 0xF3, 0xE0);
        CARD_BG_HUMI = TFT_COLOR(0xE1, 0xF5, 0xFE);
        CARD_BG_WEIGHT = TFT_COLOR(0xF3, 0xE5, 0xF5);
        CARD_BG_PTC  = TFT_COLOR(0xFF, 0xEB, 0xEE);
        CARD_BG_TIME = TFT_COLOR(0xE8, 0xF5, 0xE9);
    } else {  /* 暗色 */
        UI_BG        = TFT_COLOR(0x1E, 0x1E, 0x2E);
        UI_CARD      = TFT_COLOR(0x2D, 0x2D, 0x2D);
        UI_CARD_HI   = TFT_COLOR(0x38, 0x38, 0x38);
        UI_TITLE_BG  = TFT_COLOR(0x0A, 0x0A, 0x0A);
        CARD_BG_TEMP = TFT_COLOR(0x3E, 0x2D, 0x1A);
        CARD_BG_HUMI = TFT_COLOR(0x1A, 0x2D, 0x3E);
        CARD_BG_WEIGHT = TFT_COLOR(0x2D, 0x1A, 0x2D);
        CARD_BG_PTC  = TFT_COLOR(0x3E, 0x1A, 0x1A);
        CARD_BG_TIME = TFT_COLOR(0x1A, 0x3E, 0x1A);
    }
}

#define BTN_H          18
#define BTN_GAP        4

/* 非背景色保持常量 #define */
#define UI_CARD_EDGE    TFT_COLOR(0xD0, 0xD0, 0xD0)
#define UI_TEXT         TFT_COLOR(0x22, 0x22, 0x22)
#define UI_TEXT_DIM     TFT_COLOR(0x88, 0x88, 0x88)
#define UI_ACCENT       TFT_COLOR(0x21, 0x96, 0xF3)
#define UI_ACCENT2      TFT_COLOR(0xFF, 0x52, 0x52)
#define UI_OK           TFT_COLOR(0x4C, 0xAF, 0x50)
#define UI_WARN         TFT_COLOR(0xFF, 0xC1, 0x07)
#define UI_CYAN         TFT_COLOR(0x00, 0xBC, 0xD4)
#define UI_PURPLE       TFT_COLOR(0x9C, 0x27, 0xB0)

/* 主副标题用 TFT_DrawString(scale) 渲染（原版 5x7 点阵） */

static void Delay_ms(uint16_t ms);

/* 普通边框（不保留圆角，用于按钮等） */
static void draw_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    TFT_FillRect(x, y, w, SEL_FRAME_W, color);
    TFT_FillRect(x, y + h - SEL_FRAME_W, w, SEL_FRAME_W, color);
    TFT_FillRect(x, y, SEL_FRAME_W, h, color);
    TFT_FillRect(x + w - SEL_FRAME_W, y, SEL_FRAME_W, h, color);
}

/* 圆角边框：直线段 + 四角圆弧描边（完整跟随圆角轮廓） */
static void draw_frame_rounded(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               uint16_t color, uint8_t r)
{
    int16_t dx, dy;
    int16_t r_inner, r_outer, r_inner2, r_outer2, dist2;

    if (r == 0U) {
        draw_frame(x, y, w, h, color);
        return;
    }
    /* 直线段 */
    TFT_FillRect(x + r, y, w - 2U * r, SEL_FRAME_W, color);
    TFT_FillRect(x + r, y + h - SEL_FRAME_W, w - 2U * r, SEL_FRAME_W, color);
    TFT_FillRect(x, y + r, SEL_FRAME_W, h - 2U * r, color);
    TFT_FillRect(x + w - SEL_FRAME_W, y + r, SEL_FRAME_W, h - 2U * r, color);

    /* 四角圆弧描边：像素落在 [r-SEL_FRAME_W, r] 圆环带内则画 */
    r_outer = (int16_t)r;
    r_inner = (int16_t)(r - SEL_FRAME_W);
    r_outer2 = r_outer * r_outer;
    r_inner2 = r_inner * r_inner;
    for (dy = 0; dy < r; dy++) {
        for (dx = 0; dx < r; dx++) {
            dist2 = (int16_t)((r - 1 - dx) * (r - 1 - dx) + (r - 1 - dy) * (r - 1 - dy));
            if (dist2 <= r_outer2 && dist2 >= r_inner2) {
                TFT_DrawPixel((uint16_t)(x + dx), (uint16_t)(y + dy), color);                    /* 左上 */
                TFT_DrawPixel((uint16_t)(x + w - 1 - dx), (uint16_t)(y + dy), color);            /* 右上 */
                TFT_DrawPixel((uint16_t)(x + dx), (uint16_t)(y + h - 1 - dy), color);            /* 左下 */
                TFT_DrawPixel((uint16_t)(x + w - 1 - dx), (uint16_t)(y + h - 1 - dy), color);    /* 右下 */
            }
        }
    }
}

/* 真圆角矩形：标准圆角——角弧圆心位于 (x+r, y+r)，半径 r。
 * 角区像素中心到圆心距离 > r 者切除，中间用 FillRect 高效填充，四角对称。 */
static void fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            uint16_t color, uint8_t radius)
{
    int16_t i, j;
    if (radius == 0U) { TFT_FillRect(x, y, w, h, color); return; }
    if (radius > w / 2U) radius = (uint8_t)(w / 2U);
    if (radius > h / 2U) radius = (uint8_t)(h / 2U);
    TFT_FillRect(x + radius, y, w - 2U * radius, h, color);
    TFT_FillRect(x, y + radius, radius, h - 2U * radius, color);
    TFT_FillRect(x + w - radius, y + radius, radius, h - 2U * radius, color);
    {
        int32_t r2 = (int32_t)radius * (int32_t)radius * 4;   /* (2r)² */
        for (i = 0; i < radius; i++) {
            int32_t dy = (int32_t)(2 * i + 1) - (int32_t)(2 * radius);
            int32_t maxdx = r2 - dy * dy;                     /* 本行 Δx² 上限 */
            if (maxdx < 0) continue;
            for (j = 0; j < radius; j++) {
                int32_t dx = (int32_t)(2 * j + 1) - (int32_t)(2 * radius);
                if (dx * dx <= maxdx) break;
            }
            if (j < radius) {
                uint16_t cnt = (uint16_t)(radius - j);
                TFT_FillRect((uint16_t)(x + j), (uint16_t)(y + i), cnt, 1U, color);                  /* 左上 */
                TFT_FillRect((uint16_t)(x + w - radius), (uint16_t)(y + i), cnt, 1U, color);         /* 右上 */
                TFT_FillRect((uint16_t)(x + j), (uint16_t)(y + h - 1U - i), cnt, 1U, color);         /* 左下 */
                TFT_FillRect((uint16_t)(x + w - radius), (uint16_t)(y + h - 1U - i), cnt, 1U, color);/* 右下 */
            }
        }
    }
}

/* 圆角描边：厚 th 的边框，圆弧与 fill_round_rect 同圆心——贴合轮廓描边，
 * 内部只有平铺填充，不再出现内层嵌套圆弧。 */
static void draw_round_outline(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                               uint16_t color, uint8_t radius, uint8_t th)
{
    int16_t i, j;
    if (th == 0U || radius == 0U) {
        draw_frame(x, y, w, h, color);
        return;
    }
    if (radius > w / 2U) radius = (uint8_t)(w / 2U);
    if (radius > h / 2U) radius = (uint8_t)(h / 2U);
    if (th > radius) th = radius;
    /* 直线段 */
    TFT_FillRect(x + radius, y, w - 2U * radius, th, color);               /* 上 */
    TFT_FillRect(x + radius, y + h - th, w - 2U * radius, th, color);      /* 下 */
    TFT_FillRect(x, y + radius, th, h - 2U * radius, color);               /* 左 */
    TFT_FillRect(x + w - th, y + radius, th, h - 2U * radius, color);      /* 右 */
    {
        int32_t r2  = (int32_t)radius * (int32_t)radius * 4;               /* (2r)² */
        int32_t rin = (int32_t)(radius - th) * (int32_t)(radius - th) * 4; /* (2(r-th))² */
        for (i = 0; i < radius; i++) {
            int32_t dy = (int32_t)(2 * i + 1) - (int32_t)(2 * radius);
            int32_t dyq = dy * dy;
            int32_t hi = r2 - dyq;
            int32_t lo = rin - dyq;
            if (hi < 0) continue;
            if (lo < 0) lo = 0;
            int16_t j1 = -1, j2 = -1;
            for (j = 0; j < radius; j++) {
                int32_t dx = (int32_t)(2 * j + 1) - (int32_t)(2 * radius);
                int32_t d2 = dx * dx;
                if (d2 >= lo && d2 <= hi) { if (j1 < 0) j1 = j; j2 = j; }
            }
            if (j1 >= 0) {
                uint16_t cnt = (uint16_t)(j2 - j1 + 1);
                /* 右侧镜像：角内相对列 (r-1-j2) 起笔，与左侧严格对称 */
                uint16_t rx = (uint16_t)(x + w - radius + (radius - 1 - j2));
                TFT_FillRect((uint16_t)(x + j1), (uint16_t)(y + i), cnt, 1U, color);                  /* 左上 */
                TFT_FillRect(rx, (uint16_t)(y + i), cnt, 1U, color);                                  /* 右上 */
                TFT_FillRect((uint16_t)(x + j1), (uint16_t)(y + h - 1U - i), cnt, 1U, color);         /* 左下 */
                TFT_FillRect(rx, (uint16_t)(y + h - 1U - i), cnt, 1U, color);                         /* 右下 */
            }
        }
    }
}

/* ── 颜色插值工具（RGB565） ── */
static uint16_t lerp_color(uint16_t c1, uint16_t c2, uint8_t t)
{
    if (t >= 255U) return c2;
    if (t == 0U) return c1;
    uint8_t r1 = (uint8_t)((c1 >> 11) & 0x1F), g1 = (uint8_t)((c1 >> 5) & 0x3F), b1 = (uint8_t)(c1 & 0x1F);
    uint8_t r2 = (uint8_t)((c2 >> 11) & 0x1F), g2 = (uint8_t)((c2 >> 5) & 0x3F), b2 = (uint8_t)(c2 & 0x1F);
    uint8_t r = (uint8_t)(r1 + ((uint16_t)(r2 - r1) * t / 255U));
    uint8_t g = (uint8_t)(g1 + ((uint16_t)(g2 - g1) * t / 255U));
    uint8_t b = (uint8_t)(b1 + ((uint16_t)(b2 - b1) * t / 255U));
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* 选中卡顶部色条：随时间在 ACCENT 与高亮之间呼吸（平滑动画） */
static uint16_t pulse_color(void)
{
    uint32_t t = SystemTime_Millis() % 1600U;   /* 1.6s 周期 */
    uint8_t phase;
    if (t < 800U) phase = (uint8_t)(t * 255U / 800U);
    else phase = (uint8_t)((1600U - t) * 255U / 800U);
    return lerp_color(UI_ACCENT, UI_TEXT, phase);
}

/* 在卡片顶部画高亮呼吸色条（仅选中卡，小区域局部刷新不闪烁） */
static void draw_card_pulse(uint16_t x, uint16_t y, uint16_t w, uint8_t selected)
{
    if (!selected) return;
    TFT_FillRect(x, y, w, 2, pulse_color());
}

static void draw_btn(uint16_t y, const char *label, uint16_t color, uint8_t selected)
{
    TFT_FillRect(5, y, 125, BTN_H, UI_CARD);
    TFT_DrawStringZh(10, y + 1, label, color, UI_CARD);
    if (selected) draw_frame(5, y, 125, BTN_H, UI_ACCENT);
}

/* 绘制右侧滚动条：total_items=总数, per_page=每页可见数, cursor_pos=当前选中项索引 */
static void draw_scrollbar(uint8_t total, uint8_t per_page, uint8_t cursor_pos, uint16_t sy, uint16_t sh)
{
    if (total <= per_page) return;
    uint8_t thumb_h = (uint16_t)sh * per_page / total;
    if (thumb_h > sh / 2) thumb_h = sh / 2;
    if (thumb_h < 8) thumb_h = 8;
    uint16_t thumb_y = sy + (uint16_t)sh * cursor_pos / (total - 1);
    TFT_FillRect(234, sy, 5, sh, UI_CARD_EDGE);
    TFT_FillRect(234, thumb_y, 5, thumb_h, UI_ACCENT);
}

static const uint16_t icon_temp_bmp[240] = {
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xA0E4, 0x90A3, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xB32C, 0xF81F, 0xF81F, 0xF81F,
    0xE617, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xA0A3, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xA8E4, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xEE17, 0x98A2, 0xF81F, 0xF81F, 0xA0C3, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x98C3, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xFEBA, 0x9924, 0xF81F, 0xF81F, 0x98A3, 0xF81F,
    0xF163, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x98A3, 0xF81F, 0xF81F, 0x9882, 0xF81F, 0xF81F, 0xE5B6, 0x98C2,
    0xF81F, 0xF81F, 0x9082, 0xF81F, 0xF81F, 0x9061, 0xF81F, 0xF81F, 0xFEDB, 0xAA8A, 0xF81F, 0xF81F, 0x90A3, 0xF81F, 0xF81F, 0xA0C3,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x9882, 0xF81F, 0xF81F, 0x9882, 0xF81F, 0xF81F, 0xF679, 0xCA2A, 0xF81F, 0xF81F,
    0xA0E4, 0xF81F, 0xF81F, 0xA0C3, 0xF81F, 0xF81F, 0xFE58, 0x98A3, 0xF81F, 0xF81F, 0x9082, 0xF81F, 0xF81F, 0x90A2, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x98A3, 0xF81F, 0xE964, 0xA0C2, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x9882, 0xF81F,
    0xE965, 0xA0C2, 0xF81F, 0xF81F, 0xE575, 0x98A2, 0xF81F, 0xF81F, 0x90A2, 0xF81F, 0xE964, 0x98C2, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xE964, 0x98C2, 0xF81F, 0x9082, 0xF81F, 0xF81F, 0xF81F, 0xA8E3, 0xF81F, 0xF164, 0xE964, 0xE164,
    0x9061, 0xF81F, 0x9103, 0xF81F, 0x98A2, 0xF81F, 0xE965, 0xF81F, 0xDB2B, 0xE943, 0xD964, 0x8861, 0xF81F, 0xF81F, 0x98A2, 0xF81F,
    0xE964, 0xE944, 0xE944, 0xE985, 0xD124, 0x90A2, 0xF81F, 0xF81F, 0x98C3, 0xF81F, 0xE184, 0xE944, 0xE964, 0xF164, 0x9082, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xA0C4, 0xF81F, 0xA0C2, 0x98A2, 0x98C2, 0xA0C3, 0xF81F, 0x88A2, 0xF81F, 0xF81F, 0xF81F, 0x9883, 0xF699,
    0xF81F, 0xF81F, 0x8861, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xAA69, 0x9081, 0x9082, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
};

static void draw_icon_temp(uint16_t x, uint16_t y, uint16_t bg)
{
    TFT_DrawBitmap(x + 2, y - 2, 10, 24, icon_temp_bmp, 0xF81F, bg);
}

/* 重量哑铃图标（30x30 RGB565 位图，透明色 0xF81F） */
/* 重量哑铃图标（18x17 RGB565 位图，透明色 0xF81F，主体统一 UI_PURPLE） */
static const uint16_t icon_weight_bmp[306] = {
    0xF81F, 0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0xF81F, 0xF81F, 0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0x9936, 0xF81F, 0x9936, 0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0x9936, 0xF81F, 0x9936, 0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x9936, 0x9936, 0x9936, 0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0xF81F, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936, 0x9936,
    0x9936, 0xF81F,
};


/* 重量哑铃图标（18x17 位图，透明色 0xF81F） */
static void draw_icon_weight(uint16_t x, uint16_t y, uint16_t bg)
{
    TFT_DrawBitmap(x, y + 1, 18, 17, icon_weight_bmp, 0xF81F, bg);
}

/* 湿度水滴图标（14x20 RGB565 位图，透明色 0xF81F） */
static const uint16_t icon_humi_bmp[280] = {
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x00E9, 0x0108, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0x08E8, 0x561C, 0x55FC, 0x1147, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0x5D7A, 0x561D, 0x561C, 0x657A, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x00E8, 0x55FC,
    0x561C, 0x55FC, 0x561C, 0x0109, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x00E7, 0x4DDC, 0x55FC, 0x561C, 0x561C,
    0x561C, 0x561C, 0x0108, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x55DC, 0x55FC, 0xA6DD, 0x55FC, 0x561C, 0x4DFC, 0x55FC,
    0x2BF8, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x00E8, 0x55FC, 0x4DFC, 0x55FC, 0x561C, 0x55FC, 0x55FC, 0x561C, 0x2C19, 0x0109,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x4DDC, 0x55FC, 0xFFDE, 0x4DFC, 0x4DFC, 0x561C, 0x55FC, 0x561C, 0x55FC, 0x2BF8, 0xF81F, 0xF81F,
    0xF81F, 0x00C9, 0x55FD, 0x4DFC, 0x55FC, 0x55FC, 0x55FC, 0x55FC, 0x55FC, 0x55FC, 0x4DFC, 0x2BF8, 0x00E9, 0xF81F, 0x0108, 0x55FC,
    0x561C, 0x55DC, 0x4DFC, 0x55FC, 0x55FC, 0x55FC, 0x55FC, 0x561D, 0x561C, 0x23D7, 0x2BF8, 0x0109, 0x0109, 0x55FC, 0xCFBF, 0x65DB,
    0x55FC, 0x4DFC, 0x55FC, 0x55FC, 0x55FC, 0x55FC, 0x55FC, 0x2437, 0x2BF9, 0x00C8, 0x4DFC, 0x55FC, 0xFFFF, 0x55FC, 0x55FC, 0x561C,
    0x55FC, 0x561C, 0x4DFC, 0x55FC, 0x55FC, 0x55FC, 0x2BF9, 0x2BF9, 0x55FC, 0x55FC, 0xFFFF, 0x561D, 0x55FC, 0x561C, 0x561C, 0x55FC,
    0x4DFC, 0x55FC, 0x55FC, 0x451A, 0x2BF9, 0x2C19, 0x561C, 0x55FC, 0xFFFE, 0x561C, 0x4DFC, 0x561C, 0x561C, 0x55FC, 0x55FC, 0x561D,
    0x55FC, 0x2BF9, 0x23F8, 0x2C19, 0x45BB, 0x55FC, 0x55FC, 0x561C, 0x561D, 0x561D, 0x561C, 0x4DFC, 0x561C, 0x561C, 0x561C, 0x2BF9,
    0x2C19, 0x2BF7, 0x00E8, 0x4DFC, 0x55FC, 0xFFFF, 0x561C, 0x561C, 0x561D, 0x561C, 0x55FC, 0x561C, 0x23D8, 0x2BF9, 0x2BF8, 0x0109,
    0xF81F, 0x00E8, 0x4DFC, 0x561C, 0x55FC, 0x4DFC, 0x4DFC, 0x55FC, 0x4D7C, 0x2BF9, 0x2BF8, 0x2C19, 0x0109, 0xF81F, 0xF81F, 0xF81F,
    0x0109, 0x2BF9, 0x2C19, 0x2BF9, 0x2BF9, 0x2BF8, 0x2BF9, 0x2BF9, 0x2BF8, 0x2C19, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x0108,
    0x00E9, 0x2BF8, 0x2C19, 0x2C19, 0x2C19, 0x0109, 0x00C8, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x0108,
    0x00E8, 0x0108, 0x00E7, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
};

static void draw_icon_humi(uint16_t x, uint16_t y, uint16_t bg)
{
    TFT_DrawBitmap(x, y - 1, 14, 20, icon_humi_bmp, 0xF81F, bg);
}

/* PTC 加热图标（30x30 RGB565 位图，透明色 0xF81F，主体统一 UI_ACCENT2） */
/* PTC 加热图标（火苗，22x22 RGB565，透明色 0xF81F，保留内部多色） */
/* PTC 加热图标（火苗，22x22 RGB565，透明色 0xF81F，外壳 UI_ACCENT2+内芯黄） */
/* PTC 加热图标（火苗，26x26 RGB565，透明色 0xF81F，外圈 UI_ACCENT2 内填黄） */
/* PTC 加热图标（火苗，26x26 RGB565，透明色 0xF81F，外红中橙内黄） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，外红中橙内黄） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，上红环+橙体+底部小黄） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，红环+橙体+底部 5px 小黄焰） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，红环+橙体+底部 5届黄块） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，红环+橙体+底部等比缩小小火焰） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，红环+橙体+底部外圈+中间圈缩小(黄)） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，红环+橙体+底部等比缩放火焰轮廓(黄)） */
/* PTC 火苗图标（30x30 取模，26x26 绘制，RGB565，透明 0xF81F，红环+橙体+底部居中缩小火焰(黄实心)） */
/* PTC 火苗图标（15x21 RGB565，透明 0xF81F） */
static const uint16_t icon_ptc_bmp[315] = {
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF800, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF800, 0xFC00, 0xF800, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF800,
    0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF800, 0xF800, 0xF800, 0xFC00, 0xFC00,
    0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xF800, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00,
    0xFC00, 0xFC00, 0xFC00, 0xF800, 0xF81F, 0xF800, 0xF81F, 0xF81F, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00,
    0xFC00, 0xFC00, 0xFC00, 0xF800, 0xFC00, 0xF81F, 0xF81F, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00,
    0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xF81F, 0xF81F, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFFE0, 0xFC00, 0xFC00, 0xFC00,
    0xFC00, 0xFC00, 0xFC00, 0xF81F, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xFC00,
    0xFC00, 0xFC00, 0xF800, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xFC00,
    0xFC00, 0xF800, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xFC00, 0xFC00,
    0xF800, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xFC00, 0xF800,
    0xF81F, 0xFC00, 0xFC00, 0xFC00, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xFC00, 0xF81F, 0xF81F,
    0xFC00, 0xFC00, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xFC00, 0xF81F, 0xF81F, 0xFC00,
    0xFC00, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xFC00, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFC00, 0xFC00, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xFFE0, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
};














/* PTC 加热图标（15x21 位图，透明色 0xF81F） */
static void draw_icon_ptc(uint16_t x, uint16_t y, uint16_t bg)
{
    TFT_DrawBitmap(x, y - 1, 15, 21, icon_ptc_bmp, 0xF81F, bg);
}

/* 时钟/烘干时间图标（22x22 RGB565，透明色 0xF81F，100x100原图裁剪缩小） */
static const uint16_t icon_clock_bmp[484] = {
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x9D77, 0x5BD1, 0x124D, 0x122D, 0x120C, 0x09CC, 0x4B50, 0x9515, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x63F1, 0x3B50, 0x1B31, 0x12D0, 0x0A4D,
    0x01EC, 0x01EC, 0x020D, 0x022E, 0x024E, 0x226E, 0x63B1, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0x3B2F, 0x1331, 0x01CC, 0x0129, 0x014A, 0x1A6E, 0x2B11, 0x4372, 0x22AF, 0x018B, 0x0129, 0x01EC, 0x028F, 0x220C, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x4370, 0x12F0, 0x016A, 0x224D, 0x4392, 0x12D0, 0x33B3, 0x6CF6, 0x5454, 0x4C34, 0x0290,
    0x3351, 0x09AB, 0x0129, 0x024E, 0x224D, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x5BB1, 0x12F0, 0x0149, 0x01CC, 0x43D3, 0x8D98,
    0x2B92, 0x2352, 0x64B6, 0x7D37, 0x2372, 0x02B0, 0x5C95, 0x0AAF, 0x020D, 0x016A, 0x022E, 0x5350, 0xF81F, 0xF81F, 0xF81F, 0x9D56,
    0x12F0, 0x016A, 0x01AC, 0x02B0, 0x33B2, 0x5434, 0x2B72, 0x02B0, 0x2B92, 0x3392, 0x02B0, 0x02B0, 0x3392, 0x0AD0, 0x02AF, 0x01ED,
    0x016A, 0x020D, 0xF81F, 0xF81F, 0xF81F, 0x5C33, 0x0A6E, 0x11CB, 0x4BF3, 0x6CF7, 0x33B3, 0x02B0, 0x02B0, 0x02AF, 0x3BF3, 0x4BD3,
    0x02AF, 0x02B0, 0x02B0, 0x02B0, 0x1B31, 0x64B6, 0x226E, 0x022D, 0x3AAE, 0xF81F, 0xF81F, 0x2B10, 0x016A, 0x09ED, 0x5C54, 0x64B5,
    0x8578, 0x02B0, 0x02B0, 0x02B0, 0x3BF3, 0x43B2, 0x02AF, 0x02B0, 0x028F, 0x02B0, 0x12F0, 0x43F3, 0x128E, 0x018B, 0x0A0D, 0xF81F,
    0x7C73, 0x12D0, 0x014A, 0x026F, 0x43F3, 0x5C75, 0x33B2, 0x0AD0, 0x0AD0, 0x02B0, 0x1B11, 0x1A8F, 0x02B0, 0x0AF0, 0x4C34, 0x028F,
    0x1311, 0x4C34, 0x12F0, 0x018B, 0x022D, 0x5B70, 0x6433, 0x12F0, 0x016A, 0x1B31, 0x0AF0, 0x02B0, 0x02B0, 0x2352, 0xF81F, 0x3392,
    0x0AB0, 0x0A6E, 0x4C34, 0x9598, 0x4BB2, 0x028F, 0x02B0, 0x0AD0, 0x1B11, 0x01CC, 0x022D, 0x326D, 0x5C13, 0x0AD0, 0x11EC, 0x8578,
    0x4413, 0x02B0, 0x02B0, 0x02B0, 0x12F0, 0x6433, 0x3B30, 0x32AE, 0x6C75, 0x128F, 0x028F, 0x02B0, 0x02B0, 0x2B72, 0x8578, 0x1A6E,
    0x020D, 0x328E, 0x53D2, 0x0AAF, 0x09EC, 0x6CD5, 0x5454, 0x02B0, 0x02B0, 0x02B0, 0x02B0, 0x02AF, 0x01AB, 0x0129, 0x026F, 0x02B0,
    0x02AF, 0x02B0, 0x02B0, 0x33B2, 0x6CB5, 0x228E, 0x126E, 0x328D, 0x5BF2, 0x0AB0, 0x018B, 0x2B72, 0x12F1, 0x02B0, 0x02B0, 0x02B0,
    0x02B0, 0x02B0, 0x1B11, 0x0A2D, 0x02B0, 0x02AF, 0x02B0, 0x02B0, 0x02B0, 0x0AD0, 0x2B51, 0x01CC, 0x1AAF, 0x326D, 0x7432, 0x024E,
    0x014A, 0x028F, 0x3392, 0x1B10, 0x02B0, 0x02B0, 0x02B0, 0x02B0, 0x5454, 0x3BB2, 0x02AF, 0x02AF, 0x02AF, 0x02B0, 0x02B0, 0x4C55,
    0x028F, 0x09EC, 0x0A4E, 0x5B50, 0xF81F, 0x22AE, 0x01CC, 0x01ED, 0x7D16, 0x4C34, 0x02B0, 0x02B0, 0x02B0, 0x02B0, 0x4C13, 0x3BB2,
    0x02B0, 0x02B0, 0x02B0, 0x02B0, 0x33B3, 0xF81F, 0x0A2D, 0x09EC, 0x120D, 0xF81F, 0xF81F, 0x5BD1, 0x026E, 0x018A, 0x5C54, 0x2B92,
    0x02B0, 0x02B0, 0x02B0, 0x02AF, 0x02B0, 0x02B0, 0x02B0, 0x02AF, 0x02B0, 0x02B0, 0x2372, 0x7D17, 0x1A2D, 0x126E, 0x3AAD, 0xF81F,
    0xF81F, 0xF81F, 0x026E, 0x020C, 0x01CC, 0x02B0, 0x12F0, 0x6474, 0x0AB0, 0x02B0, 0x1311, 0x2372, 0x02B0, 0x2351, 0x8578, 0x1B31,
    0x02B0, 0x01CC, 0x120D, 0x09ED, 0x9D35, 0xF81F, 0xF81F, 0xF81F, 0x6BF1, 0x026E, 0x01CC, 0x01AB, 0x12F0, 0x43F3, 0x02B0, 0x02B0,
    0x5C75, 0x4C34, 0x02B0, 0x1331, 0x5454, 0x4413, 0x0A0D, 0x0A0C, 0x020D, 0x63B1, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x5350,
    0x024E, 0x01EC, 0x11EC, 0x12AF, 0x026F, 0x02B0, 0x43F3, 0x5434, 0x02B0, 0x028F, 0x3330, 0x09AC, 0x124D, 0x0A2E, 0x42CE, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x7C73, 0x01EC, 0x024E, 0x01CC, 0x016B, 0x01AC, 0x122D, 0x122D, 0x018B, 0x09AB,
    0x122D, 0x12AF, 0x09EC, 0x6BF1, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x326D, 0x09EC,
    0x020D, 0x020D, 0x020D, 0x020D, 0x022E, 0x022D, 0x09CC, 0x19EB, 0x8CD4, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0xF81F, 0x8473, 0x324C, 0x324D, 0x324D, 0x324D, 0x7C32, 0xF81F, 0xF81F, 0xF81F, 0xF81F,
    0xF81F, 0xF81F, 0xF81F, 0xF81F,
};



static void draw_icon_clock(uint16_t x, uint16_t y, uint16_t bg)
{
    TFT_DrawBitmap(x, y, 22, 22, icon_clock_bmp, 0xF81F, bg);
}

static void draw_degree(uint16_t x, uint16_t y, uint16_t color, uint8_t scale)
{
    uint8_t s = scale;
    TFT_FillRect(x, y, s * 3, s, color);
    TFT_FillRect(x, y + s, s, s * 2, color);
    TFT_FillRect(x + s * 2, y + s, s, s * 2, color);
    TFT_FillRect(x, y + s * 3, s * 3, s, color);
}

static void draw_degree(uint16_t x, uint16_t y, uint16_t color, uint8_t scale);

/* 小时:分钟:秒 六位数字的 X 坐标（size 4，每个 24px） */
static const uint16_t time_digit_x[6] = {20, 50, 94, 124, 168, 198};

/* 局部重绘单个时间数字 + 光标框（框紧贴字模字形四面，无多余间隙） */
static void refr_time_digit(uint8_t i, uint16_t txt_color, uint16_t border_color)
{
    char buf[8];
    uint16_t dx = time_digit_x[i];
    TFT_FillRect(dx - 1, 55, 22, 30, UI_BG);
    sprintf(buf, "%d", g_sys.time_digits[i]);
    TFT_DrawString(dx, 56, buf, txt_color, UI_BG, 4);
    if (g_sys.time_cursor == i) draw_frame(dx - 1, 55, 22, 30, border_color);
}

/* 选项页右侧数值 + ℃，值右对齐到 x=190 前（避开左侧标签，消除重叠） */
static void draw_val_deg(uint16_t y, const char *s, uint16_t color)
{
    uint8_t len = (uint8_t)strlen(s);
    uint16_t vx = (uint16_t)(190U - (uint16_t)len * 12U);
    TFT_FillRect(150, (uint16_t)(y - 1U), 70, 18, UI_BG);
    TFT_DrawString(vx, y, s, color, UI_BG, 2);
    draw_degree(192, y, color, 1);
    TFT_DrawString(200, y, "C", color, UI_BG, 2);
}

static void draw_card_bg(uint16_t y, uint16_t accent_color)
{
    TFT_FillRect(0, y, ACCENT_W, CARD_H, accent_color);
    TFT_FillRect(ACCENT_W, y, TFT_WIDTH - ACCENT_W, CARD_H, UI_BG);
}

/* 横屏卡片背景（可指定坐标/尺寸），sgl 风格：深色卡片 + 顶边高亮 */
static void draw_card_bg_at(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t accent_color)
{
    TFT_FillRect(x, y, w, h, UI_CARD);
    TFT_FillRect(x, y, w, 2, accent_color);        /* 顶部色条 */
    TFT_FillRect(x, y + h - 2, w, 2, accent_color); /* 底部色条 */
}

void UI_ShowBootScreen(void)
{
    TFT_FillScreen(UI_BG);

    /* 主标题 QIMINGXING（scale=4 白色） */
    {
        const char *title = "QIMINGXING";
        uint16_t tw = (uint16_t)(strlen(title) * 24U);
        TFT_DrawString((uint16_t)((TFT_WIDTH - tw) / 2U), 28, title,
                       TFT_COLOR(0xFF, 0xFF, 0xFF), UI_BG, 4);
    }

    /* 副标题 */
    {
        const char *sub = "Drying Control System";
        uint16_t sw = (uint16_t)(strlen(sub) * 6U);
        TFT_DrawString((uint16_t)((TFT_WIDTH - sw) / 2U), 69, sub,
                       TFT_COLOR(0x7F, 0x84, 0x9C), UI_BG, 1);
    }

    /* 进度条 */
    #define PB_X   30U
    #define PB_Y   92U
    #define PB_W   180U
    #define PB_H   14U
    uint16_t pb_bg = TFT_COLOR(0x6A, 0x6E, 0x88);
    uint16_t pb_fg = TFT_COLOR(0xF5, 0xA6, 0x23);
    fill_round_rect(PB_X, PB_Y, PB_W, PB_H, pb_bg, 7);

    char buf[24];
    uint16_t sry = (uint16_t)(PB_Y + PB_H + 4U);
    uint16_t srx = (uint16_t)((TFT_WIDTH - (13U * 6U + 4U * 6U)) / 2U);
    uint16_t pctx = (uint16_t)(srx + 13U * 6U);
    TFT_DrawString(srx, sry, "system ready ", TFT_COLOR(0x7F, 0x84, 0x9C), UI_BG, 1);

    /* 后台读取传感器 + 风扇安全（不显示数值） */
    uint8_t fan_cooling = 0;

    for (int pct = 0; pct <= 100; pct += 2) {
        uint16_t fw = (uint16_t)((uint32_t)PB_W * (uint32_t)pct / 100U);
        if (fw > 0U) fill_round_rect(PB_X, PB_Y, fw, PB_H, pb_fg, 7);

        TFT_FillRect(pctx, sry, 4U * 6U, 8U, UI_BG);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString(pctx, sry, buf, TFT_COLOR(0x7F, 0x84, 0x9C), UI_BG, 1);

        /* 30% 时读传感器 + 检查 NTC 是否需要散热 */
        if (pct == 30) {
            float t, h;
            if (AHT20_Read(&t, &h) == 0) {
                g_sys.current_temp = t;
                g_sys.current_humidity = h;
            }
            g_sys.ptc_temp = (float)NTC_GetTemperature() / 10.0f;

            /* 只检测 NTC 温度 > 冷却温度 → 开风扇 */
            if (g_sys.ptc_temp > (float)g_sys.params.ptc_cooling_temp) {
                Fan_SetSpeed(100);
                fan_cooling = 1;
            }
        }

        /* 70% 时复查 NTC */
        if (pct == 70 && fan_cooling) {
            g_sys.ptc_temp = (float)NTC_GetTemperature() / 10.0f;
            if (g_sys.ptc_temp <= (float)g_sys.params.ptc_cooling_temp) {
                Fan_SetSpeed(0);
                fan_cooling = 0;
            }
        }

        Delay_ms(8);
    }

    if (fan_cooling) Fan_SetSpeed(100);

    TFT_SetBrightness(0);
    Delay_ms(80);
}

/* 通用横屏标题栏：深色底 + 分隔线 + 居中标题 */
static void draw_page_title(const char *title, uint16_t accent)
{
    TFT_FillRect(0, 0, TFT_WIDTH, 24, UI_TITLE_BG);
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(title) * 12U)) / 2U,
                   6, title, accent, UI_TITLE_BG, 2);
}

/* UTF-8 混合字符串渲染宽度(TFT_DrawStringZh 布局): 中文=17px, ASCII=16px */
static uint16_t zh_str_width(const char *s)
{
    uint16_t w = 0;
    while (*s != '\0') {
        const uint8_t *p = (const uint8_t *)s;
        if (*p >= 0xE0U) { w = (uint16_t)(w + 17U); s += 3; }
        else             { w = (uint16_t)(w + 16U); s += 1; }
    }
    return w;
}

/* 中文标题栏：深色底 + 分隔线 + 按中文宽度居中 */
static void draw_page_title_zh(const char *title, uint16_t accent)
{
    TFT_FillRect(0, 0, TFT_WIDTH, 24, UI_TITLE_BG);
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    TFT_DrawStringZh((TFT_WIDTH - zh_str_width(title)) / 2U, 6, title, accent, UI_TITLE_BG);
}

/* 主界面 5 张卡片（横屏 240x135）：4 张 117x42 + 底部 236x43
 * 左上角为原点，X 向右为正，Y 向下为正，卡片左上角为参考中心 */
static void main_card_rect(uint8_t item, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
    switch (item) {
    case 0: *x = 2;   *y = 2;   break;              /* TEMP */
    case 1: *x = 121; *y = 2;   break;             /* 湿度 */
    case 2: *x = 2;   *y = 46;  break;             /* WEIGHT */
    case 3: *x = 121; *y = 46;  break;             /* PTC */
    default:*x = 2;   *y = 90;  *w = 236; *h = 43; return; /* 时间栏 */
    }
    *w = 117;
    *h = 42;
}

/* 绘制数值+单位（size2 与数字同字号），紧跟数值后，供全屏与局部刷新共用 */
static void draw_value_unit(uint8_t item, uint16_t x, uint16_t y, uint16_t h,
                            uint16_t bg, const char *value, uint16_t val_color)
{
    uint16_t vw = (uint16_t)(strlen(value) * 12U);
    uint16_t vx = (uint16_t)(x + 34);
    uint16_t ux = (uint16_t)(vx + vw + 4);

    TFT_DrawString(vx, y + (h - 16U) / 2U, value, val_color, bg, 2);

    switch (item) {
    case 0:  /* TEMP: °C */
    case 3:  /* PTC: °C */
        TFT_DrawString(ux, y + (h - 16U) / 2U, "C", val_color, bg, 2);
        draw_degree((uint16_t)(ux - 4), y + (h - 16U) / 2U - 1, val_color, 1);
        break;
    case 2:  /* WEIGHT: g */
        TFT_DrawString(ux, y + (h - 16U) / 2U, "g", val_color, bg, 2);
        break;
    default:
        break;
    }
}

/* 绘制单张主界面卡片：大图标(左) + 数值+单位(右)，单位与数字同字号(size2) */
static void draw_main_card(uint8_t item, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t bg,
                           const char *value,
                           uint16_t val_color, uint8_t selected)
{
    uint16_t fill = selected ? UI_CARD_HI : bg;
    uint8_t icon_x, icon_y;

    /* 整卡单层圆角填充 + 圆角描边，内部平铺无嵌套圆弧 */
    fill_round_rect(x, y, w, h, fill, 10);
    draw_round_outline(x, y, w, h, selected ? UI_ACCENT : UI_CARD_EDGE, 10, 2);

    /* 图标 30x30：距卡片左边缘 4px，垂直居中 */
    icon_x = (uint8_t)(x + 4);
    icon_y = (uint8_t)(y + (h - 30U) / 2U);
    switch (item) {
    case 0:  draw_icon_temp(icon_x, icon_y, fill); break;
    case 1:  draw_icon_humi(icon_x, icon_y, fill); break;
    case 2:  draw_icon_weight(icon_x, icon_y, fill); break;
    case 3:  draw_icon_ptc(icon_x, icon_y, fill); break;
    default: draw_icon_clock(icon_x - 10, icon_y - 1, fill); break;
    }

    draw_value_unit(item, x, y, h, fill, value, val_color);
}

/* 局部刷新单张主界面卡片（不整屏重绘，SGL 脏矩形思路） */
void UI_RefreshCard(uint8_t item)
{
    char buf[32];
    uint16_t x, y, w, h;
    if (item > 4) return;   /* 防止菜单泄漏的越界索引画出错误卡片 */
    main_card_rect(item, &x, &y, &w, &h);

    if (item == 4) {
        uint32_t hh, mm, ss;
        uint32_t rem_h, rem_m, rem_s;
        uint16_t fill = (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME;
        const char *state_str[] = {"停止烘干", "开始烘干", "开始烘干", "开始烘干", "停止烘干", "停止烘干"};
        uint16_t state_color[] = {UI_TEXT_DIM, UI_ACCENT2, UI_WARN, UI_ACCENT, UI_ACCENT, UI_OK};
        fill_round_rect(x, y, w, h, fill, 10);
        draw_round_outline(x, y, w, h, (g_sys.selected_item == 4) ? UI_ACCENT : UI_CARD_EDGE, 10, 2);
        draw_icon_clock((uint16_t)(x + 4), (uint16_t)(y + (h - 30U) / 2U), fill);
        hh = g_sys.params.dry_time_sec / 3600;
        mm = (g_sys.params.dry_time_sec % 3600) / 60;
        ss = g_sys.params.dry_time_sec % 60;
        sprintf(buf, "%02lu:%02lu:%02lu", hh, mm, ss);
        TFT_DrawString(x + (w - 8U * 12U) / 2U, y + (h - 14U) / 2U, buf, UI_ACCENT, fill, 2);
        TFT_DrawStringZh(166, y + (h - 16U) / 2U, state_str[g_sys.run_state],
                         state_color[g_sys.run_state], fill);
        if (g_sys.run_state == STATE_DRYING) {
            rem_h = g_sys.remaining_sec / 3600;
            rem_m = (g_sys.remaining_sec % 3600) / 60;
            rem_s = g_sys.remaining_sec % 60;
            sprintf(buf, "剩余%02lu:%02lu:%02lu", rem_h, rem_m, rem_s);
            TFT_DrawStringZh(x + (w - zh_str_width(buf)) / 2U, y + 20, buf, UI_OK, fill);
        }
        return;
    }

    switch (item) {
    case 0:
        sprintf(buf, "%.1f", g_sys.current_temp);
        draw_main_card(0, x, y, w, h, CARD_BG_TEMP, buf, UI_WARN,
                       g_sys.selected_item == 0);
        break;
    case 1:
        sprintf(buf, "%.1f%%", g_sys.current_humidity);
        draw_main_card(1, x, y, w, h, CARD_BG_HUMI, buf, UI_CYAN,
                       g_sys.selected_item == 1);
        break;
    case 2:
        sprintf(buf, "%.1f", g_sys.weight_g);
        draw_main_card(2, x, y, w, h, CARD_BG_WEIGHT, buf, UI_PURPLE,
                       g_sys.selected_item == 2);
        break;
    default:
        sprintf(buf, "%.1f", g_sys.ptc_temp);
        draw_main_card(3, x, y, w, h, CARD_BG_PTC, buf, UI_ACCENT2,
                       g_sys.selected_item == 3);
        break;
    }
}

void UI_DrawMainScreen(void)
{
    char buf[32];
    uint32_t h, m, s;
    uint16_t cy;
    const char *state_str[] = {"停止烘干", "开始烘干", "开始烘干", "开始烘干", "停止烘干", "停止烘干"};
    uint16_t state_color[] = {UI_TEXT_DIM, UI_ACCENT2, UI_WARN, UI_ACCENT, UI_ACCENT, UI_OK};

    TFT_FillScreen(UI_BG);

    /* ── 卡1 TEMP 117x42 @(2,2) ── */
    sprintf(buf, "%.1f", g_sys.current_temp);
    draw_main_card(0, 2, 2, 117, 42, CARD_BG_TEMP, buf, UI_WARN,
                   g_sys.selected_item == 0);

    /* ── 卡2 湿度 117x42 @(121,2) ── */
    sprintf(buf, "%.1f%%", g_sys.current_humidity);
    draw_main_card(1, 121, 2, 117, 42, CARD_BG_HUMI, buf, UI_CYAN,
                   g_sys.selected_item == 1);

    /* ── 卡3 WEIGHT 117x42 @(2,46) ── */
    sprintf(buf, "%.1f", g_sys.weight_g);
    draw_main_card(2, 2, 46, 117, 42, CARD_BG_WEIGHT, buf, UI_PURPLE,
                   g_sys.selected_item == 2);

    /* ── 卡4 PTC 117x42 @(121,46) ── */
    sprintf(buf, "%.1f", g_sys.ptc_temp);
    draw_main_card(3, 121, 46, 117, 42, CARD_BG_PTC, buf,
                   UI_ACCENT2, g_sys.selected_item == 3);

    /* ── 第5卡片：烘干时间 236x43 @(2,90) ── */
    cy = 90;
    fill_round_rect(2, cy, 236, 43,
                    (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME, 10);
    draw_round_outline(2, cy, 236, 43,
                       (g_sys.selected_item == 4) ? UI_ACCENT : UI_CARD_EDGE, 10, 2);
    draw_icon_clock(2 + 4, cy + (43U - 30U) / 2U,
                    (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME);
    h = g_sys.params.dry_time_sec / 3600;
    m = (g_sys.params.dry_time_sec % 3600) / 60;
    s = g_sys.params.dry_time_sec % 60;
    sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
    TFT_DrawString(2 + (236U - 8U * 12U) / 2U, cy + (43U - 14U) / 2U, buf, UI_ACCENT,
                   (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME, 2);
    TFT_DrawStringZh(166, cy + (43U - 16U) / 2U, state_str[g_sys.run_state],
                     state_color[g_sys.run_state],
                     (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME);
    if (g_sys.run_state == STATE_DRYING) {
        h = g_sys.remaining_sec / 3600;
        m = (g_sys.remaining_sec % 3600) / 60;
        s = g_sys.remaining_sec % 60;
        sprintf(buf, "剩余%02lu:%02lu:%02lu", h, m, s);
        TFT_DrawStringZh(2 + (236U - zh_str_width(buf)) / 2U, cy + 20, buf, UI_OK,
                   (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME);
    }
}

void UI_DrawWeightScreen(void)
{
    char buf[32];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("重量", UI_ACCENT2);
    sprintf(buf, "当前: %.1fg", g_sys.weight_g);
    TFT_DrawStringZh(30, 35, buf, UI_ACCENT2, UI_BG);
    TFT_DrawStringZh(10, 59, "去皮", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 77, "退出", UI_TEXT_DIM, UI_BG);
    if (g_sys.selected_item == 0) {
        TFT_FillRect(10, 58, 40, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 59, "去皮", UI_TEXT, UI_ACCENT);
    }
    if (g_sys.selected_item == 1) {
        TFT_FillRect(10, 76, 40, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 77, "退出", UI_TEXT, UI_ACCENT);
    }
}

static void refresh_weight_sel(uint8_t old_idx, uint8_t new_idx)
{
    static const char *labels[2] = {"去皮", "退出"};
    static const uint8_t ys[2] = {58, 76};
    static const uint8_t fw[2] = {40, 40};
    uint8_t i;
    for (i = 0; i < 2; i++) {
        if (i != old_idx && i != new_idx) continue;
        if (i == new_idx) {
            TFT_FillRect(10, ys[i], fw[i], 16, UI_ACCENT);
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT, UI_ACCENT);
        } else {
            TFT_FillRect(10, ys[i], fw[i], 16, UI_BG);
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT_DIM, UI_BG);
        }
    }
}

void UI_DrawTempAdjust(void)
{
    char buf[16];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("温度设置", UI_WARN);
    TFT_DrawStringZh(10, 42, "设置温度", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 60, "PID设置", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 78, "返回", UI_TEXT_DIM, UI_BG);
    if (g_sys.selected_item == 0) {
        TFT_FillRect(10, 42, 76, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 42, "设置温度", UI_TEXT, UI_ACCENT);
    }
    if (g_sys.selected_item == 1) {
        TFT_FillRect(10, 60, 92, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 60, "PID设置", UI_TEXT, UI_ACCENT);
    }
    if (g_sys.selected_item == 2) {
        TFT_FillRect(10, 78, 44, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 78, "返回", UI_TEXT, UI_ACCENT);
    }
    sprintf(buf, "%d", g_sys.params.target_temp);
    draw_val_deg(42, buf, UI_WARN);
}

static void refresh_temp_adj_sel(uint8_t old_idx, uint8_t new_idx)
{
    static const char *labels[3] = {"设置温度", "PID设置", "返回"};
    static const uint8_t ys[3] = {42, 60, 78};
    static const uint8_t fw[3] = {76, 92, 44};
    uint8_t i;
    for (i = 0; i < 3; i++) {
        if (i != old_idx && i != new_idx) continue;
        if (i == new_idx) {
            TFT_FillRect(10, ys[i], fw[i], 16, UI_ACCENT);
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT, UI_ACCENT);
        } else {
            TFT_FillRect(10, ys[i], fw[i], 16, UI_BG);
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT_DIM, UI_BG);
        }
    }
}

void UI_DrawTempEdit(void)
{
    char buf[16];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置温度", UI_WARN);
    sprintf(buf, "%d", g_sys.params.target_temp);
    TFT_DrawString(30, 70, buf, COLOR_ORANGE, UI_BG, 4);
    draw_degree(90, 72, COLOR_ORANGE, 2);
    draw_btn(160, "  保存退出", COLOR_GREEN, g_sys.selected_item == 0);
    draw_btn(184, "  取消", UI_TEXT_DIM, g_sys.selected_item == 1);
}

void UI_DrawTempPid(void)
{
    char buf[32];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("温度PID", UI_WARN);
    sprintf(buf, "温度 %d", g_sys.params.target_temp);
    TFT_DrawStringZh((TFT_WIDTH - zh_str_width(buf)) / 2U, 40, buf, COLOR_ORANGE, UI_BG);
    draw_degree(116, 40, COLOR_ORANGE, 2);
    if (g_sys.temp_pid_running) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("启动中")) / 2U, 65, "启动中", COLOR_YELLOW, UI_BG);
        TFT_FillRect(40, 95, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 107, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 95, 2, 14, COLOR_WHITE);
        TFT_FillRect(198, 95, 2, 14, COLOR_WHITE);
        uint8_t pct = g_sys.temp_pid_progress;
        TFT_FillRect(42, 97, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 115, buf, UI_TEXT, UI_BG, 2);
    } else {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("完成")) / 2U, 65, "完成", COLOR_GREEN, UI_BG);
        sprintf(buf, "KP:%.2f KI:%.2f KD:%.2f", g_sys.params.pid_kp, g_sys.params.pid_ki, g_sys.params.pid_kd);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 95, buf, COLOR_CYAN, UI_BG, 2);
    }
    draw_btn(160, "  退出", UI_TEXT_DIM, g_sys.selected_item == 0);
    if (g_sys.selected_item == 0) draw_frame(5, 160, 125, BTN_H, UI_ACCENT);
}

void UI_DrawTimeAdjust(void)
{
    char buf[8];
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置时间", UI_ACCENT);
    for (i = 0; i < 6; i++) {
        sprintf(buf, "%d", g_sys.time_digits[i]);
        TFT_DrawString(time_digit_x[i], 56, buf, COLOR_CYAN, UI_BG, 4);
        if (g_sys.time_cursor == i) draw_frame(time_digit_x[i] - 1, 55, 22, 30, UI_ACCENT2);
    }
    TFT_FillRect(78, 60, 8, 8, UI_ACCENT2);
    TFT_FillRect(78, 72, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 60, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 72, 8, 8, UI_ACCENT2);
}

void UI_DrawTimeEdit(void)
{
    char buf[8];
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置时间", COLOR_YELLOW);
    for (i = 0; i < 6; i++) {
        sprintf(buf, "%d", g_sys.time_digits[i]);
        TFT_DrawString(time_digit_x[i], 56, buf, UI_TEXT, UI_BG, 4);
        if (g_sys.time_cursor == i) draw_frame(time_digit_x[i] - 1, 55, 22, 30, COLOR_YELLOW);
    }
    TFT_FillRect(78, 60, 8, 8, UI_ACCENT2);
    TFT_FillRect(78, 72, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 60, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 72, 8, 8, UI_ACCENT2);
    draw_btn(160, "  保存退出", COLOR_GREEN, g_sys.selected_item == 0);
    draw_btn(184, "  取消", UI_TEXT_DIM, g_sys.selected_item == 1);
}

void UI_DrawPtcAdjust(void)
{
    char buf[16];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("PTC设置", UI_ACCENT2);
    TFT_DrawStringZh(10, 42, "最高温度", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 60, "冷却温度", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 78, "PID设置", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 96, "退出", UI_TEXT_DIM, UI_BG);
    if (g_sys.selected_item == 0) {
        TFT_FillRect(10, 42, 76, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 42, "最高温度", UI_TEXT, UI_ACCENT2);
    }
    if (g_sys.selected_item == 1) {
        TFT_FillRect(10, 60, 76, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 60, "冷却温度", UI_TEXT, UI_ACCENT2);
    }
    if (g_sys.selected_item == 2) {
        TFT_FillRect(10, 78, 92, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 78, "PID设置", UI_TEXT, UI_ACCENT2);
    }
    if (g_sys.selected_item == 3) {
        TFT_FillRect(10, 96, 44, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 96, "退出", UI_TEXT, UI_ACCENT2);
    }
    sprintf(buf, "%d", g_sys.params.ptc_max_temp);
    draw_val_deg(42, buf, COLOR_RED);
    sprintf(buf, "%d", g_sys.params.ptc_cooling_temp);
    draw_val_deg(60, buf, COLOR_CYAN);
}

static void refresh_ptc_adj_sel(uint8_t old_idx, uint8_t new_idx)
{
    static const char *labels[4] = {"最高温度", "冷却温度", "PID设置", "退出"};
    static const uint8_t ys[4] = {42, 60, 78, 96};
    static const uint8_t fw[4] = {76, 76, 92, 44};
    uint8_t i;
    for (i = 0; i < 4; i++) {
        if (i != old_idx && i != new_idx) continue;
        if (i == new_idx) {
            TFT_FillRect(10, ys[i], fw[i], 16, UI_ACCENT2);
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT, UI_ACCENT2);
        } else {
            TFT_FillRect(10, ys[i], fw[i], 16, UI_BG);
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT_DIM, UI_BG);
        }
    }
}

void UI_DrawPtcEdit(void)
{
    char buf[16];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("PTC最高温度", COLOR_RED);
    sprintf(buf, "%d", g_sys.params.ptc_max_temp);
    TFT_DrawString(30, 70, buf, COLOR_RED, UI_BG, 4);
    draw_degree(90, 72, COLOR_RED, 2);
    draw_btn(160, "  保存退出", COLOR_GREEN, g_sys.selected_item == 0);
    draw_btn(184, "  取消", UI_TEXT_DIM, g_sys.selected_item == 1);
}

void UI_DrawPtcCoolingEdit(void)
{
    char buf[16];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("冷却温度", COLOR_CYAN);
    sprintf(buf, "%d", g_sys.params.ptc_cooling_temp);
    TFT_DrawString(30, 70, buf, COLOR_CYAN, UI_BG, 4);
    draw_degree(90, 72, COLOR_CYAN, 2);
    draw_btn(160, "  保存退出", COLOR_GREEN, g_sys.selected_item == 0);
    draw_btn(184, "  取消", UI_TEXT_DIM, g_sys.selected_item == 1);
}

void UI_DrawPidAutotune(void)
{
    char buf[32];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("PID设置", UI_ACCENT2);
    if (g_sys.pid_autotune_running) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("启动中")) / 2U, 45, "启动中", COLOR_YELLOW, UI_BG);
        TFT_FillRect(40, 80, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 92, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 80, 2, 14, COLOR_WHITE);
        TFT_FillRect(198, 80, 2, 14, COLOR_WHITE);
        uint8_t pct = g_sys.pid_autotune_progress;
        TFT_FillRect(42, 82, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 100, buf, UI_TEXT, UI_BG, 2);
    } else {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("完成")) / 2U, 45, "完成", COLOR_GREEN, UI_BG);
        sprintf(buf, "KP:%.2f KI:%.2f KD:%.2f", g_sys.params.pid_kp, g_sys.params.pid_ki, g_sys.params.pid_kd);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 75, buf, COLOR_CYAN, UI_BG, 2);
    }
    draw_btn(160, "  退出", UI_TEXT_DIM, g_sys.selected_item == 0);
    if (g_sys.selected_item == 0) draw_frame(5, 160, 125, BTN_H, UI_ACCENT);
}

static const char *const kMenuItems[] = {"WiFi", "电机", "关于", "设置", "重置", "退出"};
#define MENU_ITEM_COUNT   6u
#define MENU_ROW_H        18u
#define MENU_ROW_GAP      20u
#define MENU_TOP_Y        32u

/* 绘制单个菜单行：选中=蓝底白字+圆角轮廓，未选中=灰字。
 * 未选中分支先 FillRect 清掉旧的高亮底，保证局部刷新无残留。 */
static void draw_menu_row(uint8_t i, uint8_t selected)
{
    uint16_t base_y = (g_sys.scroll_offset == 0) ? MENU_TOP_Y : 0;
    uint16_t y = (uint16_t)(base_y + (uint16_t)(i - g_sys.scroll_offset) * MENU_ROW_GAP);
    if (selected) {
        fill_round_rect(8, y, 224, MENU_ROW_H, UI_ACCENT, 7);
        draw_frame_rounded(8, y, 224, MENU_ROW_H, TFT_COLOR(0x0D, 0x47, 0xA1), 7);
        TFT_DrawStringZh(18, y + 1, kMenuItems[i], UI_TEXT, UI_ACCENT);
    } else {
        TFT_FillRect(8, y, 224, MENU_ROW_H, UI_BG);
        TFT_DrawStringZh(18, y + 1, kMenuItems[i], UI_TEXT_DIM, UI_BG);
    }
}

/* 菜单旋转局部刷新：只重绘旧/新两行，不整屏重绘（避免闪烁） */
void UI_RefreshMenuSel(uint8_t old_idx, uint8_t new_idx)
{
    if (old_idx < MENU_ITEM_COUNT) draw_menu_row(old_idx, 0);
    if (new_idx < MENU_ITEM_COUNT) draw_menu_row(new_idx, 1);
}

void UI_DrawMenu(void)
{
    uint8_t i;
    uint8_t so = g_sys.scroll_offset;
    uint8_t pp = 5;
    uint8_t end = so + pp; if (end > MENU_ITEM_COUNT) end = MENU_ITEM_COUNT;

    TFT_FillScreen(UI_BG);
    if (so == 0) {
        TFT_FillRect(0, 0, TFT_WIDTH, 24, UI_TITLE_BG);
        TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
        TFT_DrawString((TFT_WIDTH - 5 * 12U) / 2U, 6, "MENU", UI_ACCENT, UI_TITLE_BG, 2);
    }
    for (i = so; i < end; i++) {
        draw_menu_row(i, i == g_sys.selected_item);
    }
    draw_scrollbar(MENU_ITEM_COUNT, pp, g_sys.selected_item, (so == 0) ? 28 : 0, (so == 0) ? 100 : 135);
}

void UI_DrawMotorAdjust(void)
{
    char buf[32];
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 9 : 7;

    TFT_FillScreen(UI_BG);
    draw_page_title_zh("电机设置", UI_ACCENT);

    /* 翻页：每页最多 5 项，根据 scroll_offset 只绘制可见项 */
    uint8_t y = 36;
    uint8_t so = g_sys.scroll_offset;
    uint8_t end = so + 5;
    if (end > count) end = count;
    for (uint8_t i = so; i < end; i++) {
        if (i < count - 1) {
            switch (i) {
            case 0: sprintf(buf, "联动:%s", g_sys.params.motor_enabled ? "开" : "关"); break;
            case 1: sprintf(buf, "方向:%s", g_sys.params.motor_direction ? "反转" : "正转"); break;
            case 2: sprintf(buf, "速度:%drpm", g_sys.params.motor_speed); break;
            case 3: sprintf(buf, "摆动:%s", g_sys.params.motor_oscillate ? "开" : "关"); break;
            case 4: sprintf(buf, "角度:%d°", g_sys.params.motor_oscillate_angle); break;
            case 5: sprintf(buf, "驱动:%s", is_tmc ? "TMC2209" : "A4988"); break;
            case 6: sprintf(buf, "电流:%.1fA", (float)g_sys.params.motor_current / 10.0f); break;
            case 7: sprintf(buf, "静音:%s", g_sys.params.motor_stealthchop ? "开" : "关"); break;
            default: buf[0] = 0; break;
            }
        } else {
            sprintf(buf, "退出");
        }
        if (g_sys.selected_item == i) {
            TFT_FillRect(5, y, 220, 16, UI_ACCENT);
            TFT_DrawStringZh(5, y, buf, UI_TEXT, UI_ACCENT);
        } else {
            TFT_DrawStringZh(5, y, buf, UI_TEXT_DIM, UI_BG);
        }
        y += 18;
    }
    draw_scrollbar(count, 5, g_sys.selected_item, 36, 90);
}

void UI_DrawMotorEdit(void)
{
    char buf[32];
    int idx = g_sys.submenu_active;
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);

    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置电机", COLOR_YELLOW);

    switch (idx) {
    case 0: sprintf(buf, "联动:%s", g_sys.params.motor_enabled ? "开" : "关"); break;
    case 1: sprintf(buf, "方向:%s", g_sys.params.motor_direction ? "反转" : "正转"); break;
    case 2: sprintf(buf, "速度:%drpm", g_sys.params.motor_speed); break;
    case 3: sprintf(buf, "摆动:%s", g_sys.params.motor_oscillate ? "开" : "关"); break;
    case 4: sprintf(buf, "角度:%d°", g_sys.params.motor_oscillate_angle); break;
    case 5: sprintf(buf, "驱动:%s", is_tmc ? "TMC2209" : "A4988"); break;
    case 6: sprintf(buf, "电流:%.1fA", (float)g_sys.params.motor_current / 10.0f); break;
    case 7: sprintf(buf, "静音:%s", g_sys.params.motor_stealthchop ? "开" : "关"); break;
    default: break;
    }
    TFT_DrawStringZh(10, 60, buf, COLOR_YELLOW, UI_BG);
    draw_btn(160, "  保存退出", COLOR_GREEN, g_sys.selected_item == 0);
    draw_btn(184, "  取消", UI_TEXT_DIM, g_sys.selected_item == 1);
}

void UI_DrawAbout(void)
{
    char buf[64];
    uint32_t id = g_sys.device_id;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("关于", UI_ACCENT);
    TFT_DrawStringZh((TFT_WIDTH - zh_str_width("烘干机V1.0")) / 2U, 40, "烘干机V1.0", COLOR_YELLOW, UI_BG);
    sprintf(buf, "版本:%s", APP_VERSION);
    TFT_DrawStringZh(5, 60, buf, COLOR_CYAN, UI_BG);
    sprintf(buf, "设备:%s", DEV_NAME);
    TFT_DrawStringZh(5, 78, buf, COLOR_GREEN, UI_BG);
    sprintf(buf, "外壳:%s", DEV_SHELL);
    TFT_DrawStringZh(5, 96, buf, COLOR_GREEN, UI_BG);
    sprintf(buf, "序列号:%08lX", id);
    TFT_DrawStringZh(5, 114, buf, COLOR_ORANGE, UI_BG);
    draw_btn(184, "  退出", UI_TEXT_DIM, g_sys.selected_item == 0);
    if (g_sys.selected_item == 0) draw_frame(5, 184, 125, BTN_H, UI_ACCENT);
}

void UI_DrawWiFiScreen(void)
{
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("WiFi设置", UI_ACCENT);
    if (g_sys.selected_item == 1) {
        TFT_FillRect(10, 60, 90, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 60, g_sys.wifi_enabled ? "WiFi:开" : "WiFi:关", UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, 60, g_sys.wifi_enabled ? "WiFi:开" : "WiFi:关", g_sys.wifi_enabled ? COLOR_GREEN : UI_TEXT_DIM, UI_BG);
    }
    if (g_sys.wifi_enabled) {
        if (g_sys.wifi_ap_mode) {
            TFT_DrawStringZh(10, 78, "模式:AP", COLOR_CYAN, UI_BG);
            TFT_DrawString(10, 96, WIFI_AP_SSID, UI_TEXT, UI_BG, 2);
        } else {
            TFT_DrawStringZh(10, 78, "模式:STA", COLOR_CYAN, UI_BG);
            TFT_DrawString(10, 96, g_sys.wifi_ip, UI_TEXT, UI_BG, 2);
        }
    } else {
        TFT_DrawStringZh(10, 78, "点击开启", COLOR_YELLOW, UI_BG);
    }
    if (g_sys.selected_item == 0) {
        TFT_FillRect(10, 120, 44, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 120, "退出", UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, 120, "退出", UI_TEXT_DIM, UI_BG);
    }
}

static uint8_t ota_screen_drawn = 0;

/* 重新进入 OTA 界面时调用，下次 UI_DrawOTAScreen 会整屏重画 */
void UI_ResetOTAScreen(void)
{
    ota_screen_drawn = 0;
}

void UI_DrawOTAScreen(void)
{
    if (!ota_screen_drawn) {
        ota_screen_drawn = 1;
        TFT_FillScreen(UI_BG);
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("固件")) / 2U, 10, "固件", UI_TEXT, UI_BG);
        draw_btn(184, "  退出", UI_TEXT_DIM, g_sys.selected_item == 0);
        if (g_sys.selected_item == 0) draw_frame(5, 184, 125, BTN_H, UI_ACCENT);
    }
    if (g_sys.ota_downloading) {
        TFT_FillRect(10, 55, 120, 30, UI_BG);
        TFT_DrawStringZh(10, 60, "下载中...", COLOR_YELLOW, UI_BG);
        TFT_FillRect(10, 90, 115, 1, COLOR_WHITE);
        TFT_FillRect(10, 99, 115, 1, COLOR_WHITE);
        TFT_FillRect(10, 90, 1, 10, COLOR_WHITE);
        TFT_FillRect(124, 90, 1, 10, COLOR_WHITE);
        TFT_FillRect(11, 91, (113 * g_sys.ota_progress) / 100, 8, COLOR_GREEN);
        char buf[32];
        sprintf(buf, "%d%% (%lu/%lu)", g_sys.ota_progress, g_sys.ota_received_size, g_sys.ota_total_size);
        TFT_FillRect(10, 110, 120, 12, UI_BG);
        TFT_DrawString(10, 115, buf, UI_TEXT_DIM, UI_BG, 2);
    } else if (g_sys.ota_download_done) {
        TFT_FillRect(10, 55, 120, 60, UI_BG);
        TFT_DrawStringZh(10, 60, "下载完成", COLOR_GREEN, UI_BG);
        TFT_DrawStringZh(10, 85, "点击上传", COLOR_YELLOW, UI_BG);
        TFT_DrawStringZh(10, 103, "设备将重启", UI_TEXT_DIM, UI_BG);
    } else {
        TFT_FillRect(10, 55, 120, 60, UI_BG);
        TFT_DrawStringZh(10, 60, "拖拽固件", UI_TEXT_DIM, UI_BG);
        TFT_DrawStringZh(10, 78, "到网页", UI_TEXT_DIM, UI_BG);
        TFT_DrawStringZh(10, 96, "点击上传", UI_TEXT_DIM, UI_BG);
    }
}

void UI_DrawSafetyAlert(void)
{
    TFT_FillScreen(COLOR_RED);
    TFT_DrawStringZh(5, 30, "安全警报", COLOR_WHITE, COLOR_RED);

    if (g_sys.safety_state == SAFETY_BOX_BROKEN) {
        TFT_DrawStringZh(5, 70, "箱体破损", COLOR_WHITE, COLOR_RED);
        TFT_DrawStringZh(5, 100, "烘干停止", COLOR_WHITE, COLOR_RED);
    } else if (g_sys.safety_state == SAFETY_LID_OPEN) {
        TFT_DrawStringZh(5, 70, "上盖打开", COLOR_WHITE, COLOR_RED);
        TFT_DrawStringZh(5, 100, "烘干停止", COLOR_WHITE, COLOR_RED);
    }

    draw_btn(160, "  确认", COLOR_GREEN, 1);
    draw_frame(5, 160, 125, BTN_H, UI_ACCENT);
}

void UI_DrawSettingsScreen(void)
{
    char buf[32];
    uint8_t i;
    uint8_t cnt = 7;
    uint8_t so = g_sys.scroll_offset;
    uint8_t end = so + 5; if (end > cnt) end = cnt;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置", UI_ACCENT);
    for (i = so; i < end; i++) {
        static const char *kLabels[] = {"蜂鸣器联动","蜂鸣器音量","灯光开关","背光","主题","熄屏","退出"};
        static const char *off_labels[] = {"从不","1s","5s","10s","20s","30s","60s","120s","300s"};
        uint16_t y = (uint16_t)(36 + (i - so) * 18);
        TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT_DIM, UI_BG);
        if (i == g_sys.selected_item) {
            TFT_FillRect(10, y, 95, 16, UI_ACCENT);
            TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT, UI_ACCENT);
        }
        if (i < 6) {
            switch (i) {
            case 0: sprintf(buf, "%s", g_sys.buzzer_link ? "开" : "关"); break;
            case 1: sprintf(buf, "%d/10", g_sys.buzzer_vol); break;
            case 2: sprintf(buf, "%s", g_sys.light_switch ? "开" : "关"); break;
            case 3: sprintf(buf, "%d%%", g_sys.backlight); break;
            case 4: sprintf(buf, "%s", g_sys.theme ? "暗色" : "亮色"); break;
            case 5: sprintf(buf, "%s", (g_sys.screen_off_timeout < 9U) ? off_labels[g_sys.screen_off_timeout] : "从不"); break;
            default: buf[0] = 0;
            }
            TFT_DrawStringZh(120, y, buf, UI_ACCENT, UI_BG);
            if (i == 1 || i == 3) {
                uint8_t m = (i == 1) ? 10U : 100U;
                uint8_t v = (i == 1) ? g_sys.buzzer_vol : g_sys.backlight;
                uint8_t w = (uint8_t)((uint16_t)v * 50U / m);
                TFT_FillRect(165, y + 2, 50, 6, UI_CARD_EDGE);
                if (w) TFT_FillRect(165, y + 2, w, 6, UI_ACCENT);
            }
        }
    }
    draw_scrollbar(cnt, 5, g_sys.selected_item, 36, 90);
}

/* 设置页编辑中数值变化时只局部刷新该项的数值文本+进度条，避免全屏重绘
 * (全屏重绘阻塞主循环→编码器轮询被节流→加速失效，且造成闪屏)。 */
void UI_RefreshSettingsValue(void)
{
    uint8_t i = g_sys.selected_item;
    if (i != 1 && i != 3) return;
    uint16_t y = (uint16_t)(36 + (i - g_sys.scroll_offset) * 18);
    char buf[16];
    uint8_t m, v, w;
    if (i == 1) { m = 10;  v = g_sys.buzzer_vol; sprintf(buf, "%d/10", v); }
    else        { m = 100; v = g_sys.backlight;   sprintf(buf, "%d%%", v); }
    TFT_FillRect(120, y, 40, 16, UI_BG);                 /* 清旧值 */
    TFT_DrawString(120, y, buf, UI_ACCENT, UI_BG, 2);    /* 画新值 */
    w = (uint8_t)((uint16_t)v * 50U / m);
    TFT_FillRect(165, y + 2, 50, 6, UI_CARD_EDGE);       /* 进度条底 */
    if (w) TFT_FillRect(165, y + 2, w, 6, UI_ACCENT);   /* 进度条填充 */
}

static Screen_t last_screen = (Screen_t)255;

void UI_Update(void)
{
    if (g_sys.current_screen != last_screen) {
        g_sys.scroll_offset = 0;
        last_screen = g_sys.current_screen;
        switch (g_sys.current_screen) {
            case SCREEN_MAIN:
                if (g_sys.selected_item > 4) g_sys.selected_item = 0;   /* 菜单泄漏索引收敛 */
                UI_DrawMainScreen();
                break;
            case SCREEN_WEIGHT:        UI_DrawWeightScreen(); break;
            case SCREEN_TEMP_ADJUST:   UI_DrawTempAdjust(); break;
            case SCREEN_TEMP_EDIT:     UI_DrawTempEdit(); break;
            case SCREEN_TEMP_PID:      UI_DrawTempPid(); break;
            case SCREEN_TIME_ADJUST: {
                if (g_sys.prev_screen != SCREEN_TIME_EDIT) {
                    /* 首次进入（来自主界面）：从 dry_time_sec 生成六位数字；
                     * 从编辑页返回时保持已改的数字，便于继续调整。 */
                    uint32_t h = g_sys.params.dry_time_sec / 3600;
                    uint32_t m = (g_sys.params.dry_time_sec % 3600) / 60;
                    uint32_t s = g_sys.params.dry_time_sec % 60;
                    g_sys.time_digits[0] = (uint8_t)(h / 10);
                    g_sys.time_digits[1] = (uint8_t)(h % 10);
                    g_sys.time_digits[2] = (uint8_t)(m / 10);
                    g_sys.time_digits[3] = (uint8_t)(m % 10);
                    g_sys.time_digits[4] = (uint8_t)(s / 10);
                    g_sys.time_digits[5] = (uint8_t)(s % 10);
                }
                if (g_sys.time_cursor >= TIME_DIGIT_COUNT) g_sys.time_cursor = 0;
                UI_DrawTimeAdjust();
                break;
            }

            case SCREEN_TIME_EDIT:   UI_DrawTimeEdit(); break;
            case SCREEN_PTC_ADJUST:    UI_DrawPtcAdjust(); break;
            case SCREEN_PTC_EDIT:      UI_DrawPtcEdit(); break;
            case SCREEN_PTC_COOLING_EDIT: UI_DrawPtcCoolingEdit(); break;
            case SCREEN_PID_AUTOTUNE:  UI_DrawPidAutotune(); break;
            case SCREEN_MENU:          UI_DrawMenu(); break;
            case SCREEN_MOTOR_ADJUST:  UI_DrawMotorAdjust(); break;
            case SCREEN_MOTOR_EDIT:    UI_DrawMotorEdit(); break;
            case SCREEN_ABOUT:         UI_DrawAbout(); break;
            case SCREEN_WIFI:          UI_DrawWiFiScreen(); break;
            case SCREEN_OTA:
                UI_ResetOTAScreen();
                UI_DrawOTAScreen();
                break;
            case SCREEN_SAFETY_ALERT:  UI_DrawSafetyAlert(); break;
        }
    } else {
        switch (g_sys.current_screen) {
            case SCREEN_MAIN:
                UI_UpdateMainDynamic();
                break;
            case SCREEN_OTA:
                if (g_sys.ota_downloading || g_sys.ota_download_done) UI_DrawOTAScreen();
                break;
            case SCREEN_MENU: {
                /* 旋转改变选中项时只重绘旧/新两行+滚动条，ScrollOffset 变化时全屏重绘 */
                static uint8_t last_menu_sel = 0xFF;
                static uint8_t last_menu_so = 0xFF;
                uint8_t cur = g_sys.selected_item;
                uint8_t so = g_sys.scroll_offset;
                if (so != last_menu_so) {
                    last_menu_so = so;
                    last_menu_sel = cur;
                    UI_DrawMenu();
                } else if (cur != last_menu_sel) {
                    if (last_menu_sel < MENU_ITEM_COUNT) UI_RefreshMenuSel(last_menu_sel, cur);
                    last_menu_sel = cur;
                    draw_scrollbar(MENU_ITEM_COUNT, 5, cur, (so == 0) ? 28 : 0, (so == 0) ? 100 : 135);
                }
                break;
            }
            default:
            /* 子页面：仅当该页面相关状态变化时才重绘，避免每 50ms 整屏刷新闪烁 */
            {
                static uint8_t last_sel = 0xFF;
                static uint8_t last_so = 0xFF;
                static Screen_t last_scr = (Screen_t)0xFF;
                static uint8_t last_tc = 0xFF;
                static uint32_t last_dry = 0xFFFFFFFF;
                static uint16_t last_ptc_max = 0xFFFF;
                static uint16_t last_ptc_cool = 0xFFFF;
                static uint16_t last_target = 0xFFFF;
                static uint8_t last_wifi = 0xFF;
                static uint8_t last_pid_prog = 0xFF;
                static uint8_t last_auto_prog = 0xFF;
                uint8_t redraw = 0;
                if (g_sys.current_screen != last_scr) {
                    redraw = 1;
                    last_scr = g_sys.current_screen;
                    /* 重置各页面状态标记，保证首次进入时重绘 */
                    last_sel = 0xFF; last_so = 0xFF; last_tc = 0xFF; last_dry = 0xFFFFFFFF;
                    last_ptc_max = 0xFFFF; last_ptc_cool = 0xFFFF;
                    last_target = 0xFFFF; last_wifi = 0xFF;
                    last_pid_prog = 0xFF; last_auto_prog = 0xFF;
                }
                switch (g_sys.current_screen) {
                    case SCREEN_TEMP_ADJUST: {
                        static uint8_t last_ta_sel = 0xFF;
                        static uint16_t last_ta_temp = 0xFFFF;
                        if (!redraw && g_sys.selected_item != last_ta_sel) {
                            uint8_t old = last_ta_sel;
                            last_ta_sel = g_sys.selected_item;
                            if (old < 3) refresh_temp_adj_sel(old, g_sys.selected_item);
                        }
                        last_ta_sel = g_sys.selected_item;
                        if (!redraw && (uint16_t)g_sys.params.target_temp != last_ta_temp) {
                            last_ta_temp = (uint16_t)g_sys.params.target_temp;
                            char buf[16];
                            sprintf(buf, "%d", g_sys.params.target_temp);
                            draw_val_deg(42, buf, UI_WARN);
                        }
                        break;
                    }
                    case SCREEN_PTC_ADJUST: {
                        static uint8_t last_pa_sel = 0xFF;
                        static uint16_t last_pa_max = 0xFFFF;
                        static uint16_t last_pa_cool = 0xFFFF;
                        if (!redraw && g_sys.selected_item != last_pa_sel) {
                            uint8_t old = last_pa_sel;
                            last_pa_sel = g_sys.selected_item;
                            if (old < 4) refresh_ptc_adj_sel(old, g_sys.selected_item);
                        }
                        last_pa_sel = g_sys.selected_item;
                        if (!redraw && (uint16_t)g_sys.params.ptc_max_temp != last_pa_max) {
                            last_pa_max = (uint16_t)g_sys.params.ptc_max_temp;
                            char buf[16];
                            sprintf(buf, "%d", g_sys.params.ptc_max_temp);
                            draw_val_deg(42, buf, COLOR_RED);
                        }
                        if (!redraw && (uint16_t)g_sys.params.ptc_cooling_temp != last_pa_cool) {
                            last_pa_cool = (uint16_t)g_sys.params.ptc_cooling_temp;
                            char buf[16];
                            sprintf(buf, "%d", g_sys.params.ptc_cooling_temp);
                            draw_val_deg(60, buf, COLOR_CYAN);
                        }
                        break;
                    }
                    case SCREEN_WEIGHT: {
                        static uint8_t last_wt_sel = 0xFF;
                        if (!redraw && g_sys.selected_item != last_wt_sel) {
                            uint8_t old = last_wt_sel;
                            last_wt_sel = g_sys.selected_item;
                            if (old < 2) refresh_weight_sel(old, g_sys.selected_item);
                        }
                        last_wt_sel = g_sys.selected_item;
                        break;
                    }
                    case SCREEN_MOTOR_ADJUST:
                        if (g_sys.selected_item != last_sel || g_sys.scroll_offset != last_so) {
                            redraw = 1; last_sel = g_sys.selected_item; last_so = g_sys.scroll_offset;
                        }
                        break;
                    case SCREEN_ABOUT:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        break;
                    case SCREEN_TEMP_EDIT:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        if ((uint16_t)g_sys.params.target_temp != last_target) {
                            last_target = (uint16_t)g_sys.params.target_temp;
                            if (!redraw) {
                                char buf[16];
                                sprintf(buf, "%d", g_sys.params.target_temp);
                                TFT_DrawString(30, 70, buf, COLOR_ORANGE, UI_BG, 4);
                                draw_degree(90, 72, COLOR_ORANGE, 2);
                            }
                        }
                        break;
                    case SCREEN_TIME_ADJUST:
                        if (!redraw && g_sys.time_cursor != last_tc) {
                            uint8_t oldc = last_tc;
                            if (oldc < TIME_DIGIT_COUNT) refr_time_digit(oldc, COLOR_CYAN, UI_ACCENT2);
                            if (g_sys.time_cursor < TIME_DIGIT_COUNT) refr_time_digit(g_sys.time_cursor, COLOR_CYAN, UI_ACCENT2);
                            last_tc = g_sys.time_cursor;
                        } else {
                            last_tc = g_sys.time_cursor;
                        }
                        break;
                    case SCREEN_TIME_EDIT: {
                        static uint32_t last_dig_hash = 0;
                        uint32_t dig_hash = (uint32_t)g_sys.time_digits[0] | ((uint32_t)g_sys.time_digits[1] << 4) |
                                            ((uint32_t)g_sys.time_digits[2] << 8) | ((uint32_t)g_sys.time_digits[3] << 12) |
                                            ((uint32_t)g_sys.time_digits[4] << 16) | ((uint32_t)g_sys.time_digits[5] << 20);
                        if (!redraw) {
                            if (g_sys.time_cursor != last_tc) {
                                uint8_t oldc = last_tc;
                                if (oldc < 6) refr_time_digit(oldc, UI_TEXT, COLOR_YELLOW);
                                refr_time_digit(g_sys.time_cursor, UI_TEXT, COLOR_YELLOW);
                            }
                            if (dig_hash != last_dig_hash) {
                                refr_time_digit(g_sys.time_cursor, UI_TEXT, COLOR_YELLOW);
                            }
                        }
                        last_tc = g_sys.time_cursor;
                        last_dig_hash = dig_hash;
                        break;
                    }
                    case SCREEN_PTC_EDIT:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        if ((uint16_t)g_sys.params.ptc_max_temp != last_ptc_max) {
                            last_ptc_max = (uint16_t)g_sys.params.ptc_max_temp;
                            if (!redraw) {
                                char buf[16];
                                sprintf(buf, "%d", g_sys.params.ptc_max_temp);
                                TFT_DrawString(30, 70, buf, COLOR_RED, UI_BG, 4);
                                draw_degree(90, 72, COLOR_RED, 2);
                            }
                        }
                        break;
                    case SCREEN_PTC_COOLING_EDIT:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        if ((uint16_t)g_sys.params.ptc_cooling_temp != last_ptc_cool) {
                            last_ptc_cool = (uint16_t)g_sys.params.ptc_cooling_temp;
                            if (!redraw) {
                                char buf[16];
                                sprintf(buf, "%d", g_sys.params.ptc_cooling_temp);
                                TFT_DrawString(30, 70, buf, COLOR_CYAN, UI_BG, 4);
                                draw_degree(90, 72, COLOR_CYAN, 2);
                            }
                        }
                        break;
                    case SCREEN_MOTOR_EDIT:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        break;
                    case SCREEN_WIFI:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        if (g_sys.wifi_enabled != last_wifi) { redraw = 1; last_wifi = g_sys.wifi_enabled; }
                        break;
                    case SCREEN_TEMP_PID:
                        if (g_sys.temp_pid_progress != last_pid_prog) { redraw = 1; last_pid_prog = g_sys.temp_pid_progress; }
                        break;
                    case SCREEN_PID_AUTOTUNE:
                        if (g_sys.pid_autotune_progress != last_auto_prog) { redraw = 1; last_auto_prog = g_sys.pid_autotune_progress; }
                        break;
                    case SCREEN_SAFETY_ALERT:
                        break;
                    case SCREEN_SETTINGS: {
                        static uint8_t last_ss_sel = 0xFF;
                        static uint8_t last_ss_so = 0xFF;
                        static uint8_t last_ss_bv = 0xFF;
                        static uint8_t last_ss_bl = 0xFF;
                        if (g_sys.scroll_offset != last_ss_so) {
                            last_ss_so = g_sys.scroll_offset;
                            UI_DrawSettingsScreen();
                        } else if (!redraw && g_sys.selected_item != last_ss_sel) {
                            last_ss_sel = g_sys.selected_item;
                            UI_DrawSettingsScreen();
                        }
                        last_ss_sel = g_sys.selected_item;
                        if (!redraw) {
                            if (g_sys.buzzer_vol != last_ss_bv || g_sys.backlight != last_ss_bl) {
                                last_ss_bv = g_sys.buzzer_vol;
                                last_ss_bl = g_sys.backlight;
                                UI_RefreshSettingsValue();
                            }
                        }
                        break;
                    }
                    default: break;
                }
                if (redraw) {
                    switch (g_sys.current_screen) {
                        case SCREEN_WEIGHT:        UI_DrawWeightScreen(); break;
                        case SCREEN_TEMP_ADJUST:   UI_DrawTempAdjust(); break;
                        case SCREEN_TEMP_EDIT:     UI_DrawTempEdit(); break;
                        case SCREEN_TIME_ADJUST:   UI_DrawTimeAdjust(); break;
                        case SCREEN_TIME_EDIT:     UI_DrawTimeEdit(); break;
                        case SCREEN_PTC_ADJUST:    UI_DrawPtcAdjust(); break;
                        case SCREEN_PTC_EDIT:      UI_DrawPtcEdit(); break;
                        case SCREEN_PTC_COOLING_EDIT: UI_DrawPtcCoolingEdit(); break;
                        case SCREEN_MOTOR_ADJUST:  UI_DrawMotorAdjust(); break;
                        case SCREEN_MOTOR_EDIT:    UI_DrawMotorEdit(); break;
                        case SCREEN_ABOUT:         UI_DrawAbout(); break;
                        case SCREEN_WIFI:          UI_DrawWiFiScreen(); break;
                        case SCREEN_TEMP_PID:      UI_DrawTempPid(); break;
                        case SCREEN_PID_AUTOTUNE:  UI_DrawPidAutotune(); break;
case SCREEN_SAFETY_ALERT:  UI_DrawSafetyAlert(); break;
                        case SCREEN_SETTINGS:       UI_DrawSettingsScreen(); break;
                        default: break;
                    }
                }
                break;
            }
        }
    }
}

void UI_UpdateMainDynamic(void)
{
    char buf[32];
    uint32_t h, m, s;
    uint16_t x, y, w, ch;
    static char last_val[4][32];
    static uint8_t last_state = 0xFF;

    /* 烘干状态变化时只重绘第5卡的状态文字；IDLE 时不显示文字 */
    if ((uint8_t)g_sys.run_state != last_state) {
        last_state = (uint8_t)g_sys.run_state;
        static const char *sstr[] = {"停止烘干","开始烘干","开始烘干","开始烘干","停止烘干","停止烘干"};
        static const uint16_t scol[] = {UI_TEXT_DIM, UI_ACCENT2, UI_WARN, UI_ACCENT, UI_ACCENT, UI_OK};
        uint16_t fill = (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME;
        const char *s = sstr[g_sys.run_state];
        TFT_FillRect(166, 103, 70, 16, fill);
        TFT_DrawStringZh(166, 103, s, scol[g_sys.run_state], fill);
    }

    /* 卡1 TEMP 117x42 @(2,2) */
    main_card_rect(0, &x, &y, &w, &ch);
    sprintf(buf, "%.1f", g_sys.current_temp);
    if (strcmp(buf, last_val[0]) != 0) {
        draw_value_unit(0, x, y, ch,
                        (g_sys.selected_item == 0) ? UI_CARD_HI : CARD_BG_TEMP,
                        buf, UI_WARN);
        strcpy(last_val[0], buf);
    }

    /* 卡2 湿度 117x42 @(121,2) */
    main_card_rect(1, &x, &y, &w, &ch);
    sprintf(buf, "%.1f%%", g_sys.current_humidity);
    if (strcmp(buf, last_val[1]) != 0) {
        draw_value_unit(1, x, y, ch,
                        (g_sys.selected_item == 1) ? UI_CARD_HI : CARD_BG_HUMI,
                        buf, UI_CYAN);
        strcpy(last_val[1], buf);
    }

    /* 卡3 WEIGHT 117x42 @(2,46) */
    main_card_rect(2, &x, &y, &w, &ch);
    sprintf(buf, "%.1f", g_sys.weight_g);
    if (strcmp(buf, last_val[2]) != 0) {
        draw_value_unit(2, x, y, ch,
                        (g_sys.selected_item == 2) ? UI_CARD_HI : CARD_BG_WEIGHT,
                        buf, UI_PURPLE);
        strcpy(last_val[2], buf);
    }

    /* 卡4 PTC 117x42 @(121,46) */
    main_card_rect(3, &x, &y, &w, &ch);
    sprintf(buf, "%.1f", g_sys.ptc_temp);
    if (strcmp(buf, last_val[3]) != 0) {
        draw_value_unit(3, x, y, ch,
                        (g_sys.selected_item == 3) ? UI_CARD_HI : CARD_BG_PTC,
                        buf, UI_ACCENT2);
        strcpy(last_val[3], buf);
    }

    /* 卡5 烘干时间 236x43 @(2,90)：时间数字仅变化时重绘 */
    main_card_rect(4, &x, &y, &w, &ch);
    h = g_sys.params.dry_time_sec / 3600;
    m = (g_sys.params.dry_time_sec % 3600) / 60;
    s = g_sys.params.dry_time_sec % 60;
    sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
    {
        static char last_time_str[16] = "";
        if (strcmp(buf, last_time_str) != 0) {
            strcpy(last_time_str, buf);
            TFT_DrawString((uint16_t)(x + (w - 8U * 12U) / 2U),
                           (uint16_t)(y + (ch - 14U) / 2U),
                           buf, UI_ACCENT,
                           (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME, 2);
        }
    }

    /* REM 倒计时：仅烘干中且值变化时重绘；退出烘干时清掉残留行 */
    {
        static char last_rem_str[16] = "";
        static uint8_t rem_was_drawing = 0;
        uint16_t rem_bg = (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME;
        if (g_sys.run_state == STATE_DRYING) {
            h = g_sys.remaining_sec / 3600;
            m = (g_sys.remaining_sec % 3600) / 60;
            s = g_sys.remaining_sec % 60;
            sprintf(buf, "剩余%02lu:%02lu:%02lu", h, m, s);
            if (strcmp(buf, last_rem_str) != 0) {
                strcpy(last_rem_str, buf);
                uint16_t rem_w = zh_str_width(buf);
                TFT_FillRect((uint16_t)(x + (w - rem_w) / 2U), (uint16_t)(y + 20), rem_w, 16U, rem_bg);
                TFT_DrawStringZh((uint16_t)(x + (w - rem_w) / 2U), (uint16_t)(y + 20), buf, UI_OK, rem_bg);
            }
            rem_was_drawing = 1;
        } else if (rem_was_drawing) {
            last_rem_str[0] = '\0';
            TFT_FillRect(x, (uint16_t)(y + 20), w, 16U, rem_bg);
            rem_was_drawing = 0;
        }
    }
}

static void Delay_ms(uint16_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 7200; i++);
}
#endif /* BOOTLOADER_BUILD */

