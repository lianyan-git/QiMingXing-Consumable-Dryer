#ifndef BOOTLOADER_BUILD
#include "ui_manager.h"
#include "system_config.h"
#include "bsp_encoder.h"
#include "bsp_tft_st7789.h"
#include "bsp_sht40.h"
#include "bsp_ntc.h"
#include "bsp_fan.h"
#include "bsp_rgb_led.h"
#include "bsp_cs1237.h"
#include "bsp_stepper.h"
#include "bsp_ptc.h"
#include "system_time.h"
#include "stm32f10x.h"
#include "esp_link.h"
#include "music_play.h"
#include "music_store.h"
#include "lang_ota.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

extern void CAN_Cluster_RequestSearch(void);

#define CARD_H         30
#define CARD_GAP       4
#define ACCENT_W       3
#define SEL_FRAME_W    2
#define BTN_Y          218
/* 鈹鈹 涓婚�槝鑹诧紙鍙�杩愯屾椂鍒囨崲锛屽彉閲忔浛浠 #define锛 鈹鈹 */
#define TFT_COLOR(r,g,b)  ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))

/* 浜�鑹膊涓婚�槩橀粯璁ゅ */
static uint16_t UI_BG           = TFT_COLOR(0xE8, 0xEA, 0xED);
static uint16_t UI_CARD         = TFT_COLOR(0xFF, 0xFF, 0xFF);
static uint16_t UI_CARD_HI      = TFT_COLOR(0xF0, 0xF4, 0xFF);
static uint16_t TIME_CARD_BG    = TFT_COLOR(0xF8, 0xFA, 0xFF);   /* 璁剧疆鏃堕棿椤佃‖搴曪紝闅忎富棰槩樺垏鎹 */
static uint16_t TIME_EDIT_TXT   = TFT_COLOR(0x22, 0x22, 0x22);   /* 缂栬緫鎬佹暟瀛楄壊锛岄殢涓婚�槝鍒囨� */
static uint16_t CARD_BG_TEMP    = TFT_COLOR(0xFF, 0xF3, 0xE0);
static uint16_t CARD_BG_HUMI    = TFT_COLOR(0xE1, 0xF5, 0xFE);
static uint16_t CARD_BG_WEIGHT  = TFT_COLOR(0xF3, 0xE5, 0xF5);
static uint16_t CARD_BG_PTC     = TFT_COLOR(0xFF, 0xEB, 0xEE);
static uint16_t CARD_BG_TIME    = TFT_COLOR(0xE8, 0xF5, 0xE9);
static uint16_t UI_FG           = TFT_COLOR(0x22, 0x22, 0x22);  /* 涓婚�槝鍓嶆欒壊锛圵iFi 椤电瓑锛 */
static uint16_t UI_FG_DIM       = TFT_COLOR(0x88, 0x88, 0x88);  /* 涓婚�槝寮卞寲鍓嶆欒� */

void theme_apply(void)
{
    if (g_sys.theme == 0) {  /* 亮色 */
        UI_BG        = TFT_COLOR(0xE8, 0xEA, 0xED);
        UI_CARD      = TFT_COLOR(0xFF, 0xFF, 0xFF);
        UI_CARD_HI   = TFT_COLOR(0xF0, 0xF4, 0xFF);
        TIME_CARD_BG = TFT_COLOR(0xF8, 0xFA, 0xFF);  /* 杩戠櫧锛氱櫧鑹蹭富棰槩樹笅涓庡師瑙傛劅涓鑷 */
        TIME_EDIT_TXT = TFT_COLOR(0x22, 0x22, 0x22);  /* 浜�鑹膊涓婚�槩樹笅缂栬緫鏁板瓧鐢ㄦ繁鐏 */
        CARD_BG_TEMP = TFT_COLOR(0xFF, 0xF3, 0xE0);
        CARD_BG_HUMI = TFT_COLOR(0xE1, 0xF5, 0xFE);
        CARD_BG_WEIGHT = TFT_COLOR(0xF3, 0xE5, 0xF5);
        CARD_BG_PTC  = TFT_COLOR(0xFF, 0xEB, 0xEE);
        CARD_BG_TIME = TFT_COLOR(0xE8, 0xF5, 0xE9);
        UI_FG        = TFT_COLOR(0x22, 0x22, 0x22);   /* 浜�鑹膊锛氭繁鐏版枃瀛� */
        UI_FG_DIM    = TFT_COLOR(0x88, 0x88, 0x88);
    } else {  /* 鏆楄壊锛堟煍鍜屾繁鐏拌摑锛岄伩鍏嶈繎榛戦犳垚"黑底"瑙傛劅锛 */
        UI_BG        = TFT_COLOR(0x3A, 0x3A, 0x4A);
        UI_CARD      = TFT_COLOR(0x46, 0x46, 0x58);
        UI_CARD_HI   = TFT_COLOR(0x52, 0x52, 0x66);
        TIME_CARD_BG = TFT_COLOR(0x4E, 0x4E, 0x60);   /* 鏆楄壊涓婚�槝锛氳″簳姣旈〉闈㈠簳鐣ヤ轰告。锛屼笌鏆楄儗鏅�璐村悎 */
        TIME_EDIT_TXT = TFT_COLOR(0xEC, 0xEF, 0xF1);  /* 鏆楄壊涓婚�槝涓嬬紪杈戞暟瀛楃敤浜�鐏 */
        CARD_BG_TEMP = TFT_COLOR(0x4E, 0x3D, 0x2A);
        CARD_BG_HUMI = TFT_COLOR(0x2A, 0x3D, 0x4E);
        CARD_BG_WEIGHT = TFT_COLOR(0x3D, 0x2A, 0x3D);
        CARD_BG_PTC  = TFT_COLOR(0x4E, 0x2A, 0x2A);
        CARD_BG_TIME = TFT_COLOR(0x2A, 0x4E, 0x2A);
        UI_FG        = TFT_COLOR(0xEC, 0xEF, 0xF1);   /* 暗色：亮灰文字（不再黑字看不清） */
        UI_FG_DIM    = TFT_COLOR(0x9A, 0x9A, 0xAC);
    }
}

#define BTN_H          18
#define BTN_GAP        4

/* 闈炶儗鏅鑹蹭繚鎸佸父閲 #define */
#define UI_CARD_EDGE    TFT_COLOR(0xD0, 0xD0, 0xD0)
#define UI_TEXT         TFT_COLOR(0x22, 0x22, 0x22)
#define UI_TEXT_DIM     TFT_COLOR(0x88, 0x88, 0x88)
#define UI_ACCENT       TFT_COLOR(0x21, 0x96, 0xF3)
#define UI_ACCENT2      TFT_COLOR(0xFF, 0x52, 0x52)
#define UI_OK           TFT_COLOR(0x4C, 0xAF, 0x50)
#define UI_WARN         TFT_COLOR(0xFF, 0xC1, 0x07)
#define UI_CYAN         TFT_COLOR(0x00, 0xBC, 0xD4)
#define UI_PURPLE       TFT_COLOR(0x9C, 0x27, 0xB0)

/* 涓诲壇鏍囬樼敤 TFT_DrawString(scale) 娓叉煋锛堝師鐗 5x7 鐐归樀锛 */

static void Delay_ms(uint16_t ms);

/* 鏅閫氳竟妗嗭紙涓嶄繚鐣欏渾瑙掞紝鐢ㄤ簬鎸夐挳绛夛級 */
static void draw_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    TFT_FillRect(x, y, w, SEL_FRAME_W, color);
    TFT_FillRect(x, y + h - SEL_FRAME_W, w, SEL_FRAME_W, color);
    TFT_FillRect(x, y, SEL_FRAME_W, h, color);
    TFT_FillRect(x + w - SEL_FRAME_W, y, SEL_FRAME_W, h, color);
}

/* 鍦嗚掕竟妗嗭細鐩寸嚎娈 + 鍥涜掑渾寮ф弿杈癸紙瀹屾暣璺熼殢鍦嗚掕疆寤擄級 */
/* 鐪熷渾瑙掔煩褰锛氭爣鍑嗗渾瑙掆斺旇掑姬鍦嗗績浣嶄簬 (x+r, y+r)锛屽崐寰 r銆
 * 瑙掑尯鍍忕礌涓蹇冨埌鍦嗗績璺濈 > r 鑰呭垏闄わ紝涓闂寸敤 FillRect 楂樻晥濉鍏咃紝鍥涜掑圭О銆 */
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
            int32_t maxdx = r2 - dy * dy;                     /* 鏈琛 螖x2 涓婇檺 */
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

/* 鍦嗚掓弿杈癸細鍘 th 鐨勮竟妗嗭紝鍦嗗姬涓 fill_round_rect 鍚屽渾蹇冣斺旇创鍚堣疆寤撴弿杈癸紝
 * 鍐呴儴鍙鏈夊钩閾哄～鍏咃紝涓嶅啀鍑虹幇鍐呭眰宓屽楀渾寮с */
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
    /* 鐩寸嚎娈 */
    TFT_FillRect(x + radius, y, w - 2U * radius, th, color);               /* 涓 */
    TFT_FillRect(x + radius, y + h - th, w - 2U * radius, th, color);      /* 涓 */
    TFT_FillRect(x, y + radius, th, h - 2U * radius, color);               /* 宸 */
    TFT_FillRect(x + w - th, y + radius, th, h - 2U * radius, color);      /* 鍙 */
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
                /* 鍙充晶闀滃儚锛氳掑唴鐩稿瑰垪 (r-1-j2) 璧风瑪锛屼笌宸︿晶涓ユ牸瀵圭О */
                uint16_t rx = (uint16_t)(x + w - radius + (radius - 1 - j2));
                TFT_FillRect((uint16_t)(x + j1), (uint16_t)(y + i), cnt, 1U, color);                  /* 左上 */
                TFT_FillRect(rx, (uint16_t)(y + i), cnt, 1U, color);                                  /* 右上 */
                TFT_FillRect((uint16_t)(x + j1), (uint16_t)(y + h - 1U - i), cnt, 1U, color);         /* 左下 */
                TFT_FillRect(rx, (uint16_t)(y + h - 1U - i), cnt, 1U, color);                         /* 右下 */
            }
        }
    }
}

/* 鈹鈹 棰滆壊鎻掑煎伐鍏凤紙RGB565锛 鈹鈹 */
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

/* 閫変腑鍗￠《閮ㄨ壊鏉★細闅忔椂闂村湪 ACCENT 涓庨珮浜涔嬮棿鍛煎惛锛堝钩婊戝姩鐢伙級 */
static uint16_t pulse_color(void)
{
    uint32_t t = SystemTime_Millis() % 1600U;   /* 1.6s 周期 */
    uint8_t phase;
    if (t < 800U) phase = (uint8_t)(t * 255U / 800U);
    else phase = (uint8_t)((1600U - t) * 255U / 800U);
    return lerp_color(UI_ACCENT, UI_TEXT, phase);
}

/* 鍦ㄥ崱鐗囬《閮ㄧ敾楂樹寒鍛煎惛鑹叉潯锛堜粎閫変腑鍗★紝灏忓尯鍩熷眬閮ㄥ埛鏂颁笉闂鐑侊級 */
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

/* 缁樺埗鍙充晶婊氬姩鏉★細total_items=鎬绘暟, per_page=姣忛〉鍙瑙佹暟, cursor_pos=褰撳墠閫変腑椤圭储寮 */
static void draw_scrollbar(uint8_t total, uint8_t per_page, uint8_t cursor_pos, uint16_t sy, uint16_t sh)
{
    if (total <= per_page) return;
    uint8_t thumb_h = (uint16_t)sh * per_page / total;
    if (thumb_h > sh / 2) thumb_h = sh / 2;
    if (thumb_h < 8) thumb_h = 8;
    /* 婊戝潡浣嶇疆锛氶変腑绱㈠紩閽充綅鍒 [0, total-per_page]锛屾粴鍔ㄥ埌搴曟椂婊戝潡鍋滃湪杞ㄩ亾鏈绔 */
    uint16_t max_pos = (uint16_t)(total - per_page);
    uint16_t pos = (cursor_pos > max_pos) ? max_pos : cursor_pos;
    uint16_t range = (sh > thumb_h) ? (sh - thumb_h) : 0;
    uint16_t thumb_y = sy + (uint16_t)((uint32_t)range * pos / (max_pos ? max_pos : 1U));
    TFT_FillRect(234, sy, 5, sh, UI_CARD_EDGE);
    TFT_FillRect(234, thumb_y, 5, thumb_h, UI_ACCENT);
}

/* 鈹鈹 鍍忕礌婊氬姩锛氬彲瑙嗗尯鍙傛暟 鈹鈹 */
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

/* 閲嶉噺鍝戦搩鍥炬爣锛30x30 RGB565 浣嶅浘锛岄忔槑鑹 0xF81F锛 */
/* 閲嶉噺鍝戦搩鍥炬爣锛18x17 RGB565 浣嶅浘锛岄忔槑鑹 0xF81F锛屼富浣撶粺涓 UI_PURPLE锛 */
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


/* 閲嶉噺鍝戦搩鍥炬爣锛18x17 浣嶅浘锛岄忔槑鑹 0xF81F锛 */
static void draw_icon_weight(uint16_t x, uint16_t y, uint16_t bg)
{
    TFT_DrawBitmap(x, y + 7, 18, 17, icon_weight_bmp, 0xF81F, bg);
}

/* 婀垮害姘存淮鍥炬爣锛14x20 RGB565 浣嶅浘锛岄忔槑鑹 0xF81F锛 */
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

/* PTC 鍔犵儹鍥炬爣锛30x30 RGB565 浣嶅浘锛岄忔槑鑹 0xF81F锛屼富浣撶粺涓 UI_ACCENT2锛 */
/* PTC 鍔犵儹鍥炬爣锛堢伀鑻楋紝22x22 RGB565锛岄忔槑鑹 0xF81F锛屼繚鐣欏唴閮ㄥ氳壊锛 */
/* PTC 鍔犵儹鍥炬爣锛堢伀鑻楋紝22x22 RGB565锛岄忔槑鑹 0xF81F锛屽栧３ UI_ACCENT2+鍐呰姱榛勶級 */
/* PTC 鍔犵儹鍥炬爣锛堢伀鑻楋紝26x26 RGB565锛岄忔槑鑹 0xF81F锛屽栧湀 UI_ACCENT2 鍐呭～榛勶級 */
/* PTC 鍔犵儹鍥炬爣锛堢伀鑻楋紝26x26 RGB565锛岄忔槑鑹 0xF81F锛屽栫孩涓姗欏唴榛勶級 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛屽栫孩涓姗欏唴榛勶級 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛屼笂绾㈢幆+姗欎綋+搴曢儴灏忛粍锛 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛岀孩鐜+姗欎綋+搴曢儴 5px 灏忛粍鐒帮級 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛岀孩鐜+姗欎綋+搴曢儴 5灞婇粍鍧楋級 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛岀孩鐜+姗欎綋+搴曢儴绛夋瘮缂╁皬灏忕伀鐒帮級 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛岀孩鐜+姗欎綋+搴曢儴澶栧湀+涓闂村湀缂╁皬(榛)锛 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛岀孩鐜+姗欎綋+搴曢儴绛夋瘮缂╂斁鐏鐒拌疆寤(榛)锛 */
/* PTC 鐏鑻楀浘鏍囷紙30x30 鍙栨ā锛26x26 缁樺埗锛孯GB565锛岄忔槑 0xF81F锛岀孩鐜+姗欎綋+搴曢儴灞呬腑缂╁皬鐏鐒(榛勫疄蹇)锛 */
/* PTC 鐏鑻楀浘鏍囷紙15x21 RGB565锛岄忔槑 0xF81F锛 */
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














/* PTC 鍔犵儹鍥炬爣锛15x21 浣嶅浘锛岄忔槑鑹 0xF81F锛 */
static void draw_icon_ptc(uint16_t x, uint16_t y, uint16_t bg)
{
    TFT_DrawBitmap(x, y + 5, 15, 21, icon_ptc_bmp, 0xF81F, bg);
}

/* 鏃堕挓/鐑樺共鏃堕棿鍥炬爣锛22x22 RGB565锛岄忔槑鑹 0xF81F锛100x100鍘熷浘瑁佸壀缂╁皬锛 */
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

/* 灏忔椂:鍒嗛挓:绉 鍏浣嶆暟瀛楃殑 X 鍧愭爣锛坰ize 4锛屾瘡涓 24px锛 */
static const uint16_t time_digit_x[6] = {20, 50, 94, 124, 168, 198};

/* 灞閮ㄩ噸缁樺崟涓鏃堕棿鏁板瓧 + 鍏夋爣妗嗭紙妗嗙揣璐村瓧妯″瓧褰㈠洓闈锛屾棤澶氫綑闂撮殭锛
 * 閫変腑鎬佸彧鏀瑰厜鏍囪疆寤撻滆壊锛屼笉濉鍏呰儗鏅 */
