#ifndef BOOTLOADER_BUILD
#include "ui_manager.h"
#include "system_config.h"
#include "bsp_tft_st7789.h"
#include "bsp_aht20.h"
#include "bsp_ntc.h"
#include "bsp_fan.h"
#include "bsp_rgb_led.h"
#include "bsp_cs1237.h"
#include "bsp_stepper.h"
#include "system_time.h"
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
        CARD_BG_TEMP = TFT_COLOR(0xFF, 0xF3, 0xE0);
        CARD_BG_HUMI = TFT_COLOR(0xE1, 0xF5, 0xFE);
        CARD_BG_WEIGHT = TFT_COLOR(0xF3, 0xE5, 0xF5);
        CARD_BG_PTC  = TFT_COLOR(0xFF, 0xEB, 0xEE);
        CARD_BG_TIME = TFT_COLOR(0xE8, 0xF5, 0xE9);
    } else {  /* 暗色（柔和深灰蓝，避免近黑造成"黑底"观感） */
        UI_BG        = TFT_COLOR(0x3A, 0x3A, 0x4A);
        UI_CARD      = TFT_COLOR(0x46, 0x46, 0x58);
        UI_CARD_HI   = TFT_COLOR(0x52, 0x52, 0x66);
        CARD_BG_TEMP = TFT_COLOR(0x4E, 0x3D, 0x2A);
        CARD_BG_HUMI = TFT_COLOR(0x2A, 0x3D, 0x4E);
        CARD_BG_WEIGHT = TFT_COLOR(0x3D, 0x2A, 0x3D);
        CARD_BG_PTC  = TFT_COLOR(0x4E, 0x2A, 0x2A);
        CARD_BG_TIME = TFT_COLOR(0x2A, 0x4E, 0x2A);
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
        int32_t r2 = (int32_t)radius * (int32_t)radius * 4;   /* (2r)2 */
        for (i = 0; i < radius; i++) {
            int32_t dy = (int32_t)(2 * i + 1) - (int32_t)(2 * radius);
            int32_t maxdx = r2 - dy * dy;                     /* 本行 Δx2 上限 */
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
        int32_t r2  = (int32_t)radius * (int32_t)radius * 4;               /* (2r)2 */
        int32_t rin = (int32_t)(radius - th) * (int32_t)(radius - th) * 4; /* (2(r-th))2 */
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
static void __attribute__((unused)) draw_card_pulse(uint16_t x, uint16_t y, uint16_t w, uint8_t selected)
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
    /* 滑块位置：选中索引钳位到 [0, total-per_page]，滚动到底时滑块停在轨道末端 */
    uint16_t max_pos = (uint16_t)(total - per_page);
    uint16_t pos = (cursor_pos > max_pos) ? max_pos : cursor_pos;
    uint16_t range = (sh > thumb_h) ? (sh - thumb_h) : 0;
    uint16_t thumb_y = sy + (uint16_t)((uint32_t)range * pos / (max_pos ? max_pos : 1U));
    TFT_FillRect(234, sy, 5, sh, UI_CARD_EDGE);
    TFT_FillRect(234, thumb_y, 5, thumb_h, UI_ACCENT);
}

/* ── 像素滚动：可视区参数 ── */
#define SCR_ROW_H    18
#define SCR_VIEW_Y   36
#define SCR_VIEW_H   90
#define MENU_ROW_H2  20
#define MENU_VIEW_Y  28
#define MENU_VIEW_H  100

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
    TFT_DrawBitmap(x + 2, y + 3, 10, 24, icon_temp_bmp, 0xF81F, bg);
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
    TFT_DrawBitmap(x, y + 7, 18, 17, icon_weight_bmp, 0xF81F, bg);
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
    TFT_DrawBitmap(x, y + 5, 14, 20, icon_humi_bmp, 0xF81F, bg);
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
    TFT_DrawBitmap(x, y + 5, 15, 21, icon_ptc_bmp, 0xF81F, bg);
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
    TFT_DrawBitmap(x, y + 4, 22, 22, icon_clock_bmp, 0xF81F, bg);
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

/* 设置时间页白色衬底色（比 UI_CARD 略亮，与深色页面底区分）——整绘与局部刷新共用 */
#define TIME_CARD_BG TFT_COLOR(0xF8, 0xFA, 0xFF)

/* 局部重绘单个时间数字 + 光标框（框紧贴字模字形四面，无多余间隙）
 * 选中态只改光标轮廓颜色，不填充背景 */
static void refr_time_digit(uint8_t i, uint16_t txt_color, uint16_t border_color)
{
    char buf[8];
    uint16_t dx = time_digit_x[i];
    TFT_FillRect(dx - 2, 50, 26, 35, TIME_CARD_BG);
    sprintf(buf, "%d", g_sys.time_digits[i]);
    TFT_DrawString(dx, 52, buf, txt_color, TIME_CARD_BG, 4);
    if (g_sys.time_cursor == i) draw_frame(dx - 1, 51, 22, 32, border_color);
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

static void __attribute__((unused)) draw_card_bg(uint16_t y, uint16_t accent_color)
{
    TFT_FillRect(0, y, ACCENT_W, CARD_H, accent_color);
    TFT_FillRect(ACCENT_W, y, TFT_WIDTH - ACCENT_W, CARD_H, UI_BG);
}

/* 横屏卡片背景（可指定坐标/尺寸），sgl 风格：深色卡片 + 顶边高亮 */
static void __attribute__((unused)) draw_card_bg_at(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t accent_color)
{
    TFT_FillRect(x, y, w, h, UI_CARD);
    TFT_FillRect(x, y, w, 2, accent_color);        /* 顶部色条 */
    TFT_FillRect(x, y + h - 2, w, 2, accent_color); /* 底部色条 */
}

/* ── 开机进度条（横向药丸形，30,92 180x14，半径=半高7） ── */
#define PB_X   30U
#define PB_Y   92U
#define PB_W   180U
#define PB_H   14U

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

    /* 进度条（轨道 30,92 180x14 r7 药丸形） */
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
        if (fw > 0U && fw < 14U) fw = 14U;   /* 最小显示宽度=圆头直径，左端从第一帧起就是圆角 */
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

            /* 开机自动去皮 + 读取重量，屏幕亮起前卡片即有读数 */
            CS1237_Tare();
            {
                float w = CS1237_ReadWeight();
                if (w > -1000.0f) g_sys.weight_g = (int32_t)(w + ((w >= 0.0f) ? 0.5f : -0.5f));
            }

            /* 只检测 NTC 温度 > 冷却温度 → 开风扇 */
            if (g_sys.ptc_temp > (float)g_sys.params.ptc_cooling_temp) {
                Fan_SetSpeed(100);
                fan_cooling = 1;
            }
        }

        /* 60% 时重试温湿度（30% 若失败再试一次，确保 I2C 读条期间完成） */
        if (pct == 60) {
            float t, h;
            /* 开机去皮重试：芯片上电建立需若干转换周期，30% 若样本不足未成零位，这里再试一次 */
            {
                float w0 = CS1237_ReadWeight();
                if (w0 < -1000.0f || w0 > 6000.0f || w0 < -20.0f) {
                    CS1237_Tare();
                    w0 = CS1237_ReadWeight();
                }
                if (w0 > -1000.0f) g_sys.weight_g = (int32_t)(w0 + ((w0 >= 0.0f) ? 0.5f : -0.5f));
            }
            if (AHT20_Read(&t, &h) == 0) {
                g_sys.current_temp = t;
                g_sys.current_humidity = h;
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

/* 通用横屏标题栏：无背景色带，仅分隔线 + 居中标题 */
static void __attribute__((unused)) draw_page_title(const char *title, uint16_t accent)
{
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(title) * 12U)) / 2U,
                   6, title, accent, UI_BG, 2);
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
    /* 无背景色带：标题文字直接写在 UI_BG 上，仅保留底部 1px 分隔线 */
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    TFT_DrawStringZh((TFT_WIDTH - zh_str_width(title)) / 2U, 6, title, accent, UI_BG);
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

/* 重量显示整数值：int 变量，允许负数直接显示 */
static int32_t fmt_weight_g(void)
{
    return g_sys.weight_g;
}

/* 绘制数值+单位（size2 与数字同字号），紧跟数值后，供全屏与局部刷新共用。
 * 数值+单位组在卡片文本区(x+32..x+112)内水平居中；每帧先清整个区（含 ° 顶行），
 * 值变短时不留残影，单位/℃ 自动贴值动态跟随。 */
static void draw_value_unit(uint8_t item, uint16_t x, uint16_t y, uint16_t h,
                            uint16_t bg, const char *value, uint16_t val_color)
{
    uint16_t vw;
    uint16_t vx;
    uint16_t vy = (uint16_t)(y + (h - 14U) / 2U);
    uint16_t ux;
    uint16_t left, right, group_w;

    vw = (uint16_t)(strlen(value) * 12U);

    /* 组宽：数值 + 4px 间隙 + 单个单位字符(除湿度已含%外) */
    group_w = (item == 1U) ? vw : (uint16_t)(vw + 16U);
    left  = (uint16_t)(x + 32U);
    right = (uint16_t)(x + 112U);
    if (group_w >= (uint16_t)(right - left)) vx = left;
    else vx = (uint16_t)(left + ((uint16_t)(right - left) - group_w) / 2U);
    ux = (uint16_t)(vx + vw + 4U);

    /* 清屏区固定从 left 起（值变短重居中时旧值左侧也不会残留），向上含 ° 顶行 */
    TFT_FillRect(left, (uint16_t)(vy - 1), (uint16_t)(x + 114U - left), 17U, bg);

    TFT_DrawString(vx, vy, value, val_color, bg, 2);

    switch (item) {
    case 0:  /* TEMP: °C */
    case 3:  /* PTC: °C */
        TFT_DrawString(ux, vy, "C", val_color, bg, 2);
        draw_degree((uint16_t)(ux - 4), (uint16_t)(vy - 1), val_color, 1);
        break;
    case 2:  /* WEIGHT: g */
        TFT_DrawString(ux, vy, "g", val_color, bg, 2);
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

    /* 图标 30x30：距卡片左边缘 6px（略偏右），垂直居中 */
    icon_x = (uint8_t)(x + 6);
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
        uint16_t fill = (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME;
        const char *state_str[] = {"停止烘干", "开始烘干", "开始烘干", "开始烘干", "停止烘干", "停止烘干"};
        uint16_t state_color[] = {UI_TEXT_DIM, UI_ACCENT2, UI_WARN, UI_ACCENT, UI_ACCENT, UI_OK};
        fill_round_rect(x, y, w, h, fill, 10);
        draw_round_outline(x, y, w, h, (g_sys.selected_item == 4) ? UI_ACCENT : UI_CARD_EDGE, 10, 2);
        draw_icon_clock((uint16_t)(x + 6), (uint16_t)(y + (h - 30U) / 2U), fill);
        hh = g_sys.params.dry_time_sec / 3600;
        mm = (g_sys.params.dry_time_sec % 3600) / 60;
        ss = g_sys.params.dry_time_sec % 60;
        if (g_sys.run_state == STATE_DRYING || g_sys.run_state == STATE_COOLING) {
            /* 烘干中：主显示剩余倒计时（大字），上方不再放设定小字避免与状态文字重叠 */
            hh = g_sys.remaining_sec / 3600;
            mm = (g_sys.remaining_sec % 3600) / 60;
            ss = g_sys.remaining_sec % 60;
            sprintf(buf, "%02lu:%02lu:%02lu", hh, mm, ss);
            TFT_DrawString(x + (w - 8U * 12U) / 2U - 6U, y + (h - 14U) / 2U, buf, UI_OK, fill, 2);
        } else {
            sprintf(buf, "%02lu:%02lu:%02lu", hh, mm, ss);
            TFT_DrawString(x + (w - 8U * 12U) / 2U - 6U, y + (h - 14U) / 2U, buf, UI_ACCENT, fill, 2);
        }
        TFT_DrawStringZh(166, y + (h - 16U) / 2U, state_str[g_sys.run_state],
                         state_color[g_sys.run_state], fill);
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
        sprintf(buf, "%d", (int)fmt_weight_g());
        draw_main_card(2, x, y, w, h, CARD_BG_WEIGHT, buf, UI_PURPLE,
                       g_sys.selected_item == 2);
        break;
    default:
        sprintf(buf, "%d", (int)(g_sys.ptc_temp + 0.5f));
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
    sprintf(buf, "%d", (int)fmt_weight_g());
    draw_main_card(2, 2, 46, 117, 42, CARD_BG_WEIGHT, buf, UI_PURPLE,
                   g_sys.selected_item == 2);

    /* ── 卡4 PTC 117x42 @(121,46) ── */
    sprintf(buf, "%d", (int)(g_sys.ptc_temp + 0.5f));
    draw_main_card(3, 121, 46, 117, 42, CARD_BG_PTC, buf,
                   UI_ACCENT2, g_sys.selected_item == 3);

    /* ── 第5卡片：烘干时间 236x43 @(2,90) ── */
    cy = 90;
    fill_round_rect(2, cy, 236, 43,
                    (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME, 10);
    draw_round_outline(2, cy, 236, 43,
                       (g_sys.selected_item == 4) ? UI_ACCENT : UI_CARD_EDGE, 10, 2);
    draw_icon_clock(2 + 6, cy + (43U - 30U) / 2U,
                    (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME);
    h = g_sys.params.dry_time_sec / 3600;
    m = (g_sys.params.dry_time_sec % 3600) / 60;
    s = g_sys.params.dry_time_sec % 60;
    if (g_sys.run_state == STATE_DRYING || g_sys.run_state == STATE_COOLING) {
        h = g_sys.remaining_sec / 3600;
        m = (g_sys.remaining_sec % 3600) / 60;
        s = g_sys.remaining_sec % 60;
        sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
        TFT_DrawString(2 + (236U - 8U * 12U) / 2U - 6U, cy + (43U - 14U) / 2U, buf, UI_OK,
                       (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME, 2);
    } else {
        sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
        TFT_DrawString(2 + (236U - 8U * 12U) / 2U - 6U, cy + (43U - 14U) / 2U, buf, UI_ACCENT,
                       (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME, 2);
    }
    TFT_DrawStringZh(166, cy + (43U - 16U) / 2U, state_str[g_sys.run_state],
                     state_color[g_sys.run_state],
                     (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME);
}

void UI_DrawWeightScreen(void)
{
    char buf[32];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("重量", UI_ACCENT2);
    sprintf(buf, "当前: %dg", (int)fmt_weight_g());
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
    TFT_DrawStringZh(10, 60, "PID调整", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 78, "PID自动校准", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 96, "返回", UI_TEXT_DIM, UI_BG);
    if (g_sys.selected_item == 0) {
        TFT_FillRect(10, 42, 76, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 42, "设置温度", UI_TEXT, UI_ACCENT);
    }
    if (g_sys.selected_item == 1) {
        TFT_FillRect(10, 60, 88, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 60, "PID调整", UI_TEXT, UI_ACCENT);
    }
    if (g_sys.selected_item == 2) {
        TFT_FillRect(10, 78, 122, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 78, "PID自动校准", UI_TEXT, UI_ACCENT);
    }
    if (g_sys.selected_item == 3) {
        TFT_FillRect(10, 96, 44, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 96, "返回", UI_TEXT, UI_ACCENT);
    }
    sprintf(buf, "%d", g_sys.params.target_temp);
    draw_val_deg(42, buf, UI_WARN);
}

static void refresh_temp_adj_sel(uint8_t old_idx, uint8_t new_idx)
{
    static const char *labels[4] = {"设置温度", "PID调整", "PID自动校准", "返回"};
    static const uint8_t ys[4] = {42, 60, 78, 96};
    static const uint8_t fw[4] = {76, 88, 122, 44};
    uint8_t i;
    for (i = 0; i < 4; i++) {
        if (i != old_idx && i != new_idx) continue;
        TFT_FillRect(10, ys[i], fw[i], 16, UI_BG);
        if (i == new_idx) {
            TFT_FillRect(10, ys[i], fw[i], 16, UI_ACCENT);
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT, UI_ACCENT);
        } else {
            TFT_DrawStringZh(10, ys[i], labels[i], UI_TEXT_DIM, UI_BG);
        }
    }
}

static void draw_tune_live(void);   /* 校准页实时温度 AIR/PTC（定义在 1300 行附近） */

void UI_DrawTempPid(void)
{
    char buf[32];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("温度PID", UI_WARN);
    sprintf(buf, "温度 %d", g_sys.params.target_temp);
    TFT_DrawStringZh((TFT_WIDTH - zh_str_width(buf)) / 2U, 40, buf, COLOR_ORANGE, UI_BG);
    draw_degree(116, 40, COLOR_ORANGE, 2);
    if (g_sys.temp_pid_running) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("校准中，单击返回")) / 2U, 65, "校准中，单击返回", COLOR_YELLOW, UI_BG);
        TFT_FillRect(40, 95, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 107, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 95, 2, 14, COLOR_WHITE);
        TFT_FillRect(198, 95, 2, 14, COLOR_WHITE);
        uint8_t pct = g_sys.temp_pid_progress;
        TFT_FillRect(42, 97, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 115, buf, UI_TEXT, UI_BG, 2);
    } else {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("单击开始校准")) / 2U, 65, "单击开始校准", COLOR_YELLOW, UI_BG);
        sprintf(buf, "KP:%.2f KI:%.2f KD:%.2f", g_sys.params.pid_air_kp, g_sys.params.pid_air_ki, g_sys.params.pid_air_kd);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 95, buf, COLOR_CYAN, UI_BG, 2);
    }
    draw_tune_live();
    draw_btn(160, "  退出", UI_TEXT_DIM, g_sys.selected_item == 0);
    if (g_sys.selected_item == 0) draw_frame(5, 160, 125, BTN_H, UI_ACCENT);
}

void UI_DrawTimeAdjust(void)
{
    char buf[8];
    uint8_t i;
    const uint16_t card_bg = TIME_CARD_BG;   /* 比 UI_CARD 提亮一点，与页面深色底区分、局部刷新同色 */
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置时间", UI_ACCENT);
    fill_round_rect(8, 48, 224, 40, card_bg, 8);
    for (i = 0; i < 6; i++) {
        sprintf(buf, "%d", g_sys.time_digits[i]);
        TFT_DrawString(time_digit_x[i], 52, buf, COLOR_CYAN, card_bg, 4);
        if (g_sys.time_cursor == i) {
            /* 选中态只改光标轮廓颜色，不填充背景；未选中态用普通轮廓 */
            draw_frame(time_digit_x[i] - 1, 51, 22, 32, g_sys.time_edit_active ? COLOR_ORANGE : UI_ACCENT2);
        }
    }
    TFT_FillRect(78, 58, 8, 8, UI_ACCENT2);
    TFT_FillRect(78, 70, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 58, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 70, 8, 8, UI_ACCENT2);
    /* 底部居中显示当前预设名称：当前烘干预设为：XXX（标签用黄色，暗底清晰） */
    {
        static const char *lbl = "当前烘干预设为：";
        const char *nm = "PETG";
        uint16_t lw, x0;
        if (g_sys.params.current_preset < g_sys.params.preset_count) {
            const char *t = g_sys.params.presets[g_sys.params.current_preset].name;
            if (t[0]) nm = t;
        }
        lw = zh_str_width(lbl);
        x0 = (uint16_t)((TFT_WIDTH - (uint16_t)(lw + strlen(nm) * 12U)) / 2U);
        TFT_DrawStringZh(x0, 100, lbl, COLOR_YELLOW, UI_BG);
        TFT_DrawString((uint16_t)(x0 + lw), 102, nm, COLOR_GREEN, UI_BG, 2);
    }
}

void UI_DrawPtcAdjust(void)
{
    char buf[16];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("PTC设置", UI_ACCENT2);
    TFT_DrawStringZh(10, 42, "最高温度", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 60, "冷却温度", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 78, "PID调整", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 96, "PID自动校准", UI_TEXT_DIM, UI_BG);
    TFT_DrawStringZh(10, 114, "退出", UI_TEXT_DIM, UI_BG);
    if (g_sys.selected_item == 0) {
        TFT_FillRect(10, 42, 76, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 42, "最高温度", UI_TEXT, UI_ACCENT2);
    }
    if (g_sys.selected_item == 1) {
        TFT_FillRect(10, 60, 76, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 60, "冷却温度", UI_TEXT, UI_ACCENT2);
    }
    if (g_sys.selected_item == 2) {
        TFT_FillRect(10, 78, 88, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 78, "PID调整", UI_TEXT, UI_ACCENT2);
    }
    if (g_sys.selected_item == 3) {
        TFT_FillRect(10, 96, 122, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 96, "PID自动校准", UI_TEXT, UI_ACCENT2);
    }
    if (g_sys.selected_item == 4) {
        TFT_FillRect(10, 114, 44, 16, UI_ACCENT2);
        TFT_DrawStringZh(10, 114, "退出", UI_TEXT, UI_ACCENT2);
    }
    sprintf(buf, "%d", g_sys.params.ptc_max_temp);
    draw_val_deg(42, buf, COLOR_RED);
    sprintf(buf, "%d", g_sys.params.ptc_cooling_temp);
    draw_val_deg(60, buf, COLOR_CYAN);
}

static void refresh_ptc_adj_sel(uint8_t old_idx, uint8_t new_idx)
{
    static const char *labels[5] = {"最高温度", "冷却温度", "PID调整", "PID自动校准", "退出"};
    static const uint8_t ys[5] = {42, 60, 78, 96, 114};
    static const uint8_t fw[5] = {76, 76, 88, 122, 44};
    uint8_t i;
    for (i = 0; i < 5; i++) {
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
    draw_page_title_zh("PID自动校准", UI_ACCENT2);
    if (g_sys.pid_autotune_running) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("校准中，单击返回")) / 2U, 45, "校准中，单击返回", COLOR_YELLOW, UI_BG);
        TFT_FillRect(40, 80, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 92, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 80, 2, 14, COLOR_WHITE);
        TFT_FillRect(198, 80, 2, 14, COLOR_WHITE);
        uint8_t pct = g_sys.pid_autotune_progress;
        TFT_FillRect(42, 82, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 100, buf, UI_TEXT, UI_BG, 2);
    } else {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("单击开始校准")) / 2U, 45, "单击开始校准", COLOR_YELLOW, UI_BG);
        sprintf(buf, "KP:%.2f KI:%.2f KD:%.2f", g_sys.params.pid_ntc_kp, g_sys.params.pid_ntc_ki, g_sys.params.pid_ntc_kd);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 75, buf, COLOR_CYAN, UI_BG, 2);
    }
    draw_tune_live();
    draw_btn(160, "  退出", UI_TEXT_DIM, g_sys.selected_item == 0);
    if (g_sys.selected_item == 0) draw_frame(5, 160, 125, BTN_H, UI_ACCENT);
}