static void refr_time_digit(uint8_t i, uint16_t txt_color, uint16_t border_color)
{
    char buf[8];
    uint16_t dx = time_digit_x[i];
    TFT_FillRect(dx - 2, 49, 26, 34, TIME_CARD_BG);
    sprintf(buf, "%d", g_sys.time_digits[i]);
    TFT_DrawString(dx, 52, buf, txt_color, TIME_CARD_BG, 4);
    /* 鍏夋爣妗嗭細5x7 瀛楁ā scale4 澧ㄨ抗=52..79(28px)锛岄《妗50-51/搴曟80-81 绱ц创澧ㄨ抗涓婁笅缂 */
    if (g_sys.time_cursor == i) draw_frame(dx - 2, 50, 24, 32, border_color);
}

/* 閫夐」椤靛彸渚ф暟鍊 + 鈩冿紝鍊煎彸瀵归綈鍒 x=190 鍓嶏紙閬垮紑宸︿晶鏍囩撅紝娑堥櫎閲嶅彔锛 */
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

/* 妯灞忓崱鐗囪儗鏅锛堝彲鎸囧畾鍧愭爣/灏哄革級锛宻gl 椋庢牸锛氭繁鑹插崱鐗 + 椤惰竟楂樹寒 */
static void __attribute__((unused)) draw_card_bg_at(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t accent_color)
{
    TFT_FillRect(x, y, w, h, UI_CARD);
    TFT_FillRect(x, y, w, 2, accent_color);        /* 顶部色条 */
    TFT_FillRect(x, y + h - 2, w, 2, accent_color); /* 底部色条 */
}

/* 鈹鈹 寮鏈鸿繘搴︽潯锛堟í鍚戣嵂涓稿舰锛30,92 180x14锛屽崐寰=鍗婇珮7锛 鈹鈹 */
#define PB_X   30U
#define PB_Y   92U
#define PB_W   180U
#define PB_H   14U

/* 语言字库引导页：无字库时显示 AP/密码/IP/进度(全 ASCII, 不依赖字库) */
/* 高对比进度条(240x135 屏), 引导页用 (ASCII 大字), BL 样式精简 */
static void lang_draw_progress(uint8_t pct)
{
    char buf[8];
    uint16_t pw;
    if (pct > 100U) pct = 100U;
    TFT_FillRect(16, 64, (uint16_t)(TFT_WIDTH - 32), 12, UI_BG);
    pw = (uint16_t)((uint32_t)(TFT_WIDTH - 34) * (uint16_t)pct / 100U);
    if (pw) TFT_FillRect(17, 65, pw, 10, TFT_COLOR(0x25,0xE0,0x7A));
    sprintf(buf, "%u%%", (unsigned)pct);
    { uint32_t lw = (uint32_t)strlen(buf) * 12U;
      TFT_FillRect(16, 84, TFT_WIDTH - 32, 22, TFT_COLOR(0x22,0x26,0x38));
      TFT_DrawString((uint16_t)((TFT_WIDTH - (uint16_t)lw) / 2U), 84, buf,
                     TFT_COLOR(0xFF,0xFF,0xFF), TFT_COLOR(0x22,0x26,0x38), 2); }
}

void UI_DrawLangLoad(void)
{
    TFT_FillScreen(TFT_COLOR(0x22,0x26,0x38));
    TFT_DrawString((TFT_WIDTH - (uint16_t)(13 * 12U)) / 2U, 6, "Language load", TFT_COLOR(0x8B,0x5C,0xF6), TFT_COLOR(0x22,0x26,0x38), 2);
    TFT_DrawString(20, 28, "SSID : QIMINGXING", TFT_COLOR(0xFF,0xFF,0xFF), TFT_COLOR(0x22,0x26,0x38), 1);
    TFT_DrawString(20, 40, "PASS : 12345678",  TFT_COLOR(0xFF,0xFF,0xFF), TFT_COLOR(0x22,0x26,0x38), 1);
    TFT_DrawString(20, 52, "URL  : 192.168.4.1", TFT_COLOR(0xFF,0xFF,0xFF), TFT_COLOR(0x22,0x26,0x38), 1);

    g_sys.lang_upload_pct = 0;
    lang_draw_progress(0);
}

void UI_ShowBootScreen(void)
{
    TFT_FillScreen(UI_BG);

    /* 涓绘爣棰 QIMINGXING锛坰cale=4 鐧借壊锛 */
    {
        const char *title = "QIMINGXING";
        uint16_t tw = (uint16_t)(strlen(title) * 24U);
        TFT_DrawString((uint16_t)((TFT_WIDTH - tw) / 2U), 28, title,
                       TFT_COLOR(0xFF, 0xFF, 0xFF), UI_BG, 4);
    }

    /* 鍓鏍囬 */
    {
        const char *sub = "Drying Control System";
        uint16_t sw = (uint16_t)(strlen(sub) * 6U);
        TFT_DrawString((uint16_t)((TFT_WIDTH - sw) / 2U), 69, sub,
                       TFT_COLOR(0x7F, 0x84, 0x9C), UI_BG, 1);
    }

    /* 杩涘害鏉★紙杞ㄩ亾 30,92 180x14 r7 鑽涓稿舰锛 */
    uint16_t pb_bg = TFT_COLOR(0x6A, 0x6E, 0x88);
    uint16_t pb_fg = TFT_COLOR(0xF5, 0xA6, 0x23);
    fill_round_rect(PB_X, PB_Y, PB_W, PB_H, pb_bg, 7);

    char buf[24];
    uint16_t sry = (uint16_t)(PB_Y + PB_H + 4U);
    uint16_t srx = (uint16_t)((TFT_WIDTH - (13U * 6U + 4U * 6U)) / 2U);
    uint16_t pctx = (uint16_t)(srx + 13U * 6U);
    TFT_DrawString(srx, sry, "system ready ", TFT_COLOR(0x7F, 0x84, 0x9C), UI_BG, 1);

    /* 鍚庡彴璇诲彇浼犳劅鍣 + 椋庢墖瀹夊叏锛堜笉鏄剧ず鏁板硷級 */
    uint8_t fan_cooling = 0;

    for (int pct = 0; pct <= 100; pct += 2) {
        uint16_t fw = (uint16_t)((uint32_t)PB_W * (uint32_t)pct / 100U);
        if (fw > 0U && fw < 14U) fw = 14U;   /* 鏈灏忔樉绀哄藉害=鍦嗗ご鐩村緞锛屽乏绔浠庣涓甯ц捣灏辨槸鍦嗚 */
        if (fw > 0U) fill_round_rect(PB_X, PB_Y, fw, PB_H, pb_fg, 7);

        TFT_FillRect(pctx, sry, 4U * 6U, 8U, UI_BG);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString(pctx, sry, buf, TFT_COLOR(0x7F, 0x84, 0x9C), UI_BG, 1);

        /* 30% 鏃惰讳紶鎰熷櫒 + 妫鏌 NTC 鏄�鍚﹂渶瑕佹暎鐑 */
        if (pct == 30) {
            float t, h;
            if (SHT40_Read(&t, &h) == 0) {
                g_sys.current_temp = t;
        g_sys.current_humidity = h;
    }
    {
        int16_t r = NTC_GetTemperature();
        if (r >= NTC_READ_LO_10C && r <= NTC_READ_HI_10C)
            g_sys.ptc_temp = (float)r / 10.0f;   /* 哨兵不进显示(保持上一有效) */
    }

            /* 寮鏈鸿嚜鍔ㄥ幓鐨 + 璇诲彇閲嶉噺锛屽睆骞曚寒璧峰墠鍗＄墖鍗虫湁璇绘暟 */
            CS1237_Tare();
            {
                float w = CS1237_ReadWeight();
                if (w > -1000.0f) g_sys.weight_g = (int32_t)(w + ((w >= 0.0f) ? 0.5f : -0.5f));
            }

            /* 鍙妫娴 NTC 娓╁害 > 鍐峰嵈娓╁害 鈫 寮椋庢墖 */
            if (g_sys.ptc_temp > (float)g_sys.params.ptc_cooling_temp) {
                PTC_SetPower(0);   /* 安全: NTC 超冷却温度, 先关加热 */
                Fan_SetSpeed(100); /* 同时开风扇散热 */
                fan_cooling = 1;
            }
        }

        /* 60% 鏃堕噸璇曟俯婀垮害锛30% 鑻ュけ璐ュ啀璇曚竴娆★紝纭淇 I2C 璇绘潯鏈熼棿瀹屾垚锛 */
        if (pct == 60) {
            float t, h;
            /* 寮鏈哄幓鐨閲嶈瘯锛氳姱鐗囦笂鐢靛缓绔嬮渶鑻ュ共杞鎹㈠懆鏈燂紝30% 鑻ユ牱鏈涓嶈冻鏈鎴愰浂浣嶏紝杩欓噷鍐嶈瘯涓娆 */
            {
                float w0 = CS1237_ReadWeight();
                if (w0 < -1000.0f || w0 > 6000.0f || w0 < -20.0f) {
                    CS1237_Tare();
                    w0 = CS1237_ReadWeight();
                }
                if (w0 > -1000.0f) g_sys.weight_g = (int32_t)(w0 + ((w0 >= 0.0f) ? 0.5f : -0.5f));
            }
            if (SHT40_Read(&t, &h) == 0) {
                g_sys.current_temp = t;
                g_sys.current_humidity = h;
            }
        }

        /* 70% 鏃跺嶆煡 NTC */
    if (pct == 70 && fan_cooling) {
        int16_t r2 = NTC_GetTemperature();
        if (r2 >= NTC_READ_LO_10C && r2 <= NTC_READ_HI_10C)
            g_sys.ptc_temp = (float)r2 / 10.0f;
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

/* 閫氱敤妯灞忔爣棰樻爮锛氭棤鑳屾櫙鑹插甫锛屼粎鍒嗛殧绾 + 灞呬腑鏍囬 */
static void __attribute__((unused)) draw_page_title(const char *title, uint16_t accent)
{
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(title) * 12U)) / 2U,
                   6, title, accent, UI_BG, 2);
}

/* UTF-8 娣峰悎瀛楃︿覆娓叉煋瀹藉害(TFT_DrawStringZh 甯冨眬): 涓鏂=17px, ASCII=16px */
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

/* 涓鏂囨爣棰樻爮锛氭繁鑹插簳 + 鍒嗛殧绾 + 鎸変腑鏂囧藉害灞呬腑 */
static void draw_page_title_zh(const char *title, uint16_t accent)
{
    /* 鏃犺儗鏅鑹插甫锛氭爣棰樻枃瀛楃洿鎺ュ啓鍦 UI_BG 涓婏紝浠呬繚鐣欏簳閮 1px 鍒嗛殧绾 */
    TFT_FillRect(0, 24, TFT_WIDTH, 1, UI_CARD_EDGE);
    TFT_DrawStringZh((TFT_WIDTH - zh_str_width(title)) / 2U, 6, title, accent, UI_BG);
}

/* 涓荤晫闈 5 寮犲崱鐗囷紙妯灞 240x135锛夛細4 寮 117x42 + 搴曢儴 236x43
 * 宸︿笂瑙掍负鍘熺偣锛孹 鍚戝彸涓烘ｏ紝Y 鍚戜笅涓烘ｏ紝鍗＄墖宸︿笂瑙掍负鍙傝冧腑蹇 */
static void main_card_rect(uint8_t item, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
    switch (item) {
    case 0: *x = 2;   *y = 2;   break;              /* TEMP */
    case 1: *x = 121; *y = 2;   break;             /* 湿度 */
    case 2: *x = 2;   *y = 46;  break;             /* WEIGHT */
    case 3: *x = 121; *y = 46;  break;             /* PTC */
    default:*x = 2;   *y = 90;  *w = 236; *h = 43; return; /* 鏃堕棿鏍 */
    }
    *w = 117;
    *h = 42;
}

/* 閲嶉噺鏄剧ず鏁存暟鍊硷細int 鍙橀噺锛屽厑璁歌礋鏁扮洿鎺ユ樉绀 */
static int32_t fmt_weight_g(void)
{
    return g_sys.weight_g;
}

/* 缁樺埗鏁板+鍗曚綅锛坰ize2 涓庢暟瀛楀悓瀛楀彿锛夛紝绱ц窡鏁板煎悗锛屼緵鍏ㄥ睆涓庡眬閮ㄥ埛鏂板叡鐢ㄣ
 * 鏁板+鍗曚綅缁勫湪鍗＄墖鏂囨湰鍖(x+32..x+112)鍐呮按骞冲眳涓锛涙瘡甯у厛娓呮暣涓鍖猴紙鍚� 掳 椤惰岋級锛
 * 鍊煎彉鐭鏃朵笉鐣欐畫褰憋紝鍗曚綅/鈩 鑷鍔ㄨ创鍊煎姩鎬佽窡闅忋 */
static void draw_value_unit(uint8_t item, uint16_t x, uint16_t y, uint16_t h,
                            uint16_t bg, const char *value, uint16_t val_color)
{
    uint16_t vw;
    uint16_t vx;
    uint16_t vy = (uint16_t)(y + (h - 14U) / 2U);
    uint16_t ux;
    uint16_t left, right, group_w;

    vw = (uint16_t)(strlen(value) * 12U);

    /* 缁勫斤細鏁板 + 4px 闂撮殭 + 鍗曚釜鍗曚綅瀛楃(闄ゆ箍搴﹀凡鍚�%澶) */
    group_w = (item == 1U) ? vw : (uint16_t)(vw + 16U);
    left  = (uint16_t)(x + 32U);
    right = (uint16_t)(x + 112U);
    if (group_w >= (uint16_t)(right - left)) vx = left;
    else vx = (uint16_t)(left + ((uint16_t)(right - left) - group_w) / 2U);
    ux = (uint16_t)(vx + vw + 4U);

    /* 娓呭睆鍖哄浐瀹氫粠 left 璧凤紙鍊煎彉鐭閲嶅眳涓鏃舵棫鍊煎乏渚т篃涓嶄細娈嬬暀锛夛紝鍚戜笂鍚� 掳 椤惰 */
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

/* 缁樺埗鍗曞紶涓荤晫闈㈠崱鐗囷細澶у浘鏍(宸) + 鏁板+鍗曚綅(鍙)锛屽崟浣嶄笌鏁板瓧鍚屽瓧鍙(size2) */
static void draw_main_card(uint8_t item, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t bg,
                           const char *value,
                           uint16_t val_color, uint8_t selected)
{
    uint16_t fill = selected ? UI_CARD_HI : bg;
    uint8_t icon_x, icon_y;

    /* 鏁村崱鍗曞眰鍦嗚掑～鍏 + 鍦嗚掓弿杈癸紝鍐呴儴骞抽摵鏃犲祵濂楀渾寮 */
    fill_round_rect(x, y, w, h, fill, 10);
    draw_round_outline(x, y, w, h, selected ? UI_ACCENT : UI_CARD_EDGE, 10, 2);

    /* 鍥炬爣 30x30锛氳窛鍗＄墖宸﹁竟缂 6px锛堢暐鍋忓彸锛夛紝鍨傜洿灞呬腑 */
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

/* 灞閮ㄥ埛鏂板崟寮犱富鐣岄潰鍗＄墖锛堜笉鏁村睆閲嶇粯锛孲GL 鑴忕煩褰㈡濊矾锛 */
/* ==== 显示格式化: 手工整数拼串(替代 %f 族 sprintf) ====
 * MicroLIB 只要镜像中存在任何一处 %.Nf 就会链入 ~2KB 浮点输出路径;
 * PID/传感器/参数照常使用 float, 仅"生成显示字符串"改整数定点数位,
 * 十分位输入下输出与 %f 完全一致(round-half-up)。链式追加, ui_fin() 封口。 */
static char *ui_cpy(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *ui_fin(char *p) { *p = 0; return p; }

static char *ui_itoa(char *p, uint32_t v)
{
    char tmp[11];
    uint8_t k = 0;
    do { tmp[k++] = (char)('0' + (v % 10U)); v /= 10U; } while (v);
    while (k) *p++ = tmp[--k];
    return p;
}

/* 十分位整数 → "x.y" */
static char *ui_tenths(char *p, uint32_t tenth)
{
    p = ui_itoa(p, tenth / 10U);
    *p++ = '.'; *p++ = (char)('0' + (tenth % 10U));
    return p;
}

/* float → 一位小数 "x.y" */
static char *ui_d1(char *p, float v)
{
    int32_t n;
    if (v < 0.0f) { *p++ = '-'; v = -v; }
    n = (int32_t)(v * 10.0f + 0.5f);
    return ui_tenths(p, (uint32_t)n);
}

/* float → 两位小数 "x.yy" */
static char *ui_d2(char *p, float v)
{
    int32_t n;
    uint32_t w, f;
    if (v < 0.0f) { *p++ = '-'; v = -v; }
    n = (int32_t)(v * 100.0f + 0.5f);
    w = (uint32_t)n / 100U; f = (uint32_t)n % 100U;
    p = ui_itoa(p, w);
    *p++ = '.';
    *p++ = (char)('0' + f / 10U);
    *p++ = (char)('0' + f % 10U);
    return p;
}

/* 校准页汇总行 "KP:x.xx KI:x.xx KD:x.xx" */
static void ui_pid_line(char *buf, float kp, float ki, float kd)
{
    char *p = buf;
    p = ui_d2(ui_cpy(p, "KP:"), kp);
    p = ui_d2(ui_cpy(p, " KI:"), ki);
    p = ui_d2(ui_cpy(p, " KD:"), kd);
    *p = 0;
}

/* PID 调整页单行 "KP:x.x" */
static void ui_pid_row1(char *buf, const char *tag, float v)
{
    char *p = ui_d1(ui_cpy(buf, tag), v);
    *p = 0;
}

void UI_RefreshCard(uint8_t item)
{
    char buf[32];
    uint16_t x, y, w, h;
    if (item > 4) return;   /* 闃叉㈣彍鍗曟硠婕忕殑瓒婄晫绱㈠紩鐢诲嚭閿欒鍗＄墖 */
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
        if (g_sys.run_state == STATE_HEATING || g_sys.run_state == STATE_DRYING ||
            g_sys.run_state == STATE_COOLING) {   /* 开启烘干即倒计时(HEATING 也显示剩余) */
            /* 鐑樺共涓锛氫富鏄剧ず鍓╀綑鍊掕℃椂锛堝ぇ瀛楋級锛屼笂鏂逛笉鍐嶆斁璁惧畾灏忓瓧閬垮厤涓庣姸鎬佹枃瀛楅噸鍙 */
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
        ui_fin(ui_d1(buf, g_sys.current_temp));
        draw_main_card(0, x, y, w, h, CARD_BG_TEMP, buf, UI_WARN,
                       g_sys.selected_item == 0);
        break;
    case 1:
        ui_fin(ui_cpy(ui_d1(buf, g_sys.current_humidity), "%"));
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

    /* 鈹鈹 鍗1 TEMP 117x42 @(2,2) 鈹鈹 */
    ui_fin(ui_d1(buf, g_sys.current_temp));
    draw_main_card(0, 2, 2, 117, 42, CARD_BG_TEMP, buf, UI_WARN,
                   g_sys.selected_item == 0);

    /* 鈹鈹 鍗2 婀垮害 117x42 @(121,2) 鈹鈹 */
    ui_fin(ui_cpy(ui_d1(buf, g_sys.current_humidity), "%"));
    draw_main_card(1, 121, 2, 117, 42, CARD_BG_HUMI, buf, UI_CYAN,
                   g_sys.selected_item == 1);

    /* 鈹鈹 鍗3 WEIGHT 117x42 @(2,46) 鈹鈹 */
    sprintf(buf, "%d", (int)fmt_weight_g());
    draw_main_card(2, 2, 46, 117, 42, CARD_BG_WEIGHT, buf, UI_PURPLE,
                   g_sys.selected_item == 2);

    /* 鈹鈹 鍗4 PTC 117x42 @(121,46) 鈹鈹 */
    sprintf(buf, "%d", (int)(g_sys.ptc_temp + 0.5f));
    draw_main_card(3, 121, 46, 117, 42, CARD_BG_PTC, buf,
                   UI_ACCENT2, g_sys.selected_item == 3);

    /* 鈹鈹 绗5鍗＄墖锛氱儤骞叉椂闂 236x43 @(2,90) 鈹鈹 */
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
    if (g_sys.run_state == STATE_HEATING || g_sys.run_state == STATE_DRYING ||
        g_sys.run_state == STATE_COOLING) {
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

static void draw_tune_live(uint8_t which);   /* 鏍″噯椤靛疄鏃舵俯搴 AIR/PTC锛堝畾涔夊湪 1300 琛岄檮杩戯級 */

void UI_DrawTempPid(void)
{
    char buf[32];
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("温度PID", UI_WARN);
    if (g_sys.temp_pid_running) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("校准后单击返回")) / 2U, 46, "校准后单击返回", COLOR_YELLOW, UI_BG);
        TFT_FillRect(40, 66, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 78, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 66, 2, 14, COLOR_WHITE);
        TFT_FillRect(198, 66, 2, 14, COLOR_WHITE);
        uint8_t pct = g_sys.temp_pid_progress;
        TFT_FillRect(42, 68, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 86, buf, UI_TEXT, UI_BG, 2);
    } else {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("单击开始校准")) / 2U, 46, "单击开始校准", COLOR_YELLOW, UI_BG);
        ui_pid_line(buf, g_sys.params.pid_air_kp, g_sys.params.pid_air_ki, g_sys.params.pid_air_kd);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 86, buf, COLOR_CYAN, UI_BG, 2);
    }
    draw_tune_live(3);}