/* PID 调整页数据源：从 PTC 页进入(pid_return_screen=1)编辑元件 PID，从温度页进入编辑空气 PID。
 * 以前恒用 pid_air_*，PTC 校准写入 pid_ntc_* 后页面仍显示空气默认值，"看起来没写入"。 */
static float *pid_row_val(uint8_t row)
{
    if (g_sys.pid_return_screen) {
        return row == 0 ? &g_sys.params.pid_ntc_kp : row == 1 ? &g_sys.params.pid_ntc_ki : &g_sys.params.pid_ntc_kd;
    }
    return row == 0 ? &g_sys.params.pid_air_kp : row == 1 ? &g_sys.params.pid_air_ki : &g_sys.params.pid_air_kd;
}

/* PID 调整页：KP/KI/KD 三个值直接显示，单击选中后旋转直接改（步进0.1+加速），
 * 再单击退出该项并后台保存；第4项"返回"退回来源页。 */
void UI_DrawPidAdjust(void)
{
    char buf[24];
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("PID调整", COLOR_YELLOW);

    for (i = 0; i < 3; i++) {
        uint16_t y = (uint16_t)(42 + i * 18);
        if (i == 0) sprintf(buf, "KP:%.1f", *pid_row_val(0));
        else if (i == 1) sprintf(buf, "KI:%.1f", *pid_row_val(1));
        else sprintf(buf, "KD:%.1f", *pid_row_val(2));
        if (g_sys.selected_item == i || g_sys.pid_edit_active == (uint8_t)(i + 1)) {
            TFT_FillRect(10, y, 130, 16, UI_ACCENT);
            TFT_DrawStringZh(10, y, buf, UI_TEXT, UI_ACCENT);
        } else {
            TFT_DrawStringZh(10, y, buf, COLOR_CYAN, UI_BG);
        }
    }
    /* 返回 */
    if (g_sys.selected_item == 3) {
        TFT_FillRect(10, 96, 44, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 96, "返回", UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, 96, "返回", UI_TEXT_DIM, UI_BG);
    }
}