void UI_DrawTimeAdjust(void)
{
    char buf[8];
    uint8_t i;
    const uint16_t card_bg = TIME_CARD_BG;   /* 姣 UI_CARD 鎻愪寒涓鐐癸紝涓庨〉闈㈡繁鑹插簳鍖哄垎銆佸眬閮ㄥ埛鏂板悓鑹 */
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置时间", UI_ACCENT);
    /* 闈㈡澘鎸夋暟瀛(ink 52..79, 涓蹇65.5)鍨傜洿灞呬腑锛46..86 */
    fill_round_rect(8, 46, 224, 40, card_bg, 8);
    for (i = 0; i < 6; i++) {
        sprintf(buf, "%d", g_sys.time_digits[i]);
        TFT_DrawString(time_digit_x[i], 52, buf, COLOR_CYAN, card_bg, 4);
        if (g_sys.time_cursor == i) {
            /* 鍏夋爣妗嗭細5x7 瀛楁ā scale4 澧ㄨ抗=52..79(28px)锛岄《妗50-51/搴曟80-81 绱ц创澧ㄨ抗涓婁笅缂 */
            draw_frame(time_digit_x[i] - 2, 50, 24, 32, g_sys.time_edit_active ? COLOR_ORANGE : UI_ACCENT2);
        }
    }
    /* 鍐掑彿涓ょ偣锛氫腑蹇冨瑰噯鏁板瓧 ink 涓鐐 65.5锛55..62 / 67..74锛 */
    TFT_FillRect(78, 55, 8, 8, UI_ACCENT2);
    TFT_FillRect(78, 67, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 55, 8, 8, UI_ACCENT2);
    TFT_FillRect(152, 67, 8, 8, UI_ACCENT2);
    /* 搴曢儴灞呬腑鏄剧ず褰撳墠棰勮惧悕绉帮細褰撳墠鐑樺共棰勮�井句负锛殮XXX锛堟爣绛剧敤榛勮壊锛屾殫搴曟竻鏅帮級 */
    {
        static const char *lbl = "当前烘干预设为:";
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
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("校准后单击返回")) / 2U, 46, "校准后单击返回", COLOR_YELLOW, UI_BG);
        TFT_FillRect(40, 66, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 78, 160, 2, COLOR_WHITE);
        TFT_FillRect(40, 66, 2, 14, COLOR_WHITE);
        TFT_FillRect(198, 66, 2, 14, COLOR_WHITE);
        uint8_t pct = g_sys.pid_autotune_progress;
        TFT_FillRect(42, 68, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
        sprintf(buf, "%d%%", pct);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 86, buf, UI_TEXT, UI_BG, 2);
    }     else {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("单击开始校准")) / 2U, 46, "单击开始校准", COLOR_YELLOW, UI_BG);
        ui_pid_line(buf, g_sys.params.pid_ntc_kp, g_sys.params.pid_ntc_ki, g_sys.params.pid_ntc_kd);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 86, buf, COLOR_CYAN, UI_BG, 2);
    }
    draw_tune_live(3);
}

/* PID 璋冩暣椤垫暟鎹婧愶細浠 PTC 椤佃繘鍏(pid_return_screen=1)缂栬緫鍏冧欢 PID锛屼粠娓╁害椤佃繘鍏ョ紪杈戠┖姘 PID銆
 * 浠ュ墠鎭掔敤 pid_air_*锛孭TC 鏍″噯鍐欏叆 pid_ntc_* 鍚庨〉闈浠嶆樉绀虹┖姘旈粯璁ゅ硷紝"看起来没写入"銆 */
static float *pid_row_val(uint8_t row)
{
    if (g_sys.pid_return_screen) {
        return row == 0 ? &g_sys.params.pid_ntc_kp : row == 1 ? &g_sys.params.pid_ntc_ki : &g_sys.params.pid_ntc_kd;
    }
    return row == 0 ? &g_sys.params.pid_air_kp : row == 1 ? &g_sys.params.pid_air_ki : &g_sys.params.pid_air_kd;
}

/* PID 璋冩暣椤碉細KP/KI/KD 涓変釜鍊肩洿鎺ユ樉绀猴紝鍗曞嚮閫変腑鍚庢棆杞鐩存帴鏀癸紙姝ヨ繘0.1+鍔犻燂級锛
 * 鍐嶅崟鍑婚鍑鸿ラ」骞跺悗鍙颁繚瀛橈紱绗4椤"返回"閫鍥炴潵婧愰〉銆 */
static void pid_adj_draw_row(uint8_t i)
{
    char buf[24];
    uint16_t y = (uint16_t)(42 + (uint16_t)i * 18);
    if (i == 0) ui_pid_row1(buf, "KP:", *pid_row_val(0));
    else if (i == 1) ui_pid_row1(buf, "KI:", *pid_row_val(1));
    else ui_pid_row1(buf, "KD:", *pid_row_val(2));
    if (g_sys.selected_item == i || (i < 3U && g_sys.pid_edit_active == (uint8_t)(i + 1))) {
        TFT_FillRect(10, y, 130, 16, UI_ACCENT);
        TFT_DrawStringZh(10, y, buf, UI_TEXT, UI_ACCENT);
    } else {
        TFT_FillRect(10, y, 130, 16, UI_BG);
        TFT_DrawStringZh(10, y, buf, COLOR_CYAN, UI_BG);
    }
}

static void pid_adj_draw_back(void)
{
    if (g_sys.selected_item == 3) {
        TFT_FillRect(10, 96, 44, 16, UI_ACCENT);
        TFT_DrawStringZh(10, 96, "返回", UI_TEXT, UI_ACCENT);
    } else {
        TFT_FillRect(10, 96, 44, 16, UI_BG);
        TFT_DrawStringZh(10, 96, "返回", UI_TEXT_DIM, UI_BG);
    }
}

static void pid_adj_draw_sel(uint8_t i)
{
    if (i < 3U) pid_adj_draw_row(i);
    else pid_adj_draw_back();
}

void UI_DrawPidAdjust(void)
{
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("PID调整", COLOR_YELLOW);
    for (i = 0; i < 3U; i++) pid_adj_draw_sel(i);
    pid_adj_draw_back();
}

/* 棰勮�剧紪杈憫椤垫寚閽� */
static Preset_t *ui_preset_ptr(void)
{
    return &g_sys.params.presets[g_sys.preset_edit_idx];
}

/* 纭璁ゅ脊绐楋紙YES/NO 鍏夋爣閫夋嫨锛涙槸/鍚﹀瓧妯℃湭鎻愪緵鍓嶇敤 ASCII锛 */
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
        TFT_DrawStringZh((uint16_t)(px + 20 + (64U - zh_str_width("是")) / 2U), (uint16_t)(py + 55), "是", UI_TEXT, COLOR_GREEN);
        TFT_DrawStringZh((uint16_t)(px + 96 + (64U - zh_str_width("否")) / 2U), (uint16_t)(py + 55), "否", UI_TEXT_DIM, UI_CARD);
    } else {
        TFT_DrawStringZh((uint16_t)(px + 20 + (64U - zh_str_width("是")) / 2U), (uint16_t)(py + 55), "是", UI_TEXT_DIM, UI_CARD);
        fill_round_rect((uint16_t)(px + 96), (uint16_t)(py + 52), 64, 22, UI_ACCENT, 7);
        TFT_DrawStringZh((uint16_t)(px + 96 + (64U - zh_str_width("否")) / 2U), (uint16_t)(py + 55), "否", UI_TEXT, UI_ACCENT);
    }
}

/* 棰勮惧垪琛ㄥ崟琛岀粯鍒讹紙浜岀骇鍒楄〃椤碉細棰勮捐 + 鏈灏鹃鍑鸿岋級 */
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
    } else {   /* 閫�鍑汉� */
        if (g_sys.selected_item == i) {
            fill_round_rect(8, y, 224, 18, UI_ACCENT, 7);
            TFT_DrawStringZh(18, (uint16_t)(y + 1), "退出", UI_TEXT, UI_ACCENT);
        } else {
            TFT_FillRect(8, y, 224, 18, UI_BG);
            TFT_DrawStringZh(18, (uint16_t)(y + 1), "退出", UI_TEXT_DIM, UI_BG);
        }
    }
}

/* 棰勮惧垪琛ㄥ彲瑙嗗尯鏁村尯閲嶇粯锛堟粴鍔ㄧ敤锛屼笉鏁村睆鍒锋柊锛 */
static void preset_redraw_viewport(void)
{
    uint8_t i, cnt, so;
    cnt = (uint8_t)(g_sys.params.preset_count + 1);
    so = (uint8_t)(g_sys.pixel_offset / 20);
    TFT_FillRect(0, 36, TFT_WIDTH, 100, UI_BG);
    for (i = so; i < cnt; i++) draw_preset_row(i);
    draw_scrollbar(cnt, 5, so, 36, 100);
}

/* 鑰楁潗棰勮句富鑿滃崟椤碉細缂栬緫棰勮�� / 鏂板缓棰勮�� / 鍒犻櫎棰勮�� / 閫�鍑� */
static void preset_menu_draw_row(uint8_t i)
{
    static const char *const kRows[4] = {"编辑预设", "新建预设", "删除预设", "退出"};
    uint16_t y = (uint16_t)(36 + (uint16_t)i * 20);
    if (g_sys.selected_item == i) {
        fill_round_rect(8, y, 224, 18, UI_ACCENT, 7);
        TFT_DrawStringZh(18, (uint16_t)(y + 1), kRows[i], UI_TEXT, UI_ACCENT);
    } else {
        TFT_FillRect(8, y, 224, 18, UI_BG);
        TFT_DrawStringZh(18, (uint16_t)(y + 1), kRows[i], UI_TEXT_DIM, UI_BG);
    }
}

void UI_DrawPresetMenu(void)
{
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("烘干预设", UI_ACCENT);
    for (i = 0; i < 4; i++) preset_menu_draw_row(i);
}

/* 棰勮句簩绾у垪琛ㄩ〉锛坣ormal=缂栬緫/闀挎寜鍒囨崲锛沝el_mode=鍒犻櫎锛 */
void UI_DrawPreset(void)
{
    TFT_FillScreen(UI_BG);
    if (g_sys.preset_del_mode) draw_page_title_zh("删除预设", UI_ACCENT2);
    else draw_page_title_zh("编辑预设", UI_ACCENT);
    preset_redraw_viewport();
    if (g_sys.preset_confirm == 1) draw_confirm_popup("确定删除预设");
}

/* 棰勮�剧紪杈憫椤碉紙鑼锛氬悕绉�/娓╁害/鏃堕棿/閫�鍑汉锛泃emp/time 鐢ㄥ脊绐楋紱鍚嶇О浠呮柊寤哄彲缂栬緫锛 */
static void preset_edit_draw_rows(void)
{
    char buf[24];
    uint16_t y;
    Preset_t *p = ui_preset_ptr();
    TFT_FillRect(0, 36, TFT_WIDTH, 86, UI_BG);
    y = 36;
    /* 琛屽厜鏍囷細閫変腑鐨勮屾暣琛岄珮浜 */
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
        /* 鍚嶇О涓哄敮涓韬浠斤紝闈炴柊寤烘椂鏈琛屽厜鏍囦笉鍙閫夛紝涓嶅啀鐢"(内置)"澶氫綑鏍囪 */
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
    /* 閫�鍑汉锛氫笌鍙傛暟鑼宸﹀�归綈锛圶 涓鑷达級锛涗粎鍦ㄧ紪杈戦〉鍐呬綔涓虹4琛 */
    {
        uint16_t by = y;
        TFT_FillRect(10, by, 220, 18, (g_sys.preset_row == 3) ? UI_ACCENT : UI_BG);
        if (g_sys.preset_row == 3) {
            TFT_DrawStringZh(10, by, "退出", UI_TEXT, UI_ACCENT);
        } else {
            TFT_DrawStringZh(10, by, "退出", UI_TEXT_DIM, UI_BG);
        }
    }
}

void UI_DrawPresetEdit(void)
{
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("预设编辑", UI_ACCENT);
    preset_edit_draw_rows();
    if (g_sys.preset_confirm == 2) draw_confirm_popup("确定保存");
}

/* 棰勮�剧紪杈憫椤碉紙鑼锛歂AME/TEMP/TIME/APPLY锛泃emp/time 鐢ㄥ脊绐楋紱NAME 瀛楃﹀厜鏍囩紪杈戯級 */
/* 棰勮 temp/time 寮圭獥 */
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
    } else {           /* 鏃堕棿 6 浣嶏細鏍峰紡瀵归綈鐑樺共鏃堕棿鍗′簩绾ц彍鍗曪紙鎻忚竟鍏夋爣+灞呬腑鏁板瓧+鍐掑彿锛 */
        uint8_t x;
        const uint16_t pop_card = TIME_CARD_BG;   /* 闅忎富棰槩樺垏鎹锛涗寒=杩戠櫧銆佹殫=娣辩伆钃濓紝涓嶅啀榛戝簳鐧藉瓧 */
        uint16_t lab_col = (g_sys.theme == 0) ? UI_TEXT : TFT_COLOR(0xEC, 0xEF, 0xF1);
        TFT_DrawString((TFT_WIDTH - 4U * 12U) / 2U, (uint16_t)(py + 9), "TIME", lab_col, UI_CARD, 2);
        /* 鏃堕棿琛搴曟寜鏁板瓧澧ㄨ抗 py+42..py+62 鐨 Y 涓蹇(py+52)鍨傜洿灞呬腑锛氭爮楂38 鈫 y=py+33 */
        fill_round_rect((uint16_t)(px + 8), (uint16_t)(py + 33), (uint16_t)(pw - 16), 38, pop_card, 6);
        for (x = 0; x < 6; x++) {
            uint16_t dx = (uint16_t)(px + 16 + x * 22 + ((x >= 2) ? 8 : 0) + ((x >= 4) ? 8 : 0));
            uint16_t dy = (uint16_t)(py + 42);
            char ch[2] = {(char)('0' + g_sys.time_digits[x]), 0};
            uint16_t frame_col;
            /* 鍏夋爣妗嗭細5x7 瀛楁ā scale3 澧ㄨ抗=dy..dy+20(21px)锛岄《妗哾y-2..dy-1/搴曟哾y+21..dy+22 绱ц创澧ㄨ抗 */
            if (g_sys.preset_time_cur != x) frame_col = 0xFFFF;
            else frame_col = g_sys.preset_time_edit ? COLOR_ORANGE : UI_ACCENT2;
            TFT_DrawString(dx, dy, ch, COLOR_CYAN, pop_card, 3);
            if (frame_col != 0xFFFF) draw_frame((uint16_t)(dx - 2), (uint16_t)(dy - 2), 19, 25, frame_col);
        }
        /* 鍐掑彿锛堟椂:鍒:绉 涓ょ粍锛夛細灞呬腑浜庣粍闂撮殭锛屽瀭鐩村瑰噯鏁板瓧涓蹇 */
        {
            uint16_t cy = (uint16_t)(py + 45);
            uint16_t cx1 = (uint16_t)(px + 58), cx2 = (uint16_t)(px + 110);
            TFT_FillRect(cx1, (uint16_t)(cy + 4), 6, 5, UI_ACCENT2);
            TFT_FillRect(cx1, (uint16_t)(cy + 12), 6, 5, UI_ACCENT2);
            TFT_FillRect(cx2, (uint16_t)(cy + 4), 6, 5, UI_ACCENT2);
            TFT_FillRect(cx2, (uint16_t)(cy + 12), 6, 5, UI_ACCENT2);
        }
    }
}

/* PID 鏍″噯椤靛疄鏃舵俯搴︼細AIR/PTC 鎺掑湪 y=108 琛岋紙涓ら〉甯冨眬宸茬粺涓锛涙棫鐗堟斁鍦 y=138 瓒呭嚭
 * 135px 鍙瑙佸尯鎵浠ョ湅涓嶈侊級锛0.1鈩 鍙樺寲鎵嶅眬閮ㄩ噸缁樸 */
static void draw_tune_live(uint8_t which)
{
    char buf[24];
    /* which: 1=AIR(左) 2=PTC(右) 3=两者；对称分布，仅刷新变化的一侧，避免整行闪烁 */
    if (which & 1U) {
        uint16_t w, x;
        ui_fin(ui_cpy(ui_d1(ui_cpy(buf, "AIR "), g_sys.current_temp), " C"));
        w = (uint16_t)strlen(buf) * 6U; x = (uint16_t)(60U - w / 2U); if (x < 6U) x = 6U;
        TFT_FillRect(4, 106, 112, 13, UI_BG);
        TFT_DrawString(x, 108, buf, COLOR_CYAN, UI_BG, 1);
    }
    if (which & 2U) {
        uint16_t w, x;
        ui_fin(ui_cpy(ui_d1(ui_cpy(buf, "PTC "), g_sys.ptc_temp), " C"));
        w = (uint16_t)strlen(buf) * 6U; x = (uint16_t)(180U - w / 2U); if (x < 126U) x = 126U;
        TFT_FillRect(124, 106, 112, 13, UI_BG);
        TFT_DrawString(x, 108, buf, COLOR_GREEN, UI_BG, 1);
    }
}

/* PID 鏍″噯椤佃繘搴︽潯灞閮ㄥ埛鏂帮紙涓ら〉鍚屽竷灞锛氭潯 y=68銆佺櫨鍒嗘瘮 y=86锛夛紝閬垮厤鏁村睆闂鐑 */
static void refresh_autotune_bar(uint8_t which, uint8_t pct)
{
    char buf[8];
    (void)which;
    TFT_FillRect(42, 68, 156, 10, UI_BG);
    if (pct) TFT_FillRect(42, 68, (uint16_t)(156U * pct / 100U), 10, COLOR_GREEN);
    sprintf(buf, "%d%%", pct);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, 86, buf, UI_TEXT, UI_BG, 2);
}

/* PID 璋冩暣椤靛崟琛屽埛鏂帮細缂栬緫涓瀹炴椂鏇存柊瀵瑰簲琛 */
static void refresh_pid_row(uint8_t row)
{
    char buf[24];
    uint16_t y;
    if (row >= 3) return;
    y = (uint16_t)(42 + row * 18);
    if (row == 0) ui_pid_row1(buf, "KP:", *pid_row_val(0));
    else if (row == 1) ui_pid_row1(buf, "KI:", *pid_row_val(1));
    else ui_pid_row1(buf, "KD:", *pid_row_val(2));
    TFT_FillRect(10, y, 130, 16, UI_BG);
    if (g_sys.pid_edit_active == (uint8_t)(row + 1)) {
        TFT_FillRect(10, y, 130, 16, UI_ACCENT);
        TFT_DrawStringZh(10, y, buf, UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, y, buf, COLOR_CYAN, UI_BG);
    }
}

static const char *const kMenuItems[] = {"WiFi", "电机", "关于", "CAN", "设置", "音乐", "重启", "恢复出厂设置", "退出"};
#define MENU_ITEM_COUNT   9u
#define MENU_ROW_H        18u
#define MENU_ROW_GAP      20u
#define MENU_TOP_Y        32u

/* 缁樺埗鍗曚釜鑿滃崟琛岋細閫変腑=钃濆簳鐧藉瓧+鍦嗚掕疆寤擄紝鏈閫変腑=鐏板瓧銆
 * 浣嶇疆鐢 pixel_offset 鍐冲畾锛屼緵鍙瑙嗗尯缁樺埗璋冪敤銆 */
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

/* 鑿滃崟鍙瑙嗗尯鏁村尯閲嶇粯锛堟粴鍔ㄧ敤锛屼笉鏁村睆鍒锋柊锛 */
static void menu_redraw_viewport(void)
{
    uint8_t i;
    TFT_FillRect(0, MENU_VIEW_Y, 233, MENU_VIEW_H, UI_BG);
    for (i = 0; i < MENU_ITEM_COUNT; i++) draw_menu_row(i, i == g_sys.selected_item);
    draw_scrollbar(MENU_ITEM_COUNT, 5, (uint8_t)(g_sys.pixel_offset / MENU_ROW_H2), MENU_VIEW_Y, MENU_VIEW_H);
}

/* 鑿滃崟缂栫爜鍣ㄦ粴鍔锛氭洿鏂伴変腑绱㈠紩(寰鐜)涓 pixel_offset锛岄噸缁樺彲瑙嗗尯 */
void UI_MenuScroll(int dir)
{
    int16_t target, max_off;
    uint8_t old_sel = g_sys.selected_item;
    uint8_t off_old = (uint8_t)(g_sys.pixel_offset / MENU_ROW_H2);
    g_sys.selected_item = EncWrap(MENU_ITEM_COUNT, (int8_t)dir, g_sys.selected_item);
    target = (int16_t)g_sys.selected_item * MENU_ROW_H2;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + MENU_ROW_H2 > g_sys.pixel_offset + MENU_VIEW_H) g_sys.pixel_offset = (int16_t)(target + MENU_ROW_H2 - MENU_VIEW_H);
    max_off = (int16_t)(MENU_ITEM_COUNT * MENU_ROW_H2 - MENU_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    /* Viewport shifted -> redraw whole viewport; else only old+new row (no full-area refresh flicker) */
    if ((uint8_t)(g_sys.pixel_offset / MENU_ROW_H2) == off_old && old_sel != g_sys.selected_item) {
        draw_menu_row(old_sel, 0);
        draw_menu_row(g_sys.selected_item, 1);
    } else {
        menu_redraw_viewport();
    }
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

/* 寮鍏虫粦鍧楁寜閽锛46x16 鍦嗚掕嵂涓革紝涓庤岄珮16瀵归綈銆傚紑=鏁翠綋濉鍏呰壊+鐧借壊鍦嗛挳闈犲彸锛涘叧=鐏板簳+鐧借壊鍦嗛挳闈犲乏 */
static void draw_toggle(uint16_t x, uint16_t y, uint8_t on)
{
    uint16_t w = 42, h = 16, k = 12;
    fill_round_rect(x, y, w, h, on ? COLOR_GREEN : UI_CARD_EDGE, 8);
    if (on) fill_round_rect((uint16_t)(x + w - k - 2), (uint16_t)(y + 2), k, (uint16_t)(h - 4), COLOR_WHITE, 6);
    else    fill_round_rect((uint16_t)(x + 2), (uint16_t)(y + 2), k, (uint16_t)(h - 4), COLOR_WHITE, 6);
}

/* 寮鍏虫粦鍔ㄥ姩鐢荤増锛歛nim=0 闈欐锛1~6 甯т粠鍏斥啋寮婊戣岋紱7~12 甯т粠寮鈫掑叧婊戣屻
 * 婊戝潡浣嶇疆鎸夊抚绾挎ф彃鍊硷紝瑙嗚変负婊戝潡骞虫粦婊戝姩锛堢害 6脳30ms鈮180ms 瀹屾垚锛夈 */
static void draw_toggle_animated(uint16_t x, uint16_t y, uint8_t on, uint8_t anim)
{
    uint16_t w = 42, h = 16, k = 12;
    uint16_t knob_x;
    fill_round_rect(x, y, w, h, on ? COLOR_GREEN : UI_CARD_EDGE, 8);
    if (anim == 0) {
        knob_x = on ? (uint16_t)(x + w - k - 2) : (uint16_t)(x + 2);
    } else {
        uint8_t fr = anim;   /* 1..12 */
        uint8_t off_dir = (fr > 6) ? 1 : 0;          /* 1-6: 鍏斥啋寮锛7-12: 寮鈫掑叧 */
        uint8_t f = (uint8_t)(off_dir ? (fr - 6) : fr);
        uint16_t x0 = off_dir ? (uint16_t)(x + w - k - 2) : (uint16_t)(x + 2);
        uint16_t x1 = off_dir ? (uint16_t)(x + 2) : (uint16_t)(x + w - k - 2);
        uint16_t span = (uint16_t)(w - k - 4);
        knob_x = (uint16_t)(x0 + ((uint32_t)(x1 - x0) * f) / 6U);
        (void)span;
    }
    fill_round_rect(knob_x, (uint16_t)(y + 2), k, (uint16_t)(h - 4), COLOR_WHITE, 6);
}

/* 鐢垫満椤佃屾枃鏈锛堝叏瀹介噸寤 鈫 閫熷害rpm闀垮害鑷閫傚簲锛岀紪杈戞椂鏃犳畫鐣欙紱寮/鍏抽」鍚︾姸鎭渚涘彉鏇存ｆ祴锛� */
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
    if (!is_tmc && slot >= 8) slot += 1;   /* A4988 无电流项: 跳过 slot 8 */
    switch (slot) {
    case 0: sprintf(buf, "联动:%s", g_sys.params.motor_enabled ? "开" : "关"); break;
    case 1: sprintf(buf, "方向:%s", g_sys.params.motor_direction ? "反转" : "正转"); break;
    case 2: sprintf(buf, "速度:%drpm", g_sys.params.motor_speed); break;
    case 3: sprintf(buf, "摆动:%s", g_sys.params.motor_oscillate ? "开" : "关"); break;
    case 4: sprintf(buf, "角度:%d°", g_sys.params.motor_oscillate_angle); break;
    case 5: sprintf(buf, "次数:%d", g_sys.params.motor_work_count); break;
    case 6: sprintf(buf, "休息:%ds", g_sys.params.motor_rest_sec); break;
    case 7: sprintf(buf, "驱动:%s", drv); break;
    case 8: ui_fin(ui_cpy(ui_tenths(ui_cpy(buf, "电流:"), g_sys.params.motor_current), "A")); break;
    case 9: sprintf(buf, "静音:%s", g_sys.params.motor_stealthchop ? "开" : "关"); break;
    default: buf[0] = 0; break;
    }
}

/* 鐢垫満琛屾槸鍚︿负寮鍏抽」锛堣仈鍔/鎽嗗姩/闈欓煶锛 */
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

/* TMC 閫氳璇婃柇缂撳瓨锛氳繘鍏ョ數鏈洪〉/姣忕掕皟鐢 Stepper_TmcProbe 鏇存柊锛岄伩鍏嶆瘡琛岀粯鍒堕兘闃诲炶 UART銆
 * ok=1 閫氳鎴愬姛锛宑hip=IC 鐗堟湰(0x2208/0x2209/0x? )锛沷k=0 鏃朵繚鐣欎笂娆¤绘暟渚涙帓鏌ャ */
/* TMC 通讯诊断缓存: 每 1s 轻量单读 IFCNT, 每 5s 做一次完整 Probe(读→无害写→读,
 * 拿 IFCNT 计数); 步进中一律跳过、沿用旧值。g_tmc_ver=最近一次 Probe 回报的计数。 */
static uint8_t  g_tmc_ok  = 0;
static uint32_t g_tmc_ver = 0;

static void tmc_probe_update(void)
{
    uint32_t ver = 0;
    uint8_t ok;
    static uint32_t s_last_full = 0;
    uint32_t now;
    if (Stepper_IsRunning()) return;             /* 步进中禁做 UART 事务(丢步): 沿用旧值 */
    now = SystemTime_Millis();
    if ((uint32_t)(now - s_last_full) >= 5000U) {
        s_last_full = now;
        ok = Stepper_TmcProbe(&ver);
        if (ok) { g_tmc_ok = 1; g_tmc_ver = ver; }
        else    { g_tmc_ok = 0; }
    } else {
        g_tmc_ok = Stepper_TmcComOk();           /* 轻量级: 单帧校验应答 */
    }
}

/* 缁樺埗/鍒锋柊搴曢儴璇婃柇琛岋紙scale1锛孉SCII锛夈侷FCNT 姣忕掗掑 鈬 鑺鐗囩‘瀹炲湪搴旂瓟銆 */
static void tmc_draw_diag(void)
{
    char buf[28];
    uint16_t fg = g_tmc_ok ? COLOR_GREEN : COLOR_RED;
    /* 屏是 240x135(TFT_HEIGHT=135): 旧坐标 y=208 整条在屏外(2208 也永远看不到)。
     * 改放"电机设置"标题行右侧徽章区 x156..239: 短文本, IFCNT 是 4bit 计数(0..15)。 */
    TFT_FillRect(156, 8, 84, 15, UI_BG);
    if (g_tmc_ok) sprintf(buf, "TMC OK n=%u", (unsigned)(g_tmc_ver & 0xFU));
    else {
        extern uint8_t Stepper_TmcErr(void);
        extern uint16_t Stepper_TmcRaw16(void);
        extern uint8_t Stepper_TmcLineIdle(void);
        sprintf(buf, "TMC F%u %04X%c", (unsigned)Stepper_TmcErr(),
                (unsigned)Stepper_TmcRaw16(),
                Stepper_TmcLineIdle() ? 'H' : 'L');
    }
    TFT_DrawString(158, 12, buf, fg, UI_BG, 1);
}

/* 鐢垫満椤靛崟琛岀粯鍒讹細寮/鍏抽」鐢绘粦鍧楋紝鍏朵綑鐢绘枃鏈 */
static void motor_draw_row(uint8_t i, uint16_t y, uint8_t selected)
{
    char buf[32];
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;
    uint8_t slot = i;
    if (!is_tmc && slot >= 8) slot += 1;   /* A4988 无电流项: 跳过 slot 8 */
    motor_row_str(i, buf);
    if (selected) TFT_FillRect(5, y, 220, 16, UI_ACCENT);
    else TFT_FillRect(5, y, 220, 16, UI_BG);
    if (i >= count - 1) {
        /* 閫�鍑汉鑼锛氭寜鏂囨滅粯鍒� */
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

/* 鐢垫満椤靛崟琛屽眬閮ㄥ埛鏂帮細娓呭叏琛屽啀缁橈紙寮鍏虫粦鍧/鏁板奸兘瀹炴椂鏇存柊锛 */
static void refresh_motor_row(uint8_t i)
{
    uint16_t y = (uint16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
    motor_draw_row(i, y, (g_sys.selected_item == i));
}

/* 鐢垫満椤靛彲瑙嗗尯鏁村尯閲嶇粯锛堟粴鍔ㄧ敤锛屼笉鏁村睆鍒锋柊锛 */
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

/* 鐢垫満椤电紪鐮佸櫒婊氬姩锛氭洿鏂伴変腑绱㈠紩(寰鐜)涓 pixel_offset锛岄噸缁樺彲瑙嗗尯 */
void UI_MotorScroll(int dir)
{
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;
    int16_t target, max_off;
    uint8_t old_sel = g_sys.selected_item;
    uint8_t off_old = (uint8_t)(g_sys.pixel_offset / SCR_ROW_H);
    g_sys.selected_item = EncWrap(count, (int8_t)dir, g_sys.selected_item);
    target = (int16_t)g_sys.selected_item * SCR_ROW_H;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + SCR_ROW_H > g_sys.pixel_offset + SCR_VIEW_H) g_sys.pixel_offset = (int16_t)(target + SCR_ROW_H - SCR_VIEW_H);
    max_off = (int16_t)(count * SCR_ROW_H - SCR_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    /* 视口未移位: 只重绘旧+新两行, 不整视区刷新不闪 */
    if ((uint8_t)(g_sys.pixel_offset / SCR_ROW_H) == off_old && old_sel != g_sys.selected_item) {
        int16_t y0 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)old_sel * SCR_ROW_H);
        int16_t y1 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)g_sys.selected_item * SCR_ROW_H);
        if (y0 >= (int16_t)SCR_VIEW_Y && y0 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) motor_draw_row(old_sel, (uint16_t)y0, 0);
        if (y1 >= (int16_t)SCR_VIEW_Y && y1 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) motor_draw_row(g_sys.selected_item, (uint16_t)y1, 1);
    } else {
        motor_redraw_viewport();
    }
}