/* 预设编辑页指针 */
static Preset_t *ui_preset_ptr(void)
{
    return &g_sys.params.presets[g_sys.preset_edit_idx];
}

/* 确认弹窗（YES/NO 光标选择；是/否字模未提供前用 ASCII） */
static void draw_confirm_popup(const char *msg)
{
    uint16_t pw = 180, ph = 86;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = (TFT_HEIGHT - ph) / 2;
    fill_round_rect(px, py, pw, ph, UI_CARD, 8);
    draw_round_outline(px, py, pw, ph, UI_ACCENT, 8, 2);
    TFT_DrawStringZh((TFT_WIDTH - zh_str_width(msg)) / 2U, (uint16_t)(py + 14), msg, UI_TEXT, UI_CARD);
    if (g_sys.preset_confirm_yes) {
        fill_round_rect((uint16_t)(px + 20), (uint16_t)(py + 52), 64, 22, COLOR_GREEN, 7);
        TFT_DrawStringZh((uint16_t)(px + 20 + (64U - zh_str_width("是")) / 2U), (uint16_t)(py + 56), "是", UI_TEXT, COLOR_GREEN);
        TFT_DrawStringZh((uint16_t)(px + 96 + (64U - zh_str_width("否")) / 2U), (uint16_t)(py + 56), "否", UI_TEXT_DIM, UI_CARD);
    } else {
        TFT_DrawStringZh((uint16_t)(px + 20 + (64U - zh_str_width("是")) / 2U), (uint16_t)(py + 56), "是", UI_TEXT_DIM, UI_CARD);
        fill_round_rect((uint16_t)(px + 96), (uint16_t)(py + 52), 64, 22, UI_ACCENT, 7);
        TFT_DrawStringZh((uint16_t)(px + 96 + (64U - zh_str_width("否")) / 2U), (uint16_t)(py + 56), "否", UI_TEXT, UI_ACCENT);
    }
}

/* 预设列表单行绘制（二级列表页：预设行 + 末尾退出行） */
static void draw_preset_row(uint8_t i)
{
    uint16_t y = (uint16_t)(36 + ((int16_t)i - (int16_t)(g_sys.pixel_offset / 20)) * 20);
    if (y < 36 || y + 18 > 36 + 100) return;
    if (i < g_sys.params.preset_count) {
        char buf[24];
        const char *nm = g_sys.params.presets[i].name;
        if (nm[0] == 0) nm = "?";
        sprintf(buf, "%s%s", (i == g_sys.params.current_preset) ? "> " : "  ", nm);
        if (g_sys.selected_item == i) {
            fill_round_rect(8, y, 224, 18, UI_ACCENT, 7);
            TFT_DrawStringZh(18, (uint16_t)(y + 1), buf, UI_TEXT, UI_ACCENT);
        } else {
            TFT_FillRect(8, y, 224, 18, UI_BG);   /* 先清除旧高亮残留 */
            TFT_DrawStringZh(18, (uint16_t)(y + 1), buf, (i == g_sys.params.current_preset) ? COLOR_CYAN : UI_TEXT_DIM, UI_BG);
        }
    } else {   /* 退出行 */
        if (g_sys.selected_item == i) {
            fill_round_rect(8, y, 224, 18, UI_ACCENT, 7);
            TFT_DrawStringZh(18, (uint16_t)(y + 1), "退出", UI_TEXT, UI_ACCENT);
        } else {
            TFT_FillRect(8, y, 224, 18, UI_BG);
            TFT_DrawStringZh(18, (uint16_t)(y + 1), "退出", UI_TEXT_DIM, UI_BG);
        }
    }
}

/* 预设列表可视区整区重绘（滚动用，不整屏刷新） */
static void preset_redraw_viewport(void)
{
    uint8_t i, cnt, so;
    cnt = (uint8_t)(g_sys.params.preset_count + 1);
    so = (uint8_t)(g_sys.pixel_offset / 20);
    TFT_FillRect(0, 36, TFT_WIDTH, 100, UI_BG);
    for (i = so; i < cnt; i++) draw_preset_row(i);
    draw_scrollbar(cnt, 5, so, 36, 100);
}

/* 耗材预设主菜单页：编辑预设 / 新增预设 / 删除预设 / 退出 */
void UI_DrawPresetMenu(void)
{
    static const char *const kRows[4] = {"编辑预设", "新增预设", "删除预设", "退出"};
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("烘干预设", UI_ACCENT);
    for (i = 0; i < 4; i++) {
        uint16_t y = (uint16_t)(36 + i * 20);
        if (g_sys.selected_item == i) {
            fill_round_rect(8, y, 224, 18, UI_ACCENT, 7);
            TFT_DrawStringZh(18, (uint16_t)(y + 1), kRows[i], UI_TEXT, UI_ACCENT);
        } else {
            TFT_FillRect(8, y, 224, 18, UI_BG);
            TFT_DrawStringZh(18, (uint16_t)(y + 1), kRows[i], UI_TEXT_DIM, UI_BG);
        }
    }
}

/* 预设二级列表页（normal=编辑/长按切换；del_mode=删除） */
void UI_DrawPreset(void)
{
    TFT_FillScreen(UI_BG);
    if (g_sys.preset_del_mode) draw_page_title_zh("删除预设", UI_ACCENT2);
    else draw_page_title_zh("编辑预设", UI_ACCENT);
    preset_redraw_viewport();
    if (g_sys.preset_confirm == 1) draw_confirm_popup("确认删除该预设");
}

/* 预设编辑页（行：名称/温度/时间/退出；temp/time 用弹窗；名称仅新建可编辑） */
void UI_DrawPresetEdit(void)
{
    char buf[24];
    uint16_t y;
    Preset_t *p = ui_preset_ptr();
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("预设编辑", UI_ACCENT);
    y = 36;
    /* 行光标：选中的行整行高亮 */
    if (g_sys.preset_row == 0) fill_round_rect(10, y, 220, 18, UI_ACCENT, 4);
    /* NAME */
    TFT_DrawStringZh(10, y, "名称", (g_sys.preset_row == 0) ? UI_TEXT : UI_TEXT_DIM,
                     (g_sys.preset_row == 0) ? UI_ACCENT : UI_BG);
    if (g_sys.preset_row_edit == 1) {
        uint8_t c;
        for (c = 0; c < PRESET_NAME_MAX; c++) {
            char ch = p->name[c];
            if (ch == 0) ch = '_';
            if (c == g_sys.preset_name_cur) {
                fill_round_rect((uint16_t)(90 + c * 18), y, 18, 18, COLOR_ORANGE, 4);
                TFT_DrawStringZh(90 + c * 18, y + 1, &ch, UI_TEXT, COLOR_ORANGE);
            } else {
                TFT_DrawStringZh(90 + c * 18, y + 1, &ch, COLOR_CYAN, (g_sys.preset_row == 0) ? UI_ACCENT : UI_BG);
            }
        }
    } else {
        TFT_DrawStringZh(90, y + 1, (p->name[0]) ? p->name : "PLA",
                         (g_sys.preset_row == 0) ? UI_TEXT : UI_ACCENT,
                         (g_sys.preset_row == 0) ? UI_ACCENT : UI_BG);
        /* 名称为唯一身份，非新建时本行光标不可选，不再画"(内置)"多余标记 */
    }
    y += 22;
    /* TEMP */
    if (g_sys.preset_row == 1) fill_round_rect(10, y, 220, 18, UI_ACCENT, 4);
    TFT_DrawStringZh(10, y, "温度", (g_sys.preset_row == 1) ? UI_TEXT : UI_TEXT_DIM,
                     (g_sys.preset_row == 1) ? UI_ACCENT : UI_BG);
    sprintf(buf, "%d C", p->temp);
    TFT_DrawStringZh(90, y + 1, buf,
                     (g_sys.preset_row_edit == 2) ? COLOR_ORANGE : (g_sys.preset_row == 1) ? UI_TEXT : UI_ACCENT,
                     (g_sys.preset_row == 1) ? UI_ACCENT : UI_BG);
    y += 22;
    /* TIME */
    if (g_sys.preset_row == 2) fill_round_rect(10, y, 220, 18, UI_ACCENT, 4);
    TFT_DrawStringZh(10, y, "时间", (g_sys.preset_row == 2) ? UI_TEXT : UI_TEXT_DIM,
                     (g_sys.preset_row == 2) ? UI_ACCENT : UI_BG);
    {
        uint32_t hh = p->time_sec / 3600U, mm = p->time_sec % 3600U / 60U, ss = p->time_sec % 60U;
        sprintf(buf, "%02lu:%02lu:%02lu", hh, mm, ss);
        TFT_DrawStringZh(90, y + 1, buf,
                         (g_sys.preset_row_edit == 3) ? COLOR_ORANGE : (g_sys.preset_row == 2) ? UI_TEXT : UI_ACCENT,
                         (g_sys.preset_row == 2) ? UI_ACCENT : UI_BG);
    }
    y += 22;
    /* 退出：与参数行左对齐（X 一致）；仅在编辑页内作为第4行 */
    {
        uint16_t by = y;
        TFT_FillRect(10, by, 220, 18, (g_sys.preset_row == 3) ? UI_ACCENT : UI_BG);
        if (g_sys.preset_row == 3) {
            TFT_DrawStringZh(10, by, "退出", UI_TEXT, UI_ACCENT);
        } else {
            TFT_DrawStringZh(10, by, "退出", UI_TEXT_DIM, UI_BG);
        }
    }
    if (g_sys.preset_confirm == 2) draw_confirm_popup("确认保存");
}