void UI_DrawMotorAdjust(void)
{
    uint8_t is_tmc = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                      g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
    uint8_t count = is_tmc ? 11 : 9;

    TFT_FillScreen(UI_BG);
    draw_page_title_zh("电机设置", UI_ACCENT);

    /* 鍙瑙嗗尯锛氭寜 pixel_offset 缁樺埗鍏ㄩ儴椤瑰苟瑁佸壀鍒拌嗗尯 */
    for (uint8_t i = 0; i < count; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        motor_draw_row(i, (uint16_t)y, (g_sys.selected_item == i));
    }
    draw_scrollbar(count, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);

    /* TMC 驱动：标题行右上角通讯状态徽章(240x135 屏, 底部无空间 → 见 tmc_draw_diag) */
    {
        if (is_tmc) {
            tmc_draw_diag();
        } else {
            TFT_FillRect(156, 8, 84, 15, UI_BG);
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

/* WiFi 椤碉細琛0=WiFi寮鍏 1=AP璁剧疆(寮閰嶇綉鐑鐐) 2=閲嶆柊璁剧疆(娓5缁+閰嶇綉) 3=閫�鍑汉�
 * 鈥滈厤/鐑鈥濈瓑瀛楁ā鏈鍏ュ簱锛屾晠鐢ㄥ瓧搴撶幇鏈夊瓧+ASCII 缁勬爣绛撅紱琛ュ瓧妯″悗鍙鏀瑰洖鈥滈厤缃/閲嶆柊閰嶇綉鈥濄 */
#define WIFI_ROWS 4
static void wifi_draw_row(uint8_t i, uint16_t y, uint8_t selected);

void UI_DrawWiFiScreen(void)
{
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("WiFi设置", UI_ACCENT);
    for (i = 0; i < WIFI_ROWS; i++)
        wifi_draw_row(i, (uint16_t)(30 + i * 26), (uint8_t)(g_sys.selected_item == i ? 1 : 0));
}

static void wifi_draw_row(uint8_t i, uint16_t y, uint8_t selected)
{
    static const char *labels[WIFI_ROWS] = {"WiFi", "AP设置", "重新设置", "退出"};
    const char *ip = EspLink_IP();
    uint16_t bg = selected ? UI_ACCENT : UI_BG;
    uint16_t lw = zh_str_width(labels[i]);
    TFT_FillRect(5, y, 230, 18, bg);
    TFT_DrawStringZh(8, y + 1, labels[i],
                     (i == 0 && g_sys.wifi_enabled && !selected) ? COLOR_GREEN : UI_FG, bg);
    if (i == 0) {
        draw_toggle(190, y + 1, g_sys.wifi_enabled);
        if (g_sys.wifi_enabled) {
            TFT_FillRect((uint16_t)(8 + lw + 6), y + 1, (uint16_t)(190 - 8 - lw - 8), 16, bg);
            switch (EspLink_State()) {
            case ESPLINK_BOOT:
            case ESPLINK_CONNECTING:
                TFT_DrawString((uint16_t)(8 + lw + 6), y + 5, "...", UI_FG_DIM, bg, 1); break;
            case ESPLINK_ONLINE:
                TFT_DrawString((uint16_t)(8 + lw + 6), y + 5, ip, (uint16_t)(selected ? UI_FG : COLOR_GREEN), bg, 1); break;
            case ESPLINK_CONFIG:
                TFT_DrawString((uint16_t)(8 + lw + 6), y + 5, "AP!", (uint16_t)(selected ? UI_FG : COLOR_CYAN), bg, 1); break;
            default: break;
            }
        }
    }
}

static uint8_t ota_screen_drawn = 0;

/* 閲嶆柊杩涘叆 OTA 鐣岄潰鏃惰皟鐢锛屼笅娆 UI_DrawOTAScreen 浼氭暣灞忛噸鐢 */
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

    draw_btn(160, "  确定", COLOR_GREEN, 1);
    draw_frame(5, 160, 125, BTN_H, UI_ACCENT);
}

/* 鍓嶇疆澹版槑锛氳剧疆椤垫暟鍊兼枃鏈鐢熸垚锛屼緵鏁村睆缁樺埗涓庡崟琛屽疄鏃跺埛鏂板叡鐢 */
static void settings_row_str(uint8_t i, char *buf);

static void lang_popup_draw(void);   /* 语言字库上传弹窗(设置页) */

/* 璁剧疆椤靛崟琛岀粯鍒讹紙鏍囩+楂樹寒+鍊/寮鍏虫粦鍧楋級 */
static void settings_draw_row(uint8_t i, uint16_t y)
{
    static const char *kLabels[] = {"蜂鸣器联动","蜂鸣器音量","灯光开关","背光","主题","熄屏","RGB灯带","更新字库","退出"};
    char buf[32];
    TFT_FillRect(10, y, 95, 16, UI_BG);
    if (i == g_sys.selected_item) {
        TFT_FillRect(10, y, 95, 16, UI_ACCENT);
        TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT_DIM, UI_BG);
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

/* RGB 浜搴﹀脊绐楋細鎸囩ず鐏�/鐏�鍏� 鍙岃繘搴︽潯 + 瀹屾垚锛泂el=0鎸囩ず鐏� 1鐏�鍏� 2瀹屾垚
 * 鍏夋爣=閫変腑琛屽渾瑙掕疆寤擄紙鏁磋岋紝鍚︽爣�/杩涘害鏉/鏁板硷級锛涘崟鍑昏繘鍏ユ暟鍊肩紪杈戯紝
 * 璇ヨ岃疆寤撳彉姗欍佹暟鍊煎尯姗欒壊楂樹寒锛堥珮浜涓庤疆寤撲繚鎸 6px 闂磋窛锛屼簰涓嶅共娑夛級銆 */
#define RGB_POP_W    190
#define RGB_POP_H    126
#define RGB_ROW_X    8      /* 琛屾嗗乏娌(px+8) */
#define RGB_ROW_W    170    /* 琛屾嗗 */
#define RGB_BAR_X    16     /* 杩涘害鏉X(px+16) */
#define RGB_BAR_W    96     /* 杩涘害鏉″斤細鍙虫部 px+112 */
#define RGB_VAL_X    122    /* 鏁板奸珮浜鍖哄乏娌(px+122) */
#define RGB_VAL_W    52     /* 鏁板奸珮浜鍖哄斤細鍙虫部 px+174 < 琛屾嗗彸娌 px+178 */
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
        TFT_FillRect((uint16_t)(px + RGB_ROW_X), ry, RGB_ROW_W, RGB_ROW_H, UI_CARD);
        TFT_DrawStringZh((uint16_t)(px + 16), (uint16_t)(py + (item == 0 ? 12 : 52)),
                         (item == 0) ? "指示灯" : "灯光",
                         selected ? cc : UI_FG, UI_CARD);
        draw_rgb_bar(item);
        /* 杞寤撴渶鍚庣敾锛氱洊鍦ㄥ唴瀹逛箣涓婏紝涓嶄細鍐嶈鏁板/杩涘害鏉″悆鎺変竴鎴 */
        if (selected) draw_round_outline((uint16_t)(px + RGB_ROW_X), ry, RGB_ROW_W, RGB_ROW_H, cc, 6, 2);
    }
    /* 完成 */
    if (sel == 2) {
        fill_round_rect((uint16_t)(px + 30), (uint16_t)(py + 96), 130, 22, COLOR_GREEN, 8);
        TFT_DrawStringZh((uint16_t)(px + 30 + (130U - zh_str_width("完成")) / 2U), (uint16_t)(py + 100), "完成", UI_FG, COLOR_GREEN);
    } else {
        TFT_DrawStringZh((uint16_t)(px + 30 + (130U - zh_str_width("完成")) / 2U), (uint16_t)(py + 100), "完成", UI_FG_DIM, UI_CARD);
    }
}

/* 浠呴噸缁樻煇涓琛(0=鎸囩ず鐏� 1=鐏�鍏�)鐨勮繘搴︽潯锛堝煎彉鍖栫伆搴︼紝涓嶅奖鍝嶅厜鏍囨/鏍囩撅級 */
static void draw_rgb_bar(uint8_t item)
{
    uint16_t px = rgb_pop_px();
    uint16_t py = rgb_pop_py();
    uint16_t by = (item == 0) ? (uint16_t)(py + 30) : (uint16_t)(py + 70);
    uint8_t val = (item == 0) ? g_sys.params.rgb_led_bright : g_sys.params.rgb_strip_bright;
    uint8_t selected = (g_sys.rgb_bright_sel == item);
    uint8_t editing  = (selected && g_sys.rgb_bright_edit);
    uint16_t cc = editing ? COLOR_ORANGE : COLOR_GREEN;
    char buf[8];
    TFT_FillRect((uint16_t)(px + RGB_BAR_X), by, RGB_BAR_W, 8, UI_CARD_EDGE);
    {
        uint16_t w = (uint16_t)((uint32_t)RGB_BAR_W * val / 100U);
        if (w) TFT_FillRect((uint16_t)(px + RGB_BAR_X), by, w, 8, selected ? cc : UI_ACCENT);
    }
    sprintf(buf, "%d%%", val);
    if (editing) {
        fill_round_rect((uint16_t)(px + RGB_VAL_X), (uint16_t)(by - 2), RGB_VAL_W, 13, COLOR_ORANGE, 4);
        TFT_DrawString((uint16_t)(px + RGB_VAL_X + 4), (uint16_t)(by - 1), buf, UI_FG, COLOR_ORANGE, 1);
    } else {
        TFT_DrawString((uint16_t)(px + RGB_VAL_X + 4), (uint16_t)(by - 1), buf, selected ? cc : UI_FG, UI_CARD, 1);
    }
}

void UI_DrawSettingsScreen(void)
{
    uint8_t i;
    uint8_t cnt = 9;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("设置", UI_ACCENT);
    for (i = 0; i < cnt; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        settings_draw_row(i, (uint16_t)y);
    }
    draw_scrollbar(cnt, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);
    if (g_sys.lang_popup) lang_popup_draw();
}

/* 璁剧疆椤靛彲瑙嗗尯鏁村尯閲嶇粯锛堟粴鍔ㄧ敤锛 */
static void settings_redraw_viewport(void)
{
    uint8_t cnt = 9;
    TFT_FillRect(0, SCR_VIEW_Y, 233, SCR_VIEW_H, UI_BG);
    for (uint8_t i = 0; i < cnt; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        settings_draw_row(i, (uint16_t)y);
    }
    draw_scrollbar(cnt, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);
}

/* 璁剧疆椤电紪鐮佸櫒婊氬姩锛氭洿鏂伴変腑绱㈠紩(寰鐜)涓 pixel_offset锛岄噸缁樺彲瑙嗗尯 */
void UI_SettingsScroll(int dir)
{
    uint8_t cnt = 9;
    int16_t target, max_off;
    uint8_t old_sel = g_sys.selected_item;
    uint8_t off_old = (uint8_t)(g_sys.pixel_offset / SCR_ROW_H);
    g_sys.selected_item = EncWrap(cnt, (int8_t)dir, g_sys.selected_item);
    target = (int16_t)g_sys.selected_item * SCR_ROW_H;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + SCR_ROW_H > g_sys.pixel_offset + SCR_VIEW_H) g_sys.pixel_offset = (int16_t)(target + SCR_ROW_H - SCR_VIEW_H);
    max_off = (int16_t)(cnt * SCR_ROW_H - SCR_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    /* Viewport shifted -> redraw whole viewport; else only old+new row (no full-area refresh flicker) */
    if ((uint8_t)(g_sys.pixel_offset / SCR_ROW_H) == off_old && old_sel != g_sys.selected_item) {
        int16_t y0 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)old_sel * SCR_ROW_H);
        int16_t y1 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)g_sys.selected_item * SCR_ROW_H);
        if (y0 >= (int16_t)SCR_VIEW_Y && y0 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) settings_draw_row(old_sel, (uint16_t)y0);
        if (y1 >= (int16_t)SCR_VIEW_Y && y1 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) settings_draw_row(g_sys.selected_item, (uint16_t)y1);
    } else {
        settings_redraw_viewport();
    }
}

/* 铚傞福鍣ㄩ煶閲�/灞忓箷浜搴 缂栬緫寮圭獥锛氳繘搴︽潯鍦ㄤ笂銆佸綋鍓嶅煎湪涓嬶紙瑕嗙洊浜庡綋鍓嶈彍鍗曚箣涓婏紝闈炴暣灞忥級 */
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

/* 音量/背光弹窗: 数值变化时只刷进度条+数字(卡片/边框不动, 防闪烁; 与 RGB 弹窗同策略) */
static void settings_popup_bar(uint8_t item)
{
    uint16_t pw = 170, ph = 90;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = (TFT_HEIGHT - ph) / 2;
    uint16_t m = (item == 1) ? 10U : 100U;
    uint16_t v = (item == 1) ? g_sys.buzzer_vol : g_sys.backlight;
    char buf[16];
    TFT_FillRect((uint16_t)(px + 15), (uint16_t)(py + 30), (uint16_t)(pw - 30), 10, UI_CARD_EDGE);
    {
        uint16_t w = (uint16_t)((uint32_t)(pw - 30) * v / m);
        if (w) TFT_FillRect((uint16_t)(px + 15), (uint16_t)(py + 30), w, 10, UI_ACCENT);
    }
    if (item == 1) sprintf(buf, "%d%%", v * 10U);
    else sprintf(buf, "%d%%", v);
    TFT_FillRect((uint16_t)(px + 15), (uint16_t)(py + 52), (uint16_t)(pw - 30), 20, UI_CARD);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 12U)) / 2U, (uint16_t)(py + 56), buf, UI_TEXT, UI_CARD, 2);
}

/* 璁剧疆椤垫暟鍊兼枃鏈锛堝叏瀹介噸寤猴紝瀹炴椂鍒锋柊鏃犳畫褰憋級 */
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

/* 璁剧疆椤靛崟琛屽煎尯鍒锋柊锛氱紪杈戜腑瀹炴椂鏇存柊锛堝紑鍏虫粦鍧/鏁板/寮圭獥骞跺瓨锛 */
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

/* 璁剧疆椤电紪杈戜腑鏁板煎彉鍖栨椂鍙灞閮ㄥ埛鏂拌ラ」鐨勬暟鍊兼枃鏈+杩涘害鏉★紝閬垮厤鍏ㄥ睆閲嶇粯
 * (鍏ㄥ睆閲嶇粯闃诲炰富寰鐜鈫掔紪鐮佸櫒杞璇㈣鑺傛祦鈫掑姞閫熷け鏁堬紝涓旈犳垚闂灞)銆 */
void UI_RefreshSettingsValue(void)
{
    uint8_t i = g_sys.selected_item;
    if (i != 1 && i != 3) return;
    uint16_t y = (uint16_t)(36 + (i - g_sys.scroll_offset) * 18);
    char buf[16];
    uint8_t m, v, w;
    if (i == 1) { m = 10;  v = g_sys.buzzer_vol; sprintf(buf, "%d/10", v); }
    else        { m = 100; v = g_sys.backlight;   sprintf(buf, "%d%%", v); }
    TFT_FillRect(120, y, 40, 16, UI_BG);                 /* 娓呮棫鍊 */
    TFT_DrawString(120, y, buf, UI_ACCENT, UI_BG, 2);    /* 鐢绘柊鍊 */
    w = (uint8_t)((uint16_t)v * 50U / m);
    TFT_FillRect(165, y + 2, 50, 6, UI_CARD_EDGE);       /* 进度条底 */
    if (w) TFT_FillRect(165, y + 2, w, 6, UI_ACCENT);   /* 杩涘害鏉″～鍏 */
}

/* 鈹鈹 CAN 闆嗙兢椤 鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹鈹 */
#define CAN_ROW_COUNT  5

/* CAN 椤垫暟鍊兼枃鏈 */
static void can_row_str(uint8_t i, char *buf)
{
    switch (i) {
    case 0: sprintf(buf, "%s", g_sys.params.can_enabled ? "开" : "关"); break;
    case 1: sprintf(buf, "%s", g_sys.params.can_role ? "从机" : "主机"); break;
    case 2:
        if (g_sys.params.can_role) buf[0] = 0;   /* 从机: 不显示 */
        else {
            switch (g_sys.can_search_state) {
            case 1: sprintf(buf, "搜索中"); break;
            case 2: sprintf(buf, "发现设备"); break;
            case 3: sprintf(buf, "连接中"); break;
            case 4: sprintf(buf, "连接成功"); break;
            case 5: sprintf(buf, "未发现设备"); break;
            case 6: sprintf(buf, "请重试"); break;
            default: buf[0] = 0; break;          /* 空闲: 默认不显示 */
            }
        }
        break;
    case 3:
        if (g_sys.params.can_role) buf[0] = 0;   /* 从机: 不显示 */
        else sprintf(buf, "%d", (uint16_t)g_sys.can_connected);
        break;
    default: buf[0] = 0; break;
    }
}

/* CAN 椤靛崟琛岀粯鍒讹紙鏍囩+楂樹寒+鍊/寮鍏虫粦鍧楋級 */
/* CAN 行可见性: 通讯关只显示开关+退出; 通讯开但从机只显示开关/主从/退出; 搜索/已连接仅主机 */
static uint8_t can_row_visible(uint8_t i)
{
    if (i == 0 || i == 4) return 1;                 /* 通讯开关 + 退出 恒显示 */
    if (!g_sys.params.can_enabled) return 0;        /* 通讯关: 主从/搜索/已连接隐藏 */
    if (i == 1) return 1;                            /* 主从关系: 通讯开时显示 */
    return (g_sys.params.can_role == 0);             /* 搜索设备/已连接设备: 仅主机 */
}

static void can_draw_row(uint8_t i, uint16_t y)
{
    static const char *kLabels[] = {"CAN通讯", "主从关系", "搜索设备", "已连接设备", "退出"};
    char buf[32];
    /* 通讯关/从机下隐藏对应行(整行清空) */
    if (!can_row_visible(i)) {
        TFT_FillRect(0, y, 233, SCR_ROW_H, UI_BG);
        return;
    }
    TFT_FillRect(10, y, 95, 16, UI_BG);
    if (i == g_sys.selected_item) {
        TFT_FillRect(10, y, 95, 16, UI_ACCENT);
        TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT, UI_ACCENT);
    } else {
        TFT_DrawStringZh(10, y, kLabels[i], UI_TEXT_DIM, UI_BG);
    }
    if (i < 4) {
        uint16_t vcol = (g_sys.can_edit_active && i == g_sys.selected_item) ? COLOR_ORANGE : UI_ACCENT;
        if (i == 0) {
            TFT_FillRect(132, y, 95, 16, UI_BG);
            draw_toggle(134, y, g_sys.params.can_enabled);
        } else {
            can_row_str(i, buf);
            TFT_FillRect(132, y, 95, 16, UI_BG);
            TFT_DrawStringZh(132, y, buf, vcol, UI_BG);
        }
    }
}

/* CAN 椤靛崟琛屽眬閮ㄥ埛鏂 */
static void refresh_can_row(uint8_t i)
{
    uint16_t y;
    if (i >= CAN_ROW_COUNT) return;
    y = (uint16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
    can_draw_row(i, y);
}

/* CAN 椤靛彲瑙嗗尯鏁村尯閲嶇粯锛堟粴鍔ㄧ敤锛屼笉鏁村睆鍒锋柊锛 */
static void can_redraw_viewport(void)
{
    uint8_t i;
    TFT_FillRect(0, SCR_VIEW_Y, 233, SCR_VIEW_H, UI_BG);
    for (i = 0; i < CAN_ROW_COUNT; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        can_draw_row(i, (uint16_t)y);
    }
    draw_scrollbar(CAN_ROW_COUNT, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);
}

/* CAN 椤电紪鐮佸櫒婊氬姩锛氭洿鏂伴変腑绱㈠紩(寰鐜)涓 pixel_offset锛岄噸缁樺彲瑙嗗尯 */
void UI_CanScroll(int dir)
{
    int16_t target, max_off;
    uint8_t old_sel = g_sys.selected_item;
    uint8_t off_old = (uint8_t)(g_sys.pixel_offset / SCR_ROW_H);
    uint8_t guard = 0;
    do {
        g_sys.selected_item = EncWrap(CAN_ROW_COUNT, (int8_t)dir, g_sys.selected_item);
        if (++guard > (uint8_t)CAN_ROW_COUNT) break;
    } while (!can_row_visible(g_sys.selected_item));   /* 跳过隐藏行(通讯关/从机) */
    target = (int16_t)g_sys.selected_item * SCR_ROW_H;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + SCR_ROW_H > g_sys.pixel_offset + SCR_VIEW_H) g_sys.pixel_offset = (int16_t)(target + SCR_ROW_H - SCR_VIEW_H);
    max_off = (int16_t)(CAN_ROW_COUNT * SCR_ROW_H - SCR_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    /* 视口未移位: 只重绘旧+新两行, 不整视区刷新不闪 */
    if ((uint8_t)(g_sys.pixel_offset / SCR_ROW_H) == off_old && old_sel != g_sys.selected_item) {
        int16_t y0 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)old_sel * SCR_ROW_H);
        int16_t y1 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)g_sys.selected_item * SCR_ROW_H);
        if (y0 >= (int16_t)SCR_VIEW_Y && y0 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) can_draw_row(old_sel, (uint16_t)y0);
        if (y1 >= (int16_t)SCR_VIEW_Y && y1 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) can_draw_row(g_sys.selected_item, (uint16_t)y1);
    } else {
        can_redraw_viewport();
    }
}

/* ===================== 闊充箰椤 =====================
 * SCREEN_MUSIC锛氫笁琛岋紙涓婁紶闊充箰 / 闊充箰鍒楄〃 / 閫�鍑汉锛�
 * SCREEN_MUSIC_LIST锛氬姩鎬佽岋紙鏇茬洰+閫�鍑汉锛夛紝闀垮悕璺戦╃侊紝鎾�鏀捐屾樉绀鸿繘搴 */
#define MUSIC_ROW_COUNT 3

static void music_draw_row(uint8_t i, uint16_t y, uint8_t selected)
{
    static const char *labels[MUSIC_ROW_COUNT] = {"上传音乐", "音乐列表", "退出"};
    uint16_t bg = selected ? UI_ACCENT : UI_BG;
    TFT_FillRect(5, y, 230, 18, bg);
    TFT_DrawStringZh(18, (uint16_t)(y + 1), labels[i], (uint16_t)(selected ? UI_FG : UI_FG_DIM), bg);
}

static void music_redraw_viewport(void)
{
    uint8_t i;
    TFT_FillRect(0, SCR_VIEW_Y, 233, SCR_VIEW_H, UI_BG);
    for (i = 0; i < MUSIC_ROW_COUNT; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        music_draw_row(i, (uint16_t)y, i == g_sys.selected_item);
    }
}

void UI_MusicScroll(int dir)
{
    int16_t target, max_off;
    uint8_t old_sel = g_sys.selected_item;
    uint8_t off_old = (uint8_t)(g_sys.pixel_offset / SCR_ROW_H);
    g_sys.selected_item = EncWrap(MUSIC_ROW_COUNT, (int8_t)dir, g_sys.selected_item);
    target = (int16_t)g_sys.selected_item * SCR_ROW_H;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + SCR_ROW_H > g_sys.pixel_offset + SCR_VIEW_H) g_sys.pixel_offset = (int16_t)(target + SCR_ROW_H - SCR_VIEW_H);
    max_off = (int16_t)(MUSIC_ROW_COUNT * SCR_ROW_H - SCR_VIEW_H);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    /* 视口未移位: 只重绘旧+新两行 */
    if ((uint8_t)(g_sys.pixel_offset / SCR_ROW_H) == off_old && old_sel != g_sys.selected_item) {
        int16_t y0 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)old_sel * SCR_ROW_H);
        int16_t y1 = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + (int16_t)g_sys.selected_item * SCR_ROW_H);
        if (y0 >= (int16_t)SCR_VIEW_Y && y0 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) music_draw_row(old_sel, (uint16_t)y0, 0);
        if (y1 >= (int16_t)SCR_VIEW_Y && y1 + SCR_ROW_H <= (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) music_draw_row(g_sys.selected_item, (uint16_t)y1, 1);
    } else {
        music_redraw_viewport();
    }
}

void UI_DrawMusic(void)
{
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("音乐", COLOR_CYAN);
    for (i = 0; i < MUSIC_ROW_COUNT; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        music_draw_row(i, (uint16_t)y, i == g_sys.selected_item);
    }
}

/* ---- 闊充箰鍒楄〃锛氳屾暟 = 鏇茬洰鏁 + 閫�鍑� ---- */
static uint16_t music_list_count(void)
{
    uint16_t t = MusicPlay_TrackCount();
    return (uint16_t)(t + 1);   /* +1 退出 */
}

/* 褰撳墠閫変腑琛屼负闀垮悕鏇茬洰锛堥渶瑕佽窇椹鐏锛 */
static uint8_t music_list_top_long(void)
{
    char buf[24];
    uint16_t i = g_sys.selected_item;
    if (i >= MusicPlay_TrackCount()) return 0;
    if (MusicPlay_GetTitle(i, buf, sizeof(buf)) != 0) return 0;
    return (zh_str_width(buf) > 168) ? 1 : 0;
}

static void music_list_draw_row(uint16_t i, uint16_t y, uint8_t selected)
{
    char buf[24];
    uint16_t w;
    uint16_t bg = selected ? UI_ACCENT : UI_BG;
    if (i >= MusicPlay_TrackCount()) {       /* 閫�鍑汉� */
        TFT_FillRect(8, y, 224, 18, bg);
        TFT_DrawStringZh(18, (uint16_t)(y + 1), "退出",
                         (uint16_t)(selected ? UI_FG : UI_FG_DIM), bg);
        return;
    }
    if (MusicPlay_GetTitle(i, buf, sizeof(buf)) != 0) buf[0] = 0;
    /* 鏇插悕杈冮暱 鈫 琛屽唴妯鍚戞粴鍔锛堣窇椹鐏锛 */
    w = zh_str_width(buf);
    if (w > 168) {
        uint16_t mq = (uint16_t)(g_sys.music_marquee % (w + 24 + 16));
        TFT_FillRect(8, y, 224, 18, bg);
        TFT_DrawStringZh((uint16_t)(18 - mq), (uint16_t)(y + 1), buf, UI_FG, bg);
        if (mq > 24) TFT_DrawStringZh((uint16_t)(18 - mq + w + 24), (uint16_t)(y + 1), buf, UI_FG, bg);
        return;
    }
    TFT_FillRect(8, y, 224, 18, bg);
    TFT_DrawStringZh(18, (uint16_t)(y + 1), buf,
                     (uint16_t)(selected ? UI_FG : UI_FG_DIM), bg);
    /* 鎾鏀句腑琛岋細鍙充晶鏄剧ず杩涘害锛堢櫨鍒嗘瘮锛宻cale2 鏇存竻鏅帮級 */
    if (MusicPlay_IsPlaying() && MusicPlay_CurTrack() == i) {
        char p[8];
        sprintf(p, "%d%%", (int)MusicPlay_ProgressPct());
        TFT_FillRect(172, (uint16_t)y, 58, 18, bg);
        TFT_DrawString(182, (uint16_t)(y + 5), p, COLOR_CYAN, bg, 1);
    }
}

/* 只刷播放行右侧进度框(不整视区重绘, 不重读 Flash 曲名); 结束 2.5s 后清除 */
static void music_list_refresh_progress(void)
{
    uint16_t i = MusicPlay_CurTrack();
    uint16_t so, y;
    uint8_t selected;
    char p[8];
    if (i >= music_list_count()) return;   /* 退出项等无对应曲行: 不画进度 */
    so = (uint16_t)(g_sys.pixel_offset / 20);
    y = (uint16_t)(36 + ((int16_t)i - (int16_t)so) * 20);
    if (y < 36 || y + 18 > 36 + 100) return;
    selected = (i == g_sys.selected_item);
    {
        uint8_t playing = MusicPlay_IsPlaying();
        uint32_t fin = MusicPlay_FinishMs();
        if (playing || (fin != 0 && (uint32_t)(SystemTime_Millis() - fin) < 2500U)) {
            sprintf(p, "%d%%", (int)MusicPlay_ProgressPct());
            TFT_FillRect(172, y, 58, 18, selected ? UI_ACCENT : UI_BG);
            TFT_DrawString(182, (uint16_t)(y + 5), p, COLOR_CYAN, selected ? UI_ACCENT : UI_BG, 1);
        } else {
            TFT_FillRect(172, y, 58, 18, selected ? UI_ACCENT : UI_BG);
        }
    }
}

static void music_list_redraw_viewport(void)
{
    uint16_t cnt = music_list_count();
    uint16_t i, so;
    so = (uint16_t)(g_sys.pixel_offset / 20);
    TFT_FillRect(0, 36, TFT_WIDTH, 100, UI_BG);
    for (i = so; i < cnt; i++) {
        uint16_t y = (uint16_t)(36 + ((int16_t)i - (int16_t)so) * 20);
        if (y < 36 || y + 18 > 36 + 100) continue;
        music_list_draw_row(i, y, i == g_sys.selected_item);
    }
    draw_scrollbar(cnt, 5, (uint8_t)(g_sys.pixel_offset / 20), 36, 100);
}

void UI_MusicListScroll(int dir)
{
    uint16_t cnt = music_list_count();
    int16_t target, max_off;
    uint8_t old_sel = g_sys.selected_item;
    uint8_t off_old = (uint8_t)(g_sys.pixel_offset / 20);
    g_sys.selected_item = EncWrap(cnt, (int8_t)dir, g_sys.selected_item);
    target = (int16_t)g_sys.selected_item * 20;
    if (target < g_sys.pixel_offset) g_sys.pixel_offset = target;
    else if (target + 20 > g_sys.pixel_offset + 100) g_sys.pixel_offset = (int16_t)(target + 20 - 100);
    max_off = (int16_t)(cnt * 20 - 100);
    if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
    if (max_off > 0 && g_sys.pixel_offset > max_off) g_sys.pixel_offset = max_off;
    /* 视口未移位: 只重绘旧+新两行 */
    if ((uint8_t)(g_sys.pixel_offset / 20) == off_old && old_sel != g_sys.selected_item) {
        int16_t so = (int16_t)(g_sys.pixel_offset / 20);
        int16_t y0 = (int16_t)(36 + ((int16_t)old_sel - so) * 20);
        int16_t y1 = (int16_t)(36 + ((int16_t)g_sys.selected_item - so) * 20);
        if (y0 >= 36 && y0 + 18 <= 136) music_list_draw_row(old_sel, (uint16_t)y0, 0);
        if (y1 >= 36 && y1 + 18 <= 136) music_list_draw_row(g_sys.selected_item, (uint16_t)y1, 1);
    } else {
        music_list_redraw_viewport();
    }
}

void UI_DrawMusicList(void)
{
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("音乐列表", UI_ACCENT2);
    music_list_redraw_viewport();
}

/* 涓婁紶闊充箰寮圭獥锛氭牴鎹涓嶅悓闃舵垫樉绀烘彁绀/杩涘害鏉 */
/* 上传进度：只刷新进度条+数字（卡片在喷窗状态切换时已整张画过，避免整卡刷新闪烁） */
static void music_popup_refresh_progress(void)
{
    uint16_t pw = 190;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = 12;
    char buf[8];
    uint8_t pct = g_sys.music_upload_pct;
    fill_round_rect((uint16_t)(px + 25), (uint16_t)(py + 52), 140, 14, UI_BG, 7);
    if (pct) fill_round_rect((uint16_t)(px + 26), (uint16_t)(py + 53), (uint16_t)(138U * pct / 100U), 12, COLOR_GREEN, 6);
    TFT_FillRect((uint16_t)(px + 45), (uint16_t)(py + 70), (uint16_t)(pw - 90), 12, UI_CARD);
    sprintf(buf, "%d%%", (int)pct);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 6U)) / 2U, (uint16_t)(py + 72), buf, UI_FG_DIM, UI_CARD, 1);
}