/* 预设编辑页（行：NAME/TEMP/TIME/APPLY；temp/time 用弹窗；NAME 字符光标编辑） */
/* 预设 temp/time 弹窗 */
static void draw_preset_popup(uint8_t kind)
{
    uint16_t pw = 170, ph = 90;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = (TFT_HEIGHT - ph) / 2;
    Preset_t *p = ui_preset_ptr();
    char buf[16];
    fill_round_rect(px, py, pw, ph, UI_CARD, 8);
    draw_round_outline(px, py, pw, ph, UI_ACCENT, 8, 2);
    if (kind == 2) {   /* 温度 30-80 */
        uint16_t w = (uint16_t)((uint32_t)(pw - 30) * (p->temp - 30U) / 50U);
        TFT_FillRect((uint16_t)(px + 15), (uint16_t)(py + 30), (uint16_t)(pw - 30), 10, UI_CARD_EDGE);
        if (w) TFT_FillRect((uint16_t)(px + 15), (uint16_t)(py + 30), w, 10, UI_ACCENT);
        sprintf(buf, "%d C", p->temp);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, (uint16_t)(py + 56), buf, UI_TEXT, UI_CARD, 2);
    } else {           /* 时间 6 位：样式对齐烘干时间卡二级菜单（描边光标+居中数字+冒号） */
        uint8_t x;
        const uint16_t pop_card = TFT_COLOR(0xF7, 0xFA, 0xFF);   /* 微亮衬底，与页面深色底区分 */
        TFT_DrawString((TFT_WIDTH - 4U * 12U) / 2U, (uint16_t)(py + 9), "TIME", UI_TEXT, pop_card, 2);
        fill_round_rect((uint16_t)(px + 8), (uint16_t)(py + 29), (uint16_t)(pw - 16), 38, pop_card, 6);
        for (x = 0; x < 6; x++) {
            uint16_t dx = (uint16_t)(px + 19 + x * 21 + ((x >= 2) ? 6 : 0) + ((x >= 4) ? 6 : 0));
            uint16_t dy = (uint16_t)(py + 42);
            char ch[2] = {(char)('0' + g_sys.time_digits[x]), 0};
            uint16_t frame_col;
            /* 选中态只画轮廓光标居中包裹数字；编辑态换橙色轮廓 */
            if (g_sys.preset_time_cur != x) frame_col = 0xFFFF;
            else frame_col = g_sys.preset_time_edit ? COLOR_ORANGE : UI_ACCENT2;
            TFT_DrawString(dx, dy, ch, COLOR_CYAN, pop_card, 3);
            if (frame_col != 0xFFFF) draw_frame((uint16_t)(dx - 2), (uint16_t)(dy - 2), 20, 24, frame_col);
        }
        /* 冒号（时:分:秒 两组）：两段方块，垂直居中对齐数字 */
        {
            uint16_t cy = (uint16_t)(py + 42);
            uint16_t cx1 = (uint16_t)(px + 59), cx2 = (uint16_t)(px + 106);
            TFT_FillRect(cx1, (uint16_t)(cy + 4), 6, 5, UI_ACCENT2);
            TFT_FillRect(cx1, (uint16_t)(cy + 12), 6, 5, UI_ACCENT2);
            TFT_FillRect(cx2, (uint16_t)(cy + 4), 6, 5, UI_ACCENT2);
            TFT_FillRect(cx2, (uint16_t)(cy + 12), 6, 5, UI_ACCENT2);
        }
    }
}

/* PID 校准页右上角实时温度：AIR/PTC（与桌面卡片同源：current_temp/ptc_temp），
 * 0.1℃ 变化才局部重绘，避免整屏闪烁。 */
static void draw_tune_live(void)
{
    char buf[24];
    TFT_FillRect(138, 30, 102, 34, UI_BG);
    sprintf(buf, "AIR %.1f", g_sys.current_temp);
    TFT_DrawString(140, 32, buf, COLOR_CYAN, UI_BG, 1);
    sprintf(buf, "PTC %.1f", g_sys.ptc_temp);
    TFT_DrawString(140, 44, buf, COLOR_GREEN, UI_BG, 1);
}

/* PID 校准页进度条局部刷新（which 0=空气tempPID 1=PTC元件PID），避免整屏闪烁 */
static void refresh_autotune_bar(uint8_t which, uint8_t pct)
{
    uint16_t bx = 42, by = (which == 0) ? 97 : 82;
    char buf[8];
    TFT_FillRect(bx, by, 156, 10, UI_BG);
    if (pct) TFT_FillRect(bx, by, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
    sprintf(buf, "%d%%", pct);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U,
                   (which == 0) ? 115 : 100, buf, UI_TEXT, UI_BG, 2);
}

/* PID 调整页单行刷新：编辑中实时更新对应行 */
static void refresh_pid_row(uint8_t row)
{
    char buf[24];
    uint16_t y;
    if (row >= 3) return;
    y = (uint16_t)(42 + row * 18);
    if (row == 0) sprintf(buf, "KP:%.1f", *pid_row_val(0));
    else if (row == 1) sprintf(buf, "KI:%.1f", *pid_row_val(1));
    else sprintf(buf, "KD:%.1f", *pid_row_val(2));
    TFT_FillRect(10, y, 130, 16, UI_BG);
    if (g_sys.pid_edit_active == (uint8_t)(row + 1)) {
        TFT_FillRect(10, y, 130, 16, UI_ACCENT);
        TFT_DrawStringZh(10, y, buf, UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, y, buf, COLOR_CYAN, UI_BG);
    }
}

static const char *const kMenuItems[] = {"WiFi", "电机", "关于", "设置", "重启", "恢复出厂设置", "退出"};
#define MENU_ITEM_COUNT   7u
#define MENU_ROW_H        18u
#define MENU_ROW_GAP      20u
#define MENU_TOP_Y        32u

/* 绘制单个菜单行：选中=蓝底白字+圆角轮廓，未选中=灰字。
 * 位置由 pixel_offset 决定，供可视区绘制调用。 */
static void draw_menu_row(uint8_t i, uint8_t selected)
{
    int16_t y = (int16_t)(MENU_VIEW_Y - g_sys.pixel_offset + i * MENU_ROW_H2);
    if (y < (int16_t)MENU_VIEW_Y || y + MENU_ROW_H > (int16_t)(MENU_VIEW_Y + MENU_VIEW_H)) return;
    if (selected) {
        fill_round_rect(8, (uint16_t)y, 224, MENU_ROW_H, UI_ACCENT, 7);
        TFT_DrawStringZh(18, (uint16_t)(y + 1), kMenuItems[i], UI_TEXT, UI_ACCENT);
    } else {
        TFT_FillRect(8, (uint16_t)y, 224, MENU_ROW_H, UI_BG);
        TFT_DrawStringZh(18, (uint16_t)(y + 1), kMenuItems[i], UI_TEXT_DIM, UI_BG);
    }
}

/* 菜单可视区整区重绘（滚动用，不整屏刷新） */
static void menu_redraw_viewport(void)
{
    uint8_t i;
    TFT_FillRect(0, MENU_VIEW_Y, 233, MENU_VIEW_H, UI_BG);
    for (i = 0; i < MENU_ITEM_COUNT; i++) draw_menu_row(i, i == g_sys.selected_item);
    draw_scrollbar(MENU_ITEM_COUNT, 5, (uint8_t)(g_sys.pixel_offset / MENU_ROW_H2), MENU_VIEW_Y, MENU_VIEW_H);
}

/* 菜单编码器滚动：更新选中索引(循环)与 pixel_offset，重绘可视区 */
void UI_MenuScroll(int dir)
{
    int16_t target, max_off;
    if (dir > 0) g_sys.selected_item = (uint8_t)((g_sys.selected_item + 1) % MENU_ITEM_COUNT);
    else g_sys.selected_item = (g_sys.selected_item == 0) ? (uint8_t)(MENU_ITEM_COUNT - 1) : (uint8_t)(g_sys.selected_item - 1);
    target = (int16_t)g_sys.selected_item * MENU_ROW_H2;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + MENU_ROW_H2 > g_sys.pixel_offset + MENU_VIEW_H) g_sys.pixel_offset = (int16_t)(target + MENU_ROW_H2 - MENU_VIEW_H);
    max_off = (int16_t)(MENU_ITEM_COUNT * MENU_ROW_H2 - MENU_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    menu_redraw_viewport();
}

void UI_DrawMenu(void)
{
    uint8_t i;
    TFT_FillScreen(UI_BG);
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    TFT_DrawString((TFT_WIDTH - 5 * 12U) / 2U, 6, "MENU", UI_ACCENT, UI_BG, 2);
    for (i = 0; i < MENU_ITEM_COUNT; i++) draw_menu_row(i, i == g_sys.selected_item);
    draw_scrollbar(MENU_ITEM_COUNT, 5, (uint8_t)(g_sys.pixel_offset / MENU_ROW_H2), MENU_VIEW_Y, MENU_VIEW_H);
}

/* 开关滑块按钮：46x16 圆角药丸，与行高16对齐。开=整体填充色+白色圆钮靠右；关=灰底+白色圆钮靠左 */
static void draw_toggle(uint16_t x, uint16_t y, uint8_t on)
{
    uint16_t w = 42, h = 16, k = 12;
    fill_round_rect(x, y, w, h, on ? COLOR_GREEN : UI_CARD_EDGE, 8);
    if (on) fill_round_rect((uint16_t)(x + w - k - 2), (uint16_t)(y + 2), k, (uint16_t)(h - 4), COLOR_WHITE, 6);
    else    fill_round_rect((uint16_t)(x + 2), (uint16_t)(y + 2), k, (uint16_t)(h - 4), COLOR_WHITE, 6);
}

/* 开关滑动动画版：anim=0 静止；1~6 帧从关→开滑行；7~12 帧从开→关滑行。
 * 滑块位置按帧线性插值，视觉为滑块平滑滑动（约 6×30ms≈180ms 完成）。 */