void UI_DrawMusicPopup(void)
{
    uint16_t pw = 190, ph = 110;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = 12;                 /* 椤堕儴寮瑰嚭锛屽簳閮ㄧ暀姝ラゆ枃瀛 */
    char buf[24];
    fill_round_rect(px, py, pw, ph, UI_CARD, 8);
    draw_round_outline(px, py, pw, ph, UI_ACCENT2, 8, 2);
    if (g_sys.music_popup == 1) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("单击开启上传AP")) / 2U, (uint16_t)(py + 14), "单击开启上传AP", UI_FG, UI_CARD);
    } else if (g_sys.music_popup == 2) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("连接热点")) / 2U, (uint16_t)(py + 14), "连接热点", UI_FG, UI_CARD);
        TFT_DrawString((TFT_WIDTH - 10 * 12U) / 2U, (uint16_t)(py + 36), "QIMINGXING", COLOR_CYAN, UI_CARD, 2);
        TFT_DrawString((TFT_WIDTH - 9 * 6U) / 2U, (uint16_t)(py + 66), "192.168.4.1", COLOR_GREEN, UI_CARD, 1);
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("点击关闭")) / 2U, (uint16_t)(py + 88), "点击关闭", UI_FG_DIM, UI_CARD);
    } else if (g_sys.music_popup == 3) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("上传中")) / 2U, (uint16_t)(py + 26), "上传中", UI_FG, UI_CARD);
        {
            uint8_t pct = g_sys.music_upload_pct;
            fill_round_rect((uint16_t)(px + 25), (uint16_t)(py + 52), 140, 14, UI_BG, 7);
            if (pct) fill_round_rect((uint16_t)(px + 26), (uint16_t)(py + 53), (uint16_t)(138U * pct / 100U), 12, COLOR_GREEN, 6);
            sprintf(buf, "%d%%", (int)pct);
            TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 6U)) / 2U, (uint16_t)(py + 72), buf, UI_FG_DIM, UI_CARD, 1);
        }
    } else if (g_sys.music_popup == 4) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("上传完成")) / 2U, (uint16_t)(py + 14), "上传完成", COLOR_GREEN, UI_CARD);
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("已存外部Flash")) / 2U, (uint16_t)(py + 44), "已存外部Flash", UI_FG_DIM, UI_CARD);
    } else if (g_sys.music_popup == 5) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("上传失败,再击重试")) / 2U, (uint16_t)(py + 14), "上传失败,再击重试", COLOR_RED, UI_CARD);
    }
}

/* 语言字库上传弹窗(与音乐上传弹窗同风格): 1=待开AP 2=AP已开等待上传 3=上传中 4=完成 5=失败 */
static void lang_popup_draw(void)
{
    uint16_t pw = 190, ph = 110;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = 12;
    char buf[8];
    uint8_t pct = g_sys.lang_upload_pct;
    fill_round_rect(px, py, pw, ph, UI_CARD, 8);
    draw_round_outline(px, py, pw, ph, UI_ACCENT2, 8, 2);
    if (g_sys.lang_popup == 1) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("单击开启上传AP")) / 2U, (uint16_t)(py + 14), "单击开启上传AP", UI_FG, UI_CARD);
    } else if (g_sys.lang_popup == 2) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("连接热点")) / 2U, (uint16_t)(py + 14), "连接热点", UI_FG, UI_CARD);
        TFT_DrawString((TFT_WIDTH - 10 * 12U) / 2U, (uint16_t)(py + 34), "QIMINGXING", COLOR_CYAN, UI_CARD, 2);
        TFT_DrawString((TFT_WIDTH - 9 * 6U) / 2U, (uint16_t)(py + 60), "192.168.4.1", COLOR_GREEN, UI_CARD, 1);
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("点击关闭")) / 2U, (uint16_t)(py + 84), "点击关闭", UI_FG_DIM, UI_CARD);
    } else if (g_sys.lang_popup == 3) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("上传中")) / 2U, (uint16_t)(py + 26), "上传中", UI_FG, UI_CARD);
        fill_round_rect((uint16_t)(px + 25), (uint16_t)(py + 52), 140, 14, UI_BG, 7);
        if (pct) fill_round_rect((uint16_t)(px + 26), (uint16_t)(py + 53), (uint16_t)(138U * pct / 100U), 12, COLOR_GREEN, 6);
        sprintf(buf, "%d%%", (int)pct);
        TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 6U)) / 2U, (uint16_t)(py + 72), buf, UI_FG_DIM, UI_CARD, 1);
    } else if (g_sys.lang_popup == 4) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("上传完成")) / 2U, (uint16_t)(py + 14), "上传完成", COLOR_GREEN, UI_CARD);
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("已存外部Flash")) / 2U, (uint16_t)(py + 44), "已存外部Flash", UI_FG_DIM, UI_CARD);
    } else if (g_sys.lang_popup == 5) {
        TFT_DrawStringZh((TFT_WIDTH - zh_str_width("上传失败,再击重试")) / 2U, (uint16_t)(py + 14), "上传失败,再击重试", COLOR_RED, UI_CARD);
    }
}

/* 语言上传进度：只刷新进度条+数字（弹窗状态切换时已整卡画过，避免闪烁） */
static void lang_popup_refresh_progress(void)
{
    uint16_t pw = 190;
    uint16_t px = (TFT_WIDTH - pw) / 2;
    uint16_t py = 12;
    char buf[8];
    uint8_t pct = LangOta_GetPct();
    g_sys.lang_upload_pct = pct;
    fill_round_rect((uint16_t)(px + 25), (uint16_t)(py + 52), 140, 14, UI_BG, 7);
    if (pct) fill_round_rect((uint16_t)(px + 26), (uint16_t)(py + 53), (uint16_t)(138U * pct / 100U), 12, COLOR_GREEN, 6);
    TFT_FillRect((uint16_t)(px + 45), (uint16_t)(py + 70), (uint16_t)(pw - 90), 12, UI_CARD);
    sprintf(buf, "%d%%", (int)pct);
    TFT_DrawString((TFT_WIDTH - (uint16_t)(strlen(buf) * 6U)) / 2U, (uint16_t)(py + 72), buf, UI_FG_DIM, UI_CARD, 1);
}

void UI_DrawCanScreen(void)
{
    uint8_t i;
    TFT_FillScreen(UI_BG);
    draw_page_title_zh("CAN设置", UI_ACCENT);
    for (i = 0; i < CAN_ROW_COUNT; i++) {
        int16_t y = (int16_t)(SCR_VIEW_Y - g_sys.pixel_offset + i * SCR_ROW_H);
        if (y < (int16_t)SCR_VIEW_Y || y + SCR_ROW_H > (int16_t)(SCR_VIEW_Y + SCR_VIEW_H)) continue;
        can_draw_row(i, (uint16_t)y);
    }
    draw_scrollbar(CAN_ROW_COUNT, 5, (uint8_t)(g_sys.pixel_offset / SCR_ROW_H), SCR_VIEW_Y, SCR_VIEW_H);
}

static Screen_t last_screen = (Screen_t)255;