static void draw_toggle_animated(uint16_t x, uint16_t y, uint8_t on, uint8_t anim)
{
    uint16_t w = 42, h = 16, k = 12;
    uint16_t knob_x;
    fill_round_rect(x, y, w, h, on ? COLOR_GREEN : UI_CARD_EDGE, 8);
    if (anim == 0) {
        knob_x = on ? (uint16_t)(x + w - k - 2) : (uint16_t)(x + 2);
    } else {
        uint8_t fr = anim;   /* 1..12 */
        uint8_t off_dir = (fr > 6) ? 1 : 0;          /* 1-6: 关→开；7-12: 开→关 */
        uint8_t f = (uint8_t)(off_dir ? (fr - 6) : fr);
        uint16_t x0 = off_dir ? (uint16_t)(x + w - k - 2) : (uint16_t)(x + 2);
        uint16_t x1 = off_dir ? (uint16_t)(x + 2) : (uint16_t)(x + w - k - 2);
        uint16_t span = (uint16_t)(w - k - 4);
        knob_x = (uint16_t)(x0 + ((uint32_t)(x1 - x0) * f) / 6U);
        (void)span;
    }
    fill_round_rect(knob_x, (uint16_t)(y + 2), k, (uint16_t)(h - 4), COLOR_WHITE, 6);
}

/* 电机页行文本（全宽重建 → 速度rpm长度自适应，编辑时无残留；开/关项含状态供变更检测） */
static void motor_row_str(uint8_t i, char *buf)
{
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;
    const char *drv;
    uint8_t slot = i;
    switch (g_sys.params.motor_driver) {
    case MOTOR_DRIVER_TMC2208: drv = "TMC2208"; break;
    case MOTOR_DRIVER_TMC2209: drv = "TMC2209"; break;
    default: drv = "A4988"; break;
    }
    if (i >= count - 1) { sprintf(buf, "退出"); return; }
    if (!is_tmc && slot >= 6) slot += 1;   /* A4988 无电流项 */
    switch (slot) {
    case 0: sprintf(buf, "联动:%s", g_sys.params.motor_enabled ? "开" : "关"); break;
    case 1: sprintf(buf, "方向:%s", g_sys.params.motor_direction ? "反转" : "正转"); break;
    case 2: sprintf(buf, "速度:%drpm", g_sys.params.motor_speed); break;
    case 3: sprintf(buf, "摆动:%s", g_sys.params.motor_oscillate ? "开" : "关"); break;
    case 4: sprintf(buf, "角度:%d°", g_sys.params.motor_oscillate_angle); break;
    case 5: sprintf(buf, "驱动:%s", drv); break;
    case 6: sprintf(buf, "电流:%.1fA", (float)g_sys.params.motor_current / 10.0f); break;
    case 7: sprintf(buf, "次数:%d", g_sys.params.motor_work_count); break;
    case 8: sprintf(buf, "休息:%ds", g_sys.params.motor_rest_sec); break;
    case 9: sprintf(buf, "静音:%s", g_sys.params.motor_stealthchop ? "开" : "关"); break;
    default: buf[0] = 0; break;
    }
}

/* 电机行是否为开关项（联动/摆动/静音） */
static uint8_t motor_slot_toggle(uint8_t slot)
{
    return (slot == 0 || slot == 3 || slot == 9);
}

static uint8_t motor_slot_toggle_val(uint8_t slot)
{
    switch (slot) {
    case 0: return g_sys.params.motor_enabled;
    case 3: return g_sys.params.motor_oscillate;
    case 9: return g_sys.params.motor_stealthchop;
    default: return 0;
    }
}

/* 电机行绘制：开/关项画滑块，其余画文本 */
static void motor_draw_row(uint8_t i, uint16_t y, uint8_t selected)
{
    char buf[32];
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;
    uint8_t slot = i;
    if (!is_tmc && slot >= 6) slot += 1;
    motor_row_str(i, buf);
    if (selected) TFT_FillRect(5, y, 220, 16, UI_ACCENT);
    else TFT_FillRect(5, y, 220, 16, UI_BG);
    if (i >= count - 1) {
        /* 退出行：按文本绘制 */
        TFT_DrawStringZh(5, y, buf, selected ? UI_TEXT : UI_TEXT_DIM, selected ? UI_ACCENT : UI_BG);
    } else if (motor_slot_toggle(slot)) {
        TFT_DrawStringZh(5, y, (slot == 9) ? "静音" : (slot == 3) ? "摆动" : "联动",
                         selected ? UI_TEXT : UI_TEXT_DIM, selected ? UI_ACCENT : UI_BG);
        if (slot == 9 && g_sys.mute_anim != 0) {
            draw_toggle_animated(168, y, motor_slot_toggle_val(slot), g_sys.mute_anim);
        } else {
            draw_toggle(168, y, motor_slot_toggle_val(slot));
        }
    } else {
        TFT_DrawStringZh(5, y, buf, selected ? UI_TEXT : UI_TEXT_DIM, selected ? UI_ACCENT : UI_BG);
    }
}

/* 电机页单行局部刷新：清全行再绘（开关滑块/数值都实时更新） */
static void refresh_motor_row(uint8_t i)
{
    uint16_t y = (uint16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
    motor_draw_row(i, y, (g_sys.selected_item == i));
}

/* 电机页可视区整区重绘（滚动用，不整屏刷新） */
static void motor_redraw_viewport(void)
{
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;
    TFT_FillRect(0, SCR_VIEW_Y, 233, SCR_VIEW_H, UI_BG);
    for (uint8_t i = 0; i < count; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        motor_draw_row(i, (uint16_t)y, (g_sys.selected_item == i));
    }
    draw_scrollbar(count, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);
}

/* 电机页编码器滚动：更新选中索引(循环)与 pixel_offset，重绘可视区 */
void UI_MotorScroll(int dir)
{
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;
    int16_t target, max_off;
    if (dir > 0) g_sys.selected_item = (uint8_t)((g_sys.selected_item + 1) % count);
    else g_sys.selected_item = (g_sys.selected_item == 0) ? (uint8_t)(count - 1) : (uint8_t)(g_sys.selected_item - 1);
    target = (int16_t)g_sys.selected_item * SCR_ROW_H;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + SCR_ROW_H > g_sys.pixel_offset + SCR_VIEW_H) g_sys.pixel_offset = (int16_t)(target + SCR_ROW_H - SCR_VIEW_H);
    max_off = (int16_t)(count * SCR_ROW_H - SCR_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    motor_redraw_viewport();
}

void UI_DrawMotorAdjust(void)
{
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;

    TFT_FillScreen(UI_BG);
    draw_page_title_zh("电机设置", UI_ACCENT);

    /* 可视区：按 pixel_offset 绘制全部项并裁剪到视区 */
    for (uint8_t i = 0; i < count; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        motor_draw_row(i, (uint16_t)y, (g_sys.selected_item == i));
    }
    draw_scrollbar(count, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);

    /* TMC 驱动：底部通讯状态诊断行（scale2+背景框，暗色下清晰可见） */
    {
        uint16_t box_y = 208;
        if (is_tmc) {
            uint8_t ok = Stepper_TmcComOk();
            uint16_t fg = ok ? COLOR_GREEN : COLOR_RED;
            fill_round_rect(5, box_y, 230, 22, UI_CARD, 6);
            TFT_DrawString(12, box_y + 3, ok ? "TMC-COM:OK" : "TMC-COM:FAIL", fg, UI_CARD, 2);
        } else {
            TFT_FillRect(5, box_y, 230, 22, UI_BG);
        }
    }
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
        TFT_FillRect(10, 60, 100, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 60, "WiFi", UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, 60, "WiFi", g_sys.wifi_enabled ? COLOR_GREEN : UI_TEXT_DIM, UI_BG);
    }
    draw_toggle(122, 60, g_sys.wifi_enabled);
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
    uint16_t tx;
    TFT_FillScreen(COLOR_RED);
    tx = (uint16_t)((TFT_WIDTH - zh_str_width("安全警报")) / 2U);
    TFT_DrawStringZh(tx, 30, "安全警报", COLOR_WHITE, COLOR_RED);

    if (g_sys.safety_state == SAFETY_BOX_BROKEN) {
        tx = (uint16_t)((TFT_WIDTH - zh_str_width("箱体破损")) / 2U);
        TFT_DrawStringZh(tx, 70, "箱体破损", COLOR_WHITE, COLOR_RED);
        tx = (uint16_t)((TFT_WIDTH - zh_str_width("烘干停止")) / 2U);
        TFT_DrawStringZh(tx, 100, "烘干停止", COLOR_WHITE, COLOR_RED);
    } else if (g_sys.safety_state == SAFETY_LID_OPEN) {
        tx = (uint16_t)((TFT_WIDTH - zh_str_width("上盖打开")) / 2U);
        TFT_DrawStringZh(tx, 70, "上盖打开", COLOR_WHITE, COLOR_RED);
        tx = (uint16_t)((TFT_WIDTH - zh_str_width("烘干停止")) / 2U);
        TFT_DrawStringZh(tx, 100, "烘干停止", COLOR_WHITE, COLOR_RED);
    }

    draw_btn(160, "  确认", COLOR_GREEN, 1);
    draw_frame(5, 160, 125, BTN_H, UI_ACCENT);
}

/* 前置声明：设置页数值文本生成，供整屏绘制与单行实时刷新共用 */
static void settings_row_str(uint8_t i, char *buf);

/* 设置页单行绘制（标签+高亮+值/开关滑块） */
static void settings_draw_row(uint8_t i, uint16_t y)
{
    static const char *kLabels[] = {"蜂鸣器联动","蜂鸣器音量","灯光开关","背光","主题","熄屏","RGB亮度","退出"};
    char buf[32];
    TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT_DIM, UI_BG);
    if (i == g_sys.selected_item) {
        TFT_FillRect(10, y, 95, 16, UI_ACCENT);
        TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT, UI_ACCENT);
    }
    if (i < 6) {
        uint16_t vcol = (g_sys.settings_edit_active && i == g_sys.selected_item) ? COLOR_ORANGE : UI_ACCENT;
        if (i == 0 || i == 2) {
            uint8_t on = (i == 0) ? g_sys.buzzer_link : g_sys.light_switch;
            TFT_FillRect(132, y, 95, 16, UI_BG);
            draw_toggle(134, y, on);
        } else {
            settings_row_str(i, buf);
            TFT_FillRect(132, y, 95, 16, UI_BG);
            TFT_DrawStringZh(132, y, buf, vcol, UI_BG);
        }
    }
}

/* RGB 亮度弹窗：指示灯/灯条 双进度条 + 完成；sel=0指示灯 1灯条 2完成
 * 光标=绿框选中行；选中后单击进入数值编辑，该行值区域橙底高亮、轮廓变橙。 */
#define RGB_POP_W    190
#define RGB_POP_H    126
#define RGB_BAR_X    16     /* 弹窗内进度条X(px+16) */
#define RGB_BAR_W    100
#define RGB_VAL_X    123    /* 数值X(px+82) */
#define RGB_ROW_H    34

static void draw_rgb_bar(uint8_t item);   /* 前置声明 */

static uint16_t rgb_pop_px(void) { return (uint16_t)((TFT_WIDTH - RGB_POP_W) / 2); }
static uint16_t rgb_pop_py(void) { return (uint16_t)((TFT_HEIGHT - RGB_POP_H) / 2); }

static void draw_rgb_bright_popup(void)
{
    uint16_t pw = RGB_POP_W, ph = RGB_POP_H;
    uint16_t px = rgb_pop_px();
    uint16_t py = rgb_pop_py();
    uint8_t sel = g_sys.rgb_bright_sel;
    uint8_t item;
    fill_round_rect(px, py, pw, ph, UI_CARD, 8);
    draw_round_outline(px, py, pw, ph, UI_ACCENT, 8, 2);
    for (item = 0; item < 2; item++) {
        uint16_t ry = (uint16_t)(py + (item == 0 ? 8 : 48));
        uint8_t selected = (sel == item);
        uint8_t editing  = (selected && g_sys.rgb_bright_edit);
        uint16_t cc = editing ? COLOR_ORANGE : COLOR_GREEN;
        TFT_FillRect((uint16_t)(px + 8), ry, (uint16_t)(pw - 16), RGB_ROW_H, UI_CARD);
        if (selected) draw_round_outline((uint16_t)(px + 8), ry, (uint16_t)(pw - 16), RGB_ROW_H, cc, 6, 2);
        TFT_DrawStringZh((uint16_t)(px + 16), (uint16_t)(py + (item == 0 ? 12 : 52)),
                         (item == 0) ? "指示灯" : "灯条",
                         selected ? cc : UI_TEXT, UI_CARD);
        draw_rgb_bar(item);
    }
    /* 完成 */
    if (sel == 2) {
        fill_round_rect((uint16_t)(px + 30), (uint16_t)(py + 96), 130, 22, COLOR_GREEN, 8);
        TFT_DrawStringZh((uint16_t)(px + 30 + (130U - zh_str_width("完成")) / 2U), (uint16_t)(py + 100), "完成", UI_TEXT, COLOR_GREEN);
    } else {
        TFT_DrawStringZh((uint16_t)(px + 30 + (130U - zh_str_width("完成")) / 2U), (uint16_t)(py + 100), "完成", UI_TEXT_DIM, UI_CARD);
    }
}

/* 仅重绘某一行(0=指示灯 1=灯条)的进度条（值变化灰度，不影响光标框/标签） */
static void draw_rgb_bar(uint8_t item)
{
    uint16_t px = rgb_pop_px();
    uint16_t py = rgb_pop_py();
    uint16_t by = (item == 0) ? (uint16_t)(py + 30) : (uint16_t)(py + 70);
    uint16_t bw = RGB_BAR_W;
    uint8_t val = (item == 0) ? g_sys.params.rgb_led_bright : g_sys.params.rgb_strip_bright;
    uint8_t selected = (g_sys.rgb_bright_sel == item);
    uint8_t editing  = (selected && g_sys.rgb_bright_edit);
    uint16_t cc = editing ? COLOR_ORANGE : COLOR_GREEN;
    char buf[8];
    TFT_FillRect((uint16_t)(px + RGB_BAR_X), by, bw, 8, UI_CARD_EDGE);
    {
        uint16_t w = (uint16_t)((uint32_t)bw * val / 100U);
        if (w) TFT_FillRect((uint16_t)(px + RGB_BAR_X), by, w, 8, selected ? cc : UI_ACCENT);
    }
    sprintf(buf, "%d%%", val);
    if (editing) {
        fill_round_rect((uint16_t)(px + 120), (uint16_t)(by - 3), 52, 16, COLOR_ORANGE, 4);
        TFT_DrawString((uint16_t)(px + 124), (uint16_t)(by - 3), buf, UI_TEXT, COLOR_ORANGE, 2);
    } else {
        TFT_DrawString((uint16_t)(px + 124), (uint16_t)(by - 3), buf, selected ? cc : UI_TEXT, UI_CARD, 2);
    }
}

void UI_DrawSettingsScreen(void)
{
    uint8_t i;
    uint8_t cnt = 8;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置", UI_ACCENT);
    for (i = 0; i < cnt; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        settings_draw_row(i, (uint16_t)y);
    }
    draw_scrollbar(cnt, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);
}

/* 设置页可视区整区重绘（滚动用） */
static void settings_redraw_viewport(void)
{
    uint8_t cnt = 8;
    TFT_FillRect(0, SCR_VIEW_Y, 233, SCR_VIEW_H, UI_BG);
    for (uint8_t i = 0; i < cnt; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        settings_draw_row(i, (uint16_t)y);
    }
    draw_scrollbar(cnt, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);
}

/* 设置页编码器滚动：更新选中索引(循环)与 pixel_offset，重绘可视区 */
void UI_SettingsScroll(int dir)
{
    uint8_t cnt = 8;
    int16_t target, max_off;
    if (dir > 0) g_sys.selected_item = (uint8_t)((g_sys.selected_item + 1) % cnt);
    else g_sys.selected_item = (g_sys.selected_item == 0) ? (uint8_t)(cnt - 1) : (uint8_t)(g_sys.selected_item - 1);
    target = (int16_t)g_sys.selected_item * SCR_ROW_H;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + SCR_ROW_H > g_sys.pixel_offset + SCR_VIEW_H) g_sys.pixel_offset = (int16_t)(target + SCR_ROW_H - SCR_VIEW_H);
    max_off = (int16_t)(cnt * SCR_ROW_H - SCR_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    settings_redraw_viewport();
}

/* 蜂鸣器音量/屏幕亮度 编辑弹窗：进度条在上、当前值在下（覆盖于当前菜单之上，非整屏） */
static void draw_settings_popup(uint8_t item)
{
    uint16_t pw = 170, ph = 90;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = (TFT_HEIGHT - ph) / 2;
    uint16_t m = (item == 1) ? 10U : 100U;
    uint16_t v = (item == 1) ? g_sys.buzzer_vol : g_sys.backlight;
    char buf[16];
    fill_round_rect(px, py, pw, ph, UI_CARD, 8);
    draw_round_outline(px, py, pw, ph, UI_ACCENT, 8, 2);
    TFT_FillRect((uint16_t)(px + 15), (uint16_t)(py + 30), (uint16_t)(pw - 30), 10, UI_CARD_EDGE);
    {
        uint16_t w = (uint16_t)((uint32_t)(pw - 30) * v / m);
        if (w) TFT_FillRect((uint16_t)(px + 15), (uint16_t)(py + 30), w, 10, UI_ACCENT);
    }
    if (item == 1) sprintf(buf, "%d%%", v * 10U);
    else sprintf(buf, "%d%%", v);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, (uint16_t)(py + 56), buf, UI_TEXT, UI_CARD, 2);
}

/* 设置页数值文本（全宽重建，实时刷新无残影） */
static void settings_row_str(uint8_t i, char *buf)
{
    static const char *off_labels[] = {"从不","1s","5s","10s","20s","30s","60s","120s","300s"};
    switch (i) {
    case 0: sprintf(buf, "%s", g_sys.buzzer_link ? "开" : "关"); break;
    case 1: sprintf(buf, "%d%%", (uint16_t)g_sys.buzzer_vol * 10U); break;
    case 2: sprintf(buf, "%s", g_sys.light_switch ? "开" : "关"); break;
    case 3: sprintf(buf, "%d%%", g_sys.backlight); break;
    case 4: sprintf(buf, "%s", g_sys.theme ? "暗色" : "亮色"); break;
    case 5: sprintf(buf, "%s", (g_sys.screen_off_timeout < 9U) ? off_labels[g_sys.screen_off_timeout] : "从不"); break;
    default: buf[0] = 0; break;
    }
}