void UI_Update(void)
{
    /* 澶栭儴锛堢綉椤甸勮/CAN 鍛戒护绛夛級璇锋眰鏁村睆閲嶇粯锛氱疆浣嶅悗鏈杞寮哄埗璧版崲灞忓垎鏀 */
    if (g_sys.ui_force_redraw) {
        g_sys.ui_force_redraw = 0;
        last_screen = (Screen_t)255;
        return;
    }
    if (g_sys.current_screen != last_screen) {
        g_sys.scroll_offset = 0;
        g_sys.pixel_offset = 0;
        last_screen = g_sys.current_screen;
        /* 杩斿洖鑿滃崟锛氭仮澶嶈繘鍏ュ瓙椤靛墠鐨勯変腑椤逛笌婊氬姩浣嶇疆 */
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
                /* 杩涘叆鏃堕棿椤碉細浠 dry_time_sec 鐢熸垚鍏浣嶆暟瀛 */
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
            case SCREEN_CAN:           UI_DrawCanScreen(); break;
            case SCREEN_MUSIC:         UI_DrawMusic(); break;
            case SCREEN_MUSIC_LIST:    UI_DrawMusicList(); break;
                        case SCREEN_LANG_LOAD:  UI_DrawLangLoad(); break;
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
                /* 鍍忕礌婊氬姩锛氭粴鍔ㄧ敱缂栫爜鍣 UI_MenuScroll 鐩存帴閲嶇粯鍙瑙嗗尯锛屾ゅ勬棤闇澶勭悊 */
                break;
            default:
            /* 瀛愰〉闈锛氫粎褰撹ラ〉闈㈢浉鍏崇姸鎬佸彉鍖栨椂鎵嶉噸缁橈紝閬垮厤姣 50ms 鏁村睆鍒锋柊闂鐑 */
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
    static uint8_t last_air = 0xFF, last_ptc = 0xFF;   /* 鏍″噯椤靛疄鏃舵俯搴︾洃鐪 */
                uint8_t redraw = 0;
                if (g_sys.current_screen != last_scr) {
                    redraw = 1;
                    last_scr = g_sys.current_screen;
                    /* 閲嶇疆鍚勯〉闈㈢姸鎬佹爣璁帮紝淇濊瘉棣栨¤繘鍏ユ椂閲嶇粯 */
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
                        static uint8_t last_mdrv = 0xFF;
                        /* 鍍忕礌婊氬姩鐢辩紪鐮佸櫒 UI_MotorScroll 閲嶇粯鍙瑙嗗尯锛屾ゅ勪笉鍐嶆暣灞忓埛鏂 */
                        if (redraw) last_mdrv = g_sys.params.motor_driver;
                        if (!redraw && g_sys.params.motor_driver != last_mdrv) {
                            /* 驱动切换: 保持选中行并滚动视口使其可见(不跳顶部) */
                            uint8_t is_tmc2 = (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                                               g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209);
                            uint8_t cnt2 = is_tmc2 ? 11 : 9;
                            int16_t tgt2, mx2;
                            if (g_sys.selected_item >= cnt2) g_sys.selected_item = (uint8_t)(cnt2 - 1);
                            tgt2 = (int16_t)g_sys.selected_item * SCR_ROW_H;
                            if (tgt2 < g_sys.pixel_offset) g_sys.pixel_offset = tgt2;
                            else if (tgt2 + SCR_ROW_H > g_sys.pixel_offset + SCR_VIEW_H) g_sys.pixel_offset = (int16_t)(tgt2 + SCR_ROW_H - SCR_VIEW_H);
                            mx2 = (int16_t)(cnt2 * SCR_ROW_H - SCR_VIEW_H);
                            if (g_sys.pixel_offset < 0) g_sys.pixel_offset = 0;
                            if (mx2 > 0 && g_sys.pixel_offset > mx2) g_sys.pixel_offset = mx2;
                            last_mdrv = g_sys.params.motor_driver;
                            motor_redraw_viewport();
                        }
                        if (!redraw && g_sys.motor_edit_active) {
                            static char last_m_row[32] = "";
                            char buf[32];
                            motor_row_str(g_sys.selected_item, buf);
                            if (strcmp(buf, last_m_row) != 0) {
                                strcpy(last_m_row, buf);
                                refresh_motor_row(g_sys.selected_item);
                            }
                        }
                        /* TMC 閫氳璇婃柇锛氳繘鍏ラ〉闈(redraw)鍏堟祴涓娆★紝涔嬪悗姣忕掑嶆祴鍒锋柊搴曡 */
                        if (g_sys.params.motor_driver == MOTOR_DRIVER_TMC2208 ||
                            g_sys.params.motor_driver == MOTOR_DRIVER_TMC2209) {
                            uint32_t now = SystemTime_Millis();
                            if (redraw || (uint32_t)(now - last_tmc_check) >= 1000U) {
                                last_tmc_check = now;
                                tmc_probe_update();
                                if (!redraw) tmc_draw_diag();
                            }
                        }
                        break;
                    }
                    case SCREEN_PID_ADJUST:
                        if (g_sys.selected_item != last_sel) {
                            uint8_t old = last_sel;
                            last_sel = g_sys.selected_item;
                            if (!redraw && old != 0xFF) {
                                if (old <= 3U) pid_adj_draw_sel(old);
                                if (g_sys.selected_item <= 3U) pid_adj_draw_sel(g_sys.selected_item);
                            }
                        }
                        /* 缂栬緫涓姣忓抚鍒锋柊褰撳墠琛岋紝鍊煎疄鏃舵洿鏂 */
                        if (!redraw && g_sys.pid_edit_active) {
                            refresh_pid_row((uint8_t)(g_sys.pid_edit_active - 1));
                        }
                        break;
                    case SCREEN_PRESET: {          /* 主菜单：4 固定行，光标移动只刷旧+新两行 */
                        static uint8_t last_msel = 0xFF;
                        if (g_sys.selected_item != last_msel) {
                            uint8_t old = last_msel;
                            last_msel = g_sys.selected_item;
                            if (!redraw && old != 0xFF) {
                                preset_menu_draw_row(old);
                                preset_menu_draw_row(g_sys.selected_item);
                            }
                        }
                        break;
                    }
                    case SCREEN_PRESET_LIST: {     /* 棰勮句簩绾у垪琛锛氭粴鍔/纭璁ゅ脊绐楀眬閮ㄥ埛鏂 */
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
                        /* 闀挎寜鍒囨崲褰撳墠棰勮 鈫 ">" 鏍囪板彉鍖栵紝鏁村彲瑙嗗尯鍒锋柊娓呮帀鏃ф爣璁版畫鐣 */
                        if (!redraw && g_sys.params.current_preset != last_cp) {
                            last_cp = g_sys.params.current_preset;
                            preset_redraw_viewport();
                        } else {
                            last_cp = g_sys.params.current_preset;
                        }
                        if (!redraw && g_sys.pixel_offset != last_po) {
                            /* 婊氬姩锛氭暣鍙瑙嗗尯閲嶇粯 */
                            last_po = g_sys.pixel_offset;
                            last_pitem = g_sys.selected_item;
                            preset_redraw_viewport();
                        } else if (!redraw && g_sys.selected_item != last_pitem) {
                            /* 瑙嗗彛鍐呯Щ鍔ㄥ厜鏍囷細鍙閲嶇粯鏃/鏂颁袱琛 */
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
                            if (last_pc == 1 && g_sys.preset_confirm == 0) redraw = 1;   /* 鍏抽棴寮圭獥娓呮畫鐣 */
                            last_pc = g_sys.preset_confirm;
                            last_cy = 0xFF;
                        }
                        /* 鍏夋爣绉诲姩锛氬彧閲嶇粯寮圭獥鍐呭癸紝涓嶆暣灞忓埛鏂 */
                        if (!redraw && g_sys.preset_confirm == 1 && g_sys.preset_confirm_yes != last_cy) {
                            last_cy = g_sys.preset_confirm_yes;
                            draw_confirm_popup("确定删除预设");
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
                            redraw = 1;              /* 寮圭獥寮/鍏虫暣椤甸噸缁橈細鐢讳笂鏍囬樸佸叧鏃舵竻鑳屾櫙 */
                        }
                        if (g_sys.preset_confirm == 2) {   /* 淇濆瓨纭璁わ細鏄�/鍚﹀彧鍒锋柊寮圭獥 */
                            if (g_sys.preset_confirm_yes != last_cy2) {
                                last_cy2 = g_sys.preset_confirm_yes;
                                if (!redraw) draw_confirm_popup("确定保存");
                            }
                            if (g_sys.preset_confirm != 2) last_cy2 = 0xFF;
                        } else {
                            last_cy2 = 0xFF;
                            /* 琛屽厜鏍囩Щ鍔锛氬悕绉/娓╁害/鏃堕棿/淇濆瓨/閫�鍑� 楂樹寒琛岄渶瀹炴椂閲嶇粯鏁撮〉 */
                            if (!redraw && g_sys.preset_row != last_prow) {
                                last_prow = g_sys.preset_row;
                                preset_edit_draw_rows();
                            }
                            if (g_sys.preset_row_edit != last_pe) {
                                last_pe = g_sys.preset_row_edit;
                                last_prow = g_sys.preset_row;
                                last_name_cur = 0xFF; last_name[0] = 0;
                                last_tv = 0xFF;
                                { uint8_t k; for (k = 0; k < 6; k++) last_dig[k] = 0xFF; }
                                preset_edit_draw_rows();
                            }
                            if (!redraw && g_sys.preset_row_edit == 1) {   /* 名称：变化才重绘 */
                                if (g_sys.preset_name_cur != last_name_cur || strcmp(p->name, last_name) != 0) {
                                    last_name_cur = g_sys.preset_name_cur;
                                    strcpy(last_name, p->name);
                                    preset_edit_draw_rows();
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
                        if (g_sys.selected_item != last_sel) {
                            last_sel = g_sys.selected_item;
                            if (!redraw) draw_btn(184, "  退出", UI_TEXT_DIM, g_sys.selected_item == 0);
                        }
                        break;
                    case SCREEN_TIME_ADJUST: {
                        static uint32_t last_dig_hash = 0;
                        static uint8_t last_edit = 0xFF;
                        uint32_t dig_hash = (uint32_t)g_sys.time_digits[0] | ((uint32_t)g_sys.time_digits[1] << 4) |
                                            ((uint32_t)g_sys.time_digits[2] << 8) | ((uint32_t)g_sys.time_digits[3] << 12) |
                                            ((uint32_t)g_sys.time_digits[4] << 16) | ((uint32_t)g_sys.time_digits[5] << 20);
                        if (g_sys.time_edit_active != last_edit) {
                            /* 缂栬緫鎬佸垏鎹锛氱珛鍗抽噸缁樺綋鍓嶄綅鍏夋爣锛堣繘=姗欒疆寤擄紝閫=鏅閫氳疆寤擄級 */
                            last_edit = g_sys.time_edit_active;
                            if (!redraw && g_sys.time_cursor < TIME_DIGIT_COUNT) {
                                refr_time_digit((uint8_t)g_sys.time_cursor,
                                                g_sys.time_edit_active ? TIME_EDIT_TXT : COLOR_CYAN,
                                                g_sys.time_edit_active ? COLOR_ORANGE : UI_ACCENT2);
                            }
                            last_tc = g_sys.time_cursor;
                            last_dig_hash = dig_hash;
                            break;
                        }
                        if (g_sys.time_edit_active) {
                            /* 缂栬緫鎬侊細鏁板/鍏夋爣鍙樺寲鏃堕噸缁樺綋鍓嶄綅 */
                            if (!redraw && (g_sys.time_cursor != last_tc || dig_hash != last_dig_hash)) {
                                refr_time_digit((uint8_t)g_sys.time_cursor, TIME_EDIT_TXT, COLOR_ORANGE);
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
                        if (g_sys.selected_item != last_sel) {
                            last_sel = g_sys.selected_item;
                            if (!redraw) {
                                draw_btn(160, "  保存退出", COLOR_GREEN, g_sys.selected_item == 0);
                                draw_btn(184, "  取消", UI_TEXT_DIM, g_sys.selected_item == 1);
                            }
                        }
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
                        if (g_sys.selected_item != last_sel) {
                            last_sel = g_sys.selected_item;
                            if (!redraw) {
                                draw_btn(160, "  保存退出", COLOR_GREEN, g_sys.selected_item == 0);
                                draw_btn(184, "  取消", UI_TEXT_DIM, g_sys.selected_item == 1);
                            }
                        }
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
                    case SCREEN_WIFI: {
                        static uint8_t last_wedit = 0xFF;
                        if (g_sys.selected_item != last_sel) {
                            uint8_t old = last_sel;
                            last_sel = g_sys.selected_item;
                            if (!redraw && old != 0xFF && old < WIFI_ROWS && g_sys.selected_item < WIFI_ROWS) {
                                wifi_draw_row(old, (uint16_t)(30 + (uint16_t)old * 26), 0);
                                wifi_draw_row(g_sys.selected_item, (uint16_t)(30 + (uint16_t)g_sys.selected_item * 26), 1);
                            }
                        }
                        if (g_sys.wifi_enabled != last_wifi) { redraw = 1; last_wifi = g_sys.wifi_enabled; }
                        if (g_sys.wifi_edit_active != last_wedit) { redraw = 1; last_wedit = g_sys.wifi_edit_active; }
                        /* ESP 閾捐矾鐘舵/IP 鍙樺寲锛氬眬閮ㄩ噸缁樺紑鍏宠岋紙涓嶆暣灞忛棯锛 */
                        {
                            static uint8_t last_lstate = 0xFF;
                            static char last_lip[16] = "";
                            uint8_t lst = (uint8_t)EspLink_State();
                            if (!redraw && (lst != last_lstate || strcmp(EspLink_IP(), last_lip) != 0)) {
                                last_lstate = lst;
                                strncpy(last_lip, EspLink_IP(), 15);
                                wifi_draw_row(0, 30, (uint8_t)(g_sys.selected_item == 0 ? 1 : 0));
                            } else {
                                last_lstate = lst;
                                strncpy(last_lip, EspLink_IP(), 15);
                            }
                        }
                        break;
                    }
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
                            { uint8_t ch = 0; if (air != last_air) { last_air = air; ch |= 1; } if (ptc != last_ptc) { last_ptc = ptc; ch |= 2; } if (ch) draw_tune_live(ch); }
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
                            { uint8_t ch = 0; if (air != last_air) { last_air = air; ch |= 1; } if (ptc != last_ptc) { last_ptc = ptc; ch |= 2; } if (ch) draw_tune_live(ch); }
                        }
                        break;
                    case SCREEN_SAFETY_ALERT:
                        break;
                    case SCREEN_SETTINGS: {
                        static char last_ss_row[24] = "";
                        static uint8_t last_pop_item = 0xFF;   /* 0xFF=鏃犲脊绐 */
                        /* 鍍忕礌婊氬姩鐢辩紪鐮佸櫒 UI_SettingsScroll 閲嶇粯鍙瑙嗗尯锛屾ゅ勪笉鍐嶆暣灞忓埛鏂 */
                        /* 缂栬緫涓瀹炴椂鍒锋柊褰撳墠琛岋紙鍊煎彉鍖栨墠閲嶇粯锛涢煶閲/浜搴﹁蛋寮圭獥锛屼笉鍒锋柊琛岄伩鍏嶉棯鐑侊級 */
                        if (!redraw && g_sys.settings_edit_active &&
                            g_sys.selected_item != 1 && g_sys.selected_item != 3 && g_sys.selected_item != 6) {
                            char buf[24];
                            settings_row_str(g_sys.selected_item, buf);
                            if (strcmp(buf, last_ss_row) != 0) {
                                strcpy(last_ss_row, buf);
                                refresh_settings_row(g_sys.selected_item);
                            }
                        }
                        /* RGB 浜搴﹀脊绐楋細鍏夋爣(sel/缂栬緫鎬)鍙樺寲閲嶇粯缁撴瀯锛涘煎彉鍖栦粎鍒疯ユ潯锛涘叧闂鏁村睆閲嶇粯娓呮畫鐣 */
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
                        /* 铚傞福鍣ㄩ煶閲�(1)/浜搴(3) 缂栬緫寮圭獥锛氫粎鍦ㄦ墦寮鎴栨暟鍊煎彉鍖栨椂閲嶇粯锛堥伩鍏嶉棯鐑侊級锛
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
                                last_pop_val[0] = 0;
                                if (pop != 0xFF) draw_settings_popup(pop);   /* 打开: 整卡绘制一次 */
                            }
                            if (!redraw && pop != 0xFF) {
                                char pv[16];
                                uint8_t pvv = (pop == 1) ? g_sys.buzzer_vol : g_sys.backlight;
                                sprintf(pv, "%d", pvv);
                                if (strcmp(pv, last_pop_val) != 0) {
                                    strcpy(last_pop_val, pv);
                                    settings_popup_bar(pop);   /* 值变: 只刷进度条+数字, 卡片不闪 */
                                }
                            }
                        }
                        /* 语言字库上传弹窗(与音乐弹窗同状态机): 弹窗变化整卡画; 上传中100ms刷进度; 完成→重启生效; 失败→自动收起 */
                        {
                            static uint8_t last_lpn = 0xFF;
                            static uint32_t last_lpt = 0;
                            uint32_t nowm = SystemTime_Millis();
                            if (g_sys.lang_popup != last_lpn) {
                                last_lpn = g_sys.lang_popup;
                                redraw = 1;
                                if (g_sys.lang_popup == 4 || g_sys.lang_popup == 5) last_lpt = nowm;
                            }
                            if (!redraw && g_sys.lang_popup == 2 && LangOta_Active()) {
                                g_sys.lang_popup = 3;        /* 握手到达: 弹窗切上传中 */
                                last_lpt = nowm;
                                redraw = 1;
                            }
                            if (!redraw && g_sys.lang_popup == 3) {
                                if (g_sys.lang_download_done) {
                                    g_sys.lang_popup = 4;     /* 完成 */
                                    last_lpt = nowm;
                                    redraw = 1;
                                } else if (!LangOta_Active() && LangOta_GotHand()) {
                                    g_sys.lang_popup = 5;     /* 失败 */
                                    last_lpt = nowm;
                                    redraw = 1;
                                }
                            }
                            if (g_sys.lang_popup == 3 && (uint32_t)(nowm - last_lpt) >= 100) {
                                last_lpt = nowm;
                                lang_popup_refresh_progress();
                            }
                            if (g_sys.lang_popup == 4 && (uint32_t)(nowm - last_lpt) >= 1800) {
                                /* 显示"上传完成"后重启, 使新字库生效 */
                                System_FlushSave();
                                NVIC_SystemReset();
                            }
                            if (g_sys.lang_popup == 5 && (uint32_t)(nowm - last_lpt) >= 1800) {
                                g_sys.lang_popup = 0;        /* 失败自动收起, 可重试 */
                                g_sys.lang_download_done = 0;
                                redraw = 1;
                            }
                        }
                        break;
                    }
                    case SCREEN_CAN: {
                        static uint8_t last_cn = 0xFF;
                        static uint8_t last_jn = 0xFF;
                        static char last_cr[24] = "";
                        static uint8_t last_cs = 0xFF;
                        /* 宸茶繛鎺ユ暟 / 鍏ョ綉鐘舵 瀹炴椂鍒锋柊 */
                        if (g_sys.can_connected != last_cn || g_sys.can_joined != last_jn) {
                            last_cn = g_sys.can_connected;
                            last_jn = g_sys.can_joined;
                            refresh_can_row(3);
                        }
                        /* 鎼滅储鎻愮ず甯э細閫掑噺鍚庢竻闄 */
                        /* 搜索设备状态机: 状态变化立即刷行2; 搜索中每500ms广播; 命中/超时推进 */
                        if (g_sys.can_search_state != last_cs) {
                            last_cs = g_sys.can_search_state;
                            refresh_can_row(2);
                        }
                        if (g_sys.can_search_state) {
                            uint32_t now2 = SystemTime_Millis();
                            uint32_t dt = (uint32_t)(now2 - g_sys.can_search_t0);
                            switch (g_sys.can_search_state) {
                            case 1:   /* 搜索中 */
                                if ((uint32_t)(now2 - g_sys.can_search_last) >= 500U) {
                                    g_sys.can_search_last = now2;
                                    CAN_Cluster_RequestSearch();
                                }
                                if (g_sys.can_connected > g_sys.can_search_cnt0) { g_sys.can_search_state = 2; g_sys.can_search_t0 = now2; }
                                else if (dt >= 4000U) { g_sys.can_search_state = 5; g_sys.can_search_t0 = now2; }
                                break;
                            case 2:   /* 发现设备 */
                                if (dt >= 800U) { g_sys.can_search_state = 3; g_sys.can_search_t0 = now2; }
                                break;
                            case 3:   /* 连接中 */
                                if (dt >= 2000U) { g_sys.can_search_state = 4; g_sys.can_search_t0 = now2; }
                                break;
                            case 4:   /* 连接成功: 2s 后消失 */
                                if (dt >= 2000U) { g_sys.can_search_state = 0; }
                                break;
                            case 5:   /* 未发现设备 */
                                if (dt >= 800U) { g_sys.can_search_state = 6; g_sys.can_search_t0 = now2; }
                                break;
                            case 6:   /* 请重试: 2s 后消失 */
                                if (dt >= 2000U) { g_sys.can_search_state = 0; }
                                break;
                            default:
                                g_sys.can_search_state = 0;
                                break;
                            }
                        }
                        /* 缂栬緫涓鍊煎彉鍖栧疄鏃跺埛鏂板綋鍓嶈 */
                        if (g_sys.can_edit_active) {
                            char buf[24];
                            can_row_str(g_sys.selected_item, buf);
                            if (strcmp(buf, last_cr) != 0) {
                                strcpy(last_cr, buf);
                                refresh_can_row(g_sys.selected_item);
                            }
                        }
                        break;
                    }
                    case SCREEN_MUSIC: {
                        /* 涓婁紶杩涘害寮圭獥姣 100ms 鍒锋柊涓娆★紱瀹屾垚/澶辫触鐭鏄惧悗鑷鍔ㄦ敹璧 */
                        static uint8_t last_mup = 0xFF;
                        static uint32_t last_ms = 0;
                        uint32_t nowm = SystemTime_Millis();
                        if (g_sys.music_popup != last_mup) {
                            last_mup = g_sys.music_popup;
                            redraw = 1;
                            if (g_sys.music_popup == 4 || g_sys.music_popup == 5) last_ms = nowm;
                        }
                        if (g_sys.music_popup == 3 && (uint32_t)(nowm - last_ms) >= 100) {
                            last_ms = nowm;
                            music_popup_refresh_progress();
                        }
                        if ((g_sys.music_popup == 4 || g_sys.music_popup == 5) &&
                            (uint32_t)(nowm - last_ms) >= 1800) {
                            g_sys.music_popup = 0;
                            redraw = 1;
                        }
                        break;
                    }
                    case SCREEN_MUSIC_LIST: {
                        /* 鎾鏀捐岃繘搴︼紙灞閮ㄥ埛鏂帮紝涓嶆暣瑙嗗彛閲嶇粯閬垮厤闂鐑/鍗￠】锛+ 闀垮悕璺戦┈鐏 */
                        static uint32_t last_ml = 0;
                        uint32_t nowm = SystemTime_Millis();
                        if ((uint32_t)(nowm - last_ml) >= 150) {
                            last_ml = nowm;
                            music_list_refresh_progress();
                            if (music_list_top_long()) {
                                g_sys.music_marquee += 2;
                                /* 浠呴噸缁橀暱鍚嶉変腑琛 */
                                {
                                    uint16_t so = (uint16_t)(g_sys.pixel_offset / 20);
                                    uint16_t y = (uint16_t)(36 + ((int16_t)g_sys.selected_item - (int16_t)so) * 20);
                                    if (y >= 36 && y + 18 <= 36 + 100) {
                                        music_list_draw_row(g_sys.selected_item, y, 1);
                                    }
                                }
                            }
                        }
                                            break;
                    }case SCREEN_LANG_LOAD: {
                        static uint32_t last_lp = 0;
                        uint32_t nowm = SystemTime_Millis();
                        if ((uint32_t)(nowm - last_lp) >= 200U) {
                            last_lp = nowm;
                            {
                                uint8_t p = LangOta_GetPct();
                                if (p != g_sys.lang_upload_pct) {
                                    g_sys.lang_upload_pct = p;
                                    lang_draw_progress(p);
                                }
                            }
                        }
                        if (g_sys.lang_download_done) {
                            g_sys.lang_download_done = 0;
                            System_FlushSave();
                            NVIC_SystemReset();
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
                        case SCREEN_CAN:            UI_DrawCanScreen(); break;
                        case SCREEN_MUSIC:          UI_DrawMusic();
                                                    if (g_sys.music_popup) UI_DrawMusicPopup(); break;
                        case SCREEN_MUSIC_LIST:     UI_DrawMusicList(); break;
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

    /* 鐑樺共鐘舵佸彉鍖栨椂鍙閲嶇粯绗5鍗＄殑鐘舵佹枃瀛楋紱IDLE 鏃朵笉鏄剧ず鏂囧瓧 */
    if ((uint8_t)g_sys.run_state != last_state) {
        last_state = (uint8_t)g_sys.run_state;
        static const char *sstr[] = {"停止烘干","开始烘干","开始烘干","开始烘干","停止烘干","停止烘干"};
        static const uint16_t scol[] = {UI_TEXT_DIM, UI_ACCENT2, UI_WARN, UI_ACCENT, UI_ACCENT, UI_OK};
        uint16_t fill = (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME;
        const char *s = sstr[g_sys.run_state];
        TFT_FillRect(166, 103, 70, 16, fill);
        TFT_DrawStringZh(166, 103, s, scol[g_sys.run_state], fill);
    }

    /* 鍗1 TEMP 117x42 @(2,2) */
    main_card_rect(0, &x, &y, &w, &ch);
    ui_fin(ui_d1(buf, g_sys.current_temp));
    if (strcmp(buf, last_val[0]) != 0) {
        draw_value_unit(0, x, y, ch,
                        (g_sys.selected_item == 0) ? UI_CARD_HI : CARD_BG_TEMP,
                        buf, UI_WARN);
        strcpy(last_val[0], buf);
    }

    /* 鍗2 婀垮害 117x42 @(121,2) */
    main_card_rect(1, &x, &y, &w, &ch);
    ui_fin(ui_cpy(ui_d1(buf, g_sys.current_humidity), "%"));
    if (strcmp(buf, last_val[1]) != 0) {
        draw_value_unit(1, x, y, ch,
                        (g_sys.selected_item == 1) ? UI_CARD_HI : CARD_BG_HUMI,
                        buf, UI_CYAN);
        strcpy(last_val[1], buf);
    }

    /* 鍗3 WEIGHT 117x42 @(2,46) */
    main_card_rect(2, &x, &y, &w, &ch);
    sprintf(buf, "%d", (int)fmt_weight_g());
    if (strcmp(buf, last_val[2]) != 0) {
        draw_value_unit(2, x, y, ch,
                        (g_sys.selected_item == 2) ? UI_CARD_HI : CARD_BG_WEIGHT,
                        buf, UI_PURPLE);
        strcpy(last_val[2], buf);
    }

    /* 鍗4 PTC 117x42 @(121,46) */
    main_card_rect(3, &x, &y, &w, &ch);
    sprintf(buf, "%d", (int)(g_sys.ptc_temp + 0.5f));
    if (strcmp(buf, last_val[3]) != 0) {
        draw_value_unit(3, x, y, ch,
                        (g_sys.selected_item == 3) ? UI_CARD_HI : CARD_BG_PTC,
                        buf, UI_ACCENT2);
        strcpy(last_val[3], buf);
    }

    /* 鍗5 鐑樺共鏃堕棿 236x43 @(2,90)锛氭椂闂存暟瀛椾粎鍙樺寲鏃堕噸缁樸
     * 鐑樺共涓涓绘樉绀=鍓╀綑鍊掕℃椂(澶у瓧)锛涚┖闂蹭富鏄剧ず=璁惧畾鏃堕棿 */
    main_card_rect(4, &x, &y, &w, &ch);
    {
        static char last_rem_str[16] = "";
        uint16_t tbg = (g_sys.selected_item == 4) ? UI_CARD_HI : CARD_BG_TIME;
        uint8_t drying = (g_sys.run_state == STATE_HEATING || g_sys.run_state == STATE_DRYING ||
                          g_sys.run_state == STATE_COOLING);
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