/* 设置页单行值区刷新：编辑中实时更新（开关滑块/数值/弹窗并存） */
static void refresh_settings_row(uint8_t i)
{
    char buf[32];
    uint16_t y;
    if (i >= 6) return;
    y = (uint16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
    uint16_t vcol = (g_sys.settings_edit_active && i == g_sys.selected_item) ? COLOR_ORANGE : UI_ACCENT;
    if (i == 0 || i == 2) {
        uint8_t on = (i == 0) ? g_sys.buzzer_link : g_sys.light_switch;
        TFT_FillRect(132, y, 95, 16, UI_BG);
        draw_toggle(134, y, on);
        return;
    }
    settings_row_str(i, buf);
    TFT_FillRect(132, y, 95, 16, UI_BG);
    TFT_DrawStringZh(132, y, buf, vcol, UI_BG);
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
        g_sys.pixel_offset = 0;
        last_screen = g_sys.current_screen;
        /* 返回菜单：恢复进入子页前的选中项与滚动位置 */
        if (g_sys.current_screen == SCREEN_MENU) {
            g_sys.selected_item = g_sys.menu_selected;
            g_sys.pixel_offset = g_sys.menu_pixel_offset;
        }
        switch (g_sys.current_screen) {
            case SCREEN_MAIN:
                if (g_sys.selected_item > 4) g_sys.selected_item = 0;   /* 菜单泄漏索引收敛 */
                UI_DrawMainScreen();
                break;
            case SCREEN_WEIGHT:        UI_DrawWeightScreen(); break;
            case SCREEN_TEMP_ADJUST:   UI_DrawTempAdjust(); break;
            case SCREEN_TEMP_PID:      UI_DrawTempPid(); break;
            case SCREEN_TIME_ADJUST: {
                /* 进入时间页：从 dry_time_sec 生成六位数字 */
                uint32_t h = g_sys.params.dry_time_sec / 3600;
                uint32_t m = (g_sys.params.dry_time_sec % 3600) / 60;
                uint32_t s = g_sys.params.dry_time_sec % 60;
                g_sys.time_digits[0] = (uint8_t)(h / 10);
                g_sys.time_digits[1] = (uint8_t)(h % 10);
                g_sys.time_digits[2] = (uint8_t)(m / 10);
                g_sys.time_digits[3] = (uint8_t)(m % 10);
                g_sys.time_digits[4] = (uint8_t)(s / 10);
                g_sys.time_digits[5] = (uint8_t)(s % 10);
                if ((uint8_t)g_sys.time_cursor >= TIME_DIGIT_COUNT) g_sys.time_cursor = (TimeField_t)0;
                UI_DrawTimeAdjust();
                break;
            }
            case SCREEN_PTC_ADJUST:    UI_DrawPtcAdjust(); break;
            case SCREEN_PTC_EDIT:      UI_DrawPtcEdit(); break;
            case SCREEN_PTC_COOLING_EDIT: UI_DrawPtcCoolingEdit(); break;
            case SCREEN_PID_AUTOTUNE:  UI_DrawPidAutotune(); break;
            case SCREEN_PID_ADJUST:    UI_DrawPidAdjust(); break;
            case SCREEN_PRESET:        UI_DrawPresetMenu(); break;
            case SCREEN_PRESET_LIST:   UI_DrawPreset(); break;
            case SCREEN_PRESET_EDIT:   UI_DrawPresetEdit(); break;
            case SCREEN_MENU:          UI_DrawMenu(); break;
            case SCREEN_MOTOR_ADJUST:  UI_DrawMotorAdjust(); break;
            case SCREEN_ABOUT:         UI_DrawAbout(); break;
            case SCREEN_WIFI:          UI_DrawWiFiScreen(); break;
            case SCREEN_SETTINGS:      UI_DrawSettingsScreen(); break;
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
            case SCREEN_MENU:
                /* 像素滚动：滚动由编码器 UI_MenuScroll 直接重绘可视区，此处无需处理 */
                break;
            default:
            /* 子页面：仅当该页面相关状态变化时才重绘，避免每 50ms 整屏刷新闪烁 */
            {
                static uint8_t last_sel = 0xFF;
                static Screen_t last_scr = (Screen_t)0xFF;
                static uint8_t last_tc = 0xFF;
                static uint16_t last_ptc_max = 0xFFFF;
                static uint16_t last_ptc_cool = 0xFFFF;
                static uint8_t last_wifi = 0xFF;
    static uint8_t last_pid_prog = 0xFF;
    static uint8_t last_auto_prog = 0xFF;
    static uint8_t last_auto_run = 0xFF;
    static uint8_t last_air = 0xFF, last_ptc = 0xFF;   /* 校准页实时温度监看 */
                uint8_t redraw = 0;
                if (g_sys.current_screen != last_scr) {
                    redraw = 1;
                    last_scr = g_sys.current_screen;
                    /* 重置各页面状态标记，保证首次进入时重绘 */
                    last_sel = 0xFF; last_tc = 0xFF;
                    last_ptc_max = 0xFFFF; last_ptc_cool = 0xFFFF;
                    last_wifi = 0xFF;
                    last_pid_prog = 0xFF; last_auto_prog = 0xFF; last_auto_run = 0xFF;
                }
                switch (g_sys.current_screen) {
                    case SCREEN_TEMP_ADJUST: {
                        static uint8_t last_ta_sel = 0xFF;
                        static uint16_t last_ta_temp = 0xFFFF;
                        if (!redraw && g_sys.selected_item != last_ta_sel) {
                            uint8_t old = last_ta_sel;
                            last_ta_sel = g_sys.selected_item;
                            if (old < 4) refresh_temp_adj_sel(old, g_sys.selected_item);
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
                            if (old < 5) refresh_ptc_adj_sel(old, g_sys.selected_item);
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
                        static char last_wt_val[24] = "";
                        static uint8_t first_wt_val = 1;
                        if (!redraw && g_sys.selected_item != last_wt_sel) {
                            uint8_t old = last_wt_sel;
                            last_wt_sel = g_sys.selected_item;
                            if (old < 2) refresh_weight_sel(old, g_sys.selected_item);
                        }
                        last_wt_sel = g_sys.selected_item;
                        if (!redraw) {
                            char buf[24];
                            sprintf(buf, "当前: %dg", (int)fmt_weight_g());
                            if (first_wt_val) {
                                first_wt_val = 0;
                                strcpy(last_wt_val, buf);
                            } else if (strcmp(buf, last_wt_val) != 0) {
                                strcpy(last_wt_val, buf);
                                TFT_FillRect(20, 33, 200, 18, UI_BG);
                                TFT_DrawStringZh(30, 35, buf, UI_ACCENT2, UI_BG);
                            }
                        }
                        break;
                    }
                    case SCREEN_MOTOR_ADJUST: {
                        static uint32_t last_tmc_check = 0;
                        static uint32_t last_anim_tick = 0;
                        /* 静音滑块滑动动画：每 30ms 推进一帧，推进后重绘静音行 */
                        if (g_sys.mute_anim != 0) {
                            uint32_t now = SystemTime_Millis();
                            if ((uint32_t)(now - last_anim_tick) >= 30U) {
                                last_anim_tick = now;
                                uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                                                  g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
                                uint8_t mute_row = is_tmc ? 9 : 7;   /* 静音行索引（A4988 无电流项少一行） */
                                g_sys.mute_anim++;
                                if (g_sys.mute_anim > 12) g_sys.mute_anim = 0;
                                refresh_motor_row(mute_row);
                            }
                        } else {
                            last_anim_tick = 0;
                        }
                        /* 像素滚动由编码器 UI_MotorScroll 重绘可视区，此处不再整屏刷新 */
                        if (!redraw && g_sys.motor_edit_active) {
                            static char last_m_row[32] = "";
                            char buf[32];
                            motor_row_str(g_sys.selected_item, buf);
                            if (strcmp(buf, last_m_row) != 0) {
                                strcpy(last_m_row, buf);
                                refresh_motor_row(g_sys.selected_item);
                            }
                        }
                        /* TMC 通讯状态每秒刷新诊断行（底部 box 区域） */
                        if (!redraw && (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                                        g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209)) {
                            uint32_t now = SystemTime_Millis();
                            if ((uint32_t)(now - last_tmc_check) >= 1000U) {
                                last_tmc_check = now;
                                uint8_t ok = Stepper_TmcComOk();
                                uint16_t fg = ok ? COLOR_GREEN : COLOR_RED;
                                fill_round_rect(5, 208, 230, 22, UI_CARD, 6);
                                TFT_DrawString(12, 211, ok ? "TMC-COM:OK" : "TMC-COM:FAIL", fg, UI_CARD, 2);
                            }
                        }
                        break;
                    }
                    case SCREEN_PID_ADJUST:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        /* 编辑中每帧刷新当前行，值实时更新 */
                        if (!redraw && g_sys.pid_edit_active) {
                            refresh_pid_row((uint8_t)(g_sys.pid_edit_active - 1));
                        }
                        break;
                    case SCREEN_PRESET: {          /* 主菜单：4 固定行，选中变化整页刷新 */
                        static uint8_t last_msel = 0xFF;
                        if (g_sys.selected_item != last_msel) {
                            last_msel = g_sys.selected_item;
                            redraw = 1;
                        }
                        break;
                    }
                    case SCREEN_PRESET_LIST: {     /* 预设二级列表：滚动/确认弹窗局部刷新 */
                        static uint8_t last_cy = 0xFF;
                        static uint8_t last_pc = 0xFF;
                        static int16_t last_po = -1000;
                        static uint8_t last_pitem = 0xFF;
                        static uint8_t last_delmode = 0xFF;
                        static uint8_t last_cp = 0xFF;
                        uint8_t lcnt = (uint8_t)(g_sys.params.preset_count + 1);
                        if (g_sys.preset_del_mode != last_delmode) {
                            last_delmode = g_sys.preset_del_mode;
                            redraw = 1;
                        }
                        /* 长按切换当前预设 → ">" 标记变化，整可视区刷新清掉旧标记残留 */
                        if (!redraw && g_sys.params.current_preset != last_cp) {
                            last_cp = g_sys.params.current_preset;
                            preset_redraw_viewport();
                        } else {
                            last_cp = g_sys.params.current_preset;
                        }
                        if (!redraw && g_sys.pixel_offset != last_po) {
                            /* 滚动：整可视区重绘 */
                            last_po = g_sys.pixel_offset;
                            last_pitem = g_sys.selected_item;
                            preset_redraw_viewport();
                        } else if (!redraw && g_sys.selected_item != last_pitem) {
                            /* 视口内移动光标：只重绘旧/新两行 */
                            uint8_t old = last_pitem;
                            last_pitem = g_sys.selected_item;
                            last_po = g_sys.pixel_offset;
                            if (old < lcnt) draw_preset_row(old);
                            draw_preset_row(g_sys.selected_item);
                            draw_scrollbar(lcnt, 5, (uint8_t)(g_sys.pixel_offset / 20), 36, 100);
                        } else {
                            last_po = g_sys.pixel_offset;
                            last_pitem = g_sys.selected_item;
                        }
                        if (g_sys.preset_confirm != last_pc) {
                            if (last_pc == 1 && g_sys.preset_confirm == 0) redraw = 1;   /* 关闭弹窗清残留 */
                            last_pc = g_sys.preset_confirm;
                            last_cy = 0xFF;
                        }
                        /* 光标移动：只重绘弹窗内容，不整屏刷新 */
                        if (!redraw && g_sys.preset_confirm == 1 && g_sys.preset_confirm_yes != last_cy) {
                            last_cy = g_sys.preset_confirm_yes;
                            draw_confirm_popup("确认删除该预设");
                        }
                        break;
                    }
                    case SCREEN_PRESET_EDIT: {
                        static uint8_t last_pe = 0xFF;
                        static uint8_t last_prow = 0xFF;
                        static uint8_t last_name_cur = 0xFF;
                        static char last_name[9] = "";
                        static uint8_t last_tv = 0xFF;
                        static uint8_t last_dig[6] = {0,0,0,0,0,0};
                        static uint8_t last_cy2 = 0xFF;
                        static uint8_t last_pcf = 0xFF;
                        Preset_t *p = ui_preset_ptr();
                        if (g_sys.preset_confirm != last_pcf) {
                            last_pcf = g_sys.preset_confirm;
                            redraw = 1;              /* 弹窗开/关整页重绘：画上标题、关时清背景 */
                        }
                        if (g_sys.preset_confirm == 2) {   /* 保存确认：是/否只刷新弹窗 */
                            if (g_sys.preset_confirm_yes != last_cy2) {
                                last_cy2 = g_sys.preset_confirm_yes;
                                if (!redraw) draw_confirm_popup("确认保存");
                            }
                            if (g_sys.preset_confirm != 2) last_cy2 = 0xFF;
                        } else {
                            last_cy2 = 0xFF;
                            /* 行光标移动：名称/温度/时间/保存/退出 高亮行需实时重绘整页 */
                            if (!redraw && g_sys.preset_row != last_prow) {
                                last_prow = g_sys.preset_row;
                                redraw = 1;
                            }
                            if (g_sys.preset_row_edit != last_pe) {
                                last_pe = g_sys.preset_row_edit;
                                last_prow = g_sys.preset_row;
                                last_name_cur = 0xFF; last_name[0] = 0;
                                last_tv = 0xFF;
                                { uint8_t k; for (k = 0; k < 6; k++) last_dig[k] = 0xFF; }
                                redraw = 1;
                            }
                            if (!redraw && g_sys.preset_row_edit == 1) {   /* 名称：变化才重绘 */
                                if (g_sys.preset_name_cur != last_name_cur || strcmp(p->name, last_name) != 0) {
                                    last_name_cur = g_sys.preset_name_cur;
                                    strcpy(last_name, p->name);
                                    redraw = 1;
                                }
                            } else if (!redraw && g_sys.preset_row_edit == 2) {  /* 温度弹窗 */
                                if (p->temp != last_tv) { last_tv = p->temp; draw_preset_popup(2); }
                            } else if (!redraw && g_sys.preset_row_edit == 3) {  /* 时间弹窗 */
                                uint8_t k, changed = 0;
                                static uint8_t last_pcur = 0xFF;
                                static uint8_t last_ptedit = 0xFF;
                                for (k = 0; k < 6; k++) if (g_sys.time_digits[k] != last_dig[k]) changed = 1;
                                if (changed || g_sys.preset_time_cur != last_pcur || g_sys.preset_time_edit != last_ptedit) {
                                    for (k = 0; k < 6; k++) last_dig[k] = g_sys.time_digits[k];
                                    last_pcur = g_sys.preset_time_cur;
                                    last_ptedit = g_sys.preset_time_edit;
                                    draw_preset_popup(3);
                                }
                            }
                        }
                        break;
                    }
                    case SCREEN_ABOUT:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        break;
                    case SCREEN_TIME_ADJUST: {
                        static uint32_t last_dig_hash = 0;
                        static uint8_t last_edit = 0xFF;
                        uint32_t dig_hash = (uint32_t)g_sys.time_digits[0] | ((uint32_t)g_sys.time_digits[1] << 4) |
                                            ((uint32_t)g_sys.time_digits[2] << 8) | ((uint32_t)g_sys.time_digits[3] << 12) |
                                            ((uint32_t)g_sys.time_digits[4] << 16) | ((uint32_t)g_sys.time_digits[5] << 20);
                        if (g_sys.time_edit_active != last_edit) {
                            /* 编辑态切换：立即重绘当前位光标（进=橙轮廓，退=普通轮廓） */
                            last_edit = g_sys.time_edit_active;
                            if (!redraw && g_sys.time_cursor < TIME_DIGIT_COUNT) {
                                refr_time_digit((uint8_t)g_sys.time_cursor,
                                                g_sys.time_edit_active ? UI_TEXT : COLOR_CYAN,
                                                g_sys.time_edit_active ? COLOR_ORANGE : UI_ACCENT2);
                            }
                            last_tc = g_sys.time_cursor;
                            last_dig_hash = dig_hash;
                            break;
                        }
                        if (g_sys.time_edit_active) {
                            /* 编辑态：数值/光标变化时重绘当前位 */
                            if (!redraw && (g_sys.time_cursor != last_tc || dig_hash != last_dig_hash)) {
                                refr_time_digit((uint8_t)g_sys.time_cursor, UI_TEXT, COLOR_ORANGE);
                            }
                            last_tc = g_sys.time_cursor;
                            last_dig_hash = dig_hash;
                        } else {
                            if (!redraw && g_sys.time_cursor != last_tc) {
                                uint8_t oldc = last_tc;
                                if (oldc < TIME_DIGIT_COUNT) refr_time_digit(oldc, COLOR_CYAN, UI_ACCENT2);
                                if (g_sys.time_cursor < TIME_DIGIT_COUNT) refr_time_digit(g_sys.time_cursor, COLOR_CYAN, UI_ACCENT2);
                            }
                            last_tc = g_sys.time_cursor;
                        }
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
                    case SCREEN_WIFI:
                        if (g_sys.selected_item != last_sel) { redraw = 1; last_sel = g_sys.selected_item; }
                        if (g_sys.wifi_enabled != last_wifi) { redraw = 1; last_wifi = g_sys.wifi_enabled; }
                        break;
                    case SCREEN_TEMP_PID:
                        if (g_sys.temp_pid_running != last_auto_run) {
                            redraw = 1; last_auto_run = g_sys.temp_pid_running;
                        }
                        if (!redraw && g_sys.temp_pid_running && g_sys.temp_pid_progress != last_pid_prog) {
                            last_pid_prog = g_sys.temp_pid_progress;
                            refresh_autotune_bar(0, g_sys.temp_pid_progress);
                        }
                        {
                            uint8_t air = (uint8_t)(g_sys.current_temp * 10.0f);
                            uint8_t ptc = (uint8_t)(g_sys.ptc_temp * 10.0f);
                            if (air != last_air || ptc != last_ptc) {
                                last_air = air; last_ptc = ptc;
                                draw_tune_live();
                            }
                        }
                        break;
                    case SCREEN_PID_AUTOTUNE:
                        if (g_sys.pid_autotune_running != last_auto_run) {
                            redraw = 1; last_auto_run = g_sys.pid_autotune_running;
                        }
                        if (!redraw && g_sys.pid_autotune_running && g_sys.pid_autotune_progress != last_auto_prog) {
                            last_auto_prog = g_sys.pid_autotune_progress;
                            refresh_autotune_bar(1, g_sys.pid_autotune_progress);
                        }
                        {
                            uint8_t air = (uint8_t)(g_sys.current_temp * 10.0f);
                            uint8_t ptc = (uint8_t)(g_sys.ptc_temp * 10.0f);
                            if (air != last_air || ptc != last_ptc) {
                                last_air = air; last_ptc = ptc;
                                draw_tune_live();
                            }
                        }
                        break;
                    case SCREEN_SAFETY_ALERT:
                        break;
                    case SCREEN_SETTINGS: {
                        static char last_ss_row[24] = "";
                        static uint8_t last_pop_item = 0xFF;   /* 0xFF=无弹窗 */
                        /* 像素滚动由编码器 UI_SettingsScroll 重绘可视区，此处不再整屏刷新 */
                        /* 编辑中实时刷新当前行（值变化才重绘；音量/亮度走弹窗，不刷新行避免闪烁） */
                        if (!redraw && g_sys.settings_edit_active &&
                            g_sys.selected_item != 1 && g_sys.selected_item != 3 && g_sys.selected_item != 6) {
                            char buf[24];
                            settings_row_str(g_sys.selected_item, buf);
                            if (strcmp(buf, last_ss_row) != 0) {
                                strcpy(last_ss_row, buf);
                                refresh_settings_row(g_sys.selected_item);
                            }
                        }
                        /* RGB 亮度弹窗：光标(sel/编辑态)变化重绘结构；值变化仅刷该条；关闭整屏重绘清残留 */
                        {
                            static uint8_t last_rbp = 0xFF;
                            static uint8_t last_rbs = 0xFF;
                            static uint8_t last_rbe = 0xFF;
                            static uint8_t last_rb = 0xFF;
                            if (g_sys.rgb_bright_popup != last_rbp) {
                                if (!g_sys.rgb_bright_popup) redraw = 1;
                                last_rbp = g_sys.rgb_bright_popup;
                                last_rbs = 0xFF;
                            }
                            if (!redraw && g_sys.rgb_bright_popup) {
                                if (g_sys.rgb_bright_sel != last_rbs || g_sys.rgb_bright_edit != last_rbe) {
                                    last_rbs = g_sys.rgb_bright_sel;
                                    last_rbe = g_sys.rgb_bright_edit;
                                    draw_rgb_bright_popup();
                                }
                                if (g_sys.rgb_bright_sel <= 1) {
                                    uint8_t rb = g_sys.rgb_bright_sel ? g_sys.params.rgb_strip_bright : g_sys.params.rgb_led_bright;
                                    if (rb != last_rb) { last_rb = rb; draw_rgb_bar(g_sys.rgb_bright_sel); }
                                }
                            }
                        }
                        /* 蜂鸣器音量(1)/亮度(3) 编辑弹窗：仅在打开或数值变化时重绘（避免闪烁），
                         * 关闭后整屏重绘清残留 */
                        {
                            static char last_pop_val[16] = "";
                            uint8_t pop = 0xFF;
                            if (g_sys.settings_edit_active &&
                                (g_sys.selected_item == 1 || g_sys.selected_item == 3)) {
                                pop = g_sys.selected_item;
                            }
                            if (pop != last_pop_item) {
                                if (last_pop_item != 0xFF && pop == 0xFF) redraw = 1;
                                last_pop_item = pop;
                                last_pop_val[0] = 0;   /* 打开弹窗强制首绘 */
                            }
                            if (!redraw && pop != 0xFF) {
                                char pv[16];
                                uint8_t pvv = (pop == 1) ? g_sys.buzzer_vol : g_sys.backlight;
                                sprintf(pv, "%d", pvv);
                                if (strcmp(pv, last_pop_val) != 0) {
                                    strcpy(last_pop_val, pv);
                                    draw_settings_popup(pop);
                                }
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
                        case SCREEN_TIME_ADJUST:   UI_DrawTimeAdjust(); break;
                        case SCREEN_PTC_ADJUST:    UI_DrawPtcAdjust(); break;
                        case SCREEN_PTC_EDIT:      UI_DrawPtcEdit(); break;
                        case SCREEN_PTC_COOLING_EDIT: UI_DrawPtcCoolingEdit(); break;
                        case SCREEN_MOTOR_ADJUST:  UI_DrawMotorAdjust(); break;
                        case SCREEN_ABOUT:         UI_DrawAbout(); break;
                        case SCREEN_WIFI:          UI_DrawWiFiScreen(); break;
                        case SCREEN_TEMP_PID:      UI_DrawTempPid(); break;
                        case SCREEN_PID_AUTOTUNE:  UI_DrawPidAutotune(); break;
            case SCREEN_PID_ADJUST:    UI_DrawPidAdjust(); break;
            case SCREEN_PRESET:        UI_DrawPresetMenu(); break;
            case SCREEN_PRESET_LIST:   UI_DrawPreset(); break;
            case SCREEN_PRESET_EDIT:   UI_DrawPresetEdit(); break;
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
    sprintf(buf, "%d", (int)fmt_weight_g());
    if (strcmp(buf, last_val[2]) != 0) {
        draw_value_unit(2, x, y, ch,
                        (g_sys.selected_item == 2) ? UI_CARD_HI : CARD_BG_WEIGHT,
                        buf, UI_PURPLE);
        strcpy(last_val[2], buf);
    }

    /* 卡4 PTC 117x42 @(121,46) */
    main_card_rect(3, &x, &y, &w, &ch);
    sprintf(buf, "%d", (int)(g_sys.ptc_temp + 0.5f));
    if (strcmp(buf, last_val[3]) != 0) {
        draw_value_unit(3, x, y, ch,
                        (g_sys.selected_item == 3) ? UI_CARD_HI : CARD_BG_PTC,
                        buf, UI_ACCENT2);
        strcpy(last_val[3], buf);
    }

    /* 卡5 烘干时间 236x43 @(2,90)：时间数字仅变化时重绘。
     * 烘干中主显示=剩余倒计时(大字)；空闲主显示=设定时间 */
    main_card_rect(4, &x, &y, &w, &ch);
    {
        static char last_rem_str[16] = "";
        uint16_t tbg = (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME;
        uint8_t drying = (g_sys.run_state == STATE_DRYING || g_sys.run_state == STATE_COOLING);
        if (drying) {
            h = g_sys.remaining_sec / 3600;
            m = (g_sys.remaining_sec % 3600) / 60;
            s = g_sys.remaining_sec % 60;
            sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
            if (strcmp(buf, last_rem_str) != 0) {
                strcpy(last_rem_str, buf);
                TFT_DrawString((uint16_t)(x + (w - 8U * 12U) / 2U - 6U),
                               (uint16_t)(y + (ch - 14U) / 2U), buf, UI_OK, tbg, 2);
            }
        } else {
            h = g_sys.params.dry_time_sec / 3600;
            m = (g_sys.params.dry_time_sec % 3600) / 60;
            s = g_sys.params.dry_time_sec % 60;
            sprintf(buf, "%02lu:%02lu:%02lu", h, m, s);
            if (strcmp(buf, last_rem_str) != 0) {
                strcpy(last_rem_str, buf);
                TFT_DrawString((uint16_t)(x + (w - 8U * 12U) / 2U - 6U),
                               (uint16_t)(y + (ch - 14U) / 2U), buf, UI_ACCENT, tbg, 2);
            }
        }
    }
}

static void Delay_ms(uint16_t ms)
{
    for (volatile uint32_t i = 0; i < ms * 7200; i++);
}
#endif /* BOOTLOADER_BUILD */

