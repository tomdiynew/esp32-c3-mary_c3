#include <string.h>

#include "board.h"
#include "font_ui_data.h"
#include "ui.h"

#define W BOARD_LCD_H_RES

static const ui_glyph_t* glyph(const ui_font_t* font, char c)
{
    unsigned char u = (unsigned char)c;
    return font->glyphs + ((u < 32 || u > 126) ? '?' - 32 : u - 32);
}

int ui_text_width(const ui_font_t* font, const char* txt)
{
    int w = 0;
    for (; *txt; txt++) {
        w += glyph(font, *txt)->adv;
    }
    return w;
}

/* Mix two panel-order RGB565 colors: a = 0 -> bg, 15 -> fg */
static inline uint16_t blend(uint16_t bg, uint16_t fg, int a)
{
    if (a >= 15) {
        return fg;
    }
    const uint16_t b = UI_SWAP16(bg), f = UI_SWAP16(fg);
    const int      rb = b >> 11, gb = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int      r  = rb + ((f >> 11) - rb) * a / 15;
    const int      g  = gb + (((f >> 5) & 0x3F) - gb) * a / 15;
    const int      bl = bb + ((f & 0x1F) - bb) * a / 15;
    return UI_SWAP16((uint16_t)((r << 11) | (g << 5) | bl));
}

void ui_text(const ui_font_t* font, const char* txt, int x, int y, uint16_t color, uint16_t* strip, int y0, int y1)
{
    for (; *txt; txt++) {
        const ui_glyph_t* g      = glyph(font, *txt);
        const int         gy     = y + g->y, gx = x + g->x;
        const int         stride = (g->w + 1) / 2;
        const int         ya = gy > y0 ? gy : y0, yb = gy + g->h < y1 ? gy + g->h : y1;
        for (int py = ya; py < yb; py++) {
            const uint8_t* src = font->bits + g->off + (py - gy) * stride;
            uint16_t*      row = strip + (py - y0) * W;
            for (int i = 0; i < g->w; i++) {
                const int a  = (i & 1) ? src[i / 2] & 0x0F : src[i / 2] >> 4;
                const int px = gx + i;
                if (a && px >= 0 && px < W) {
                    row[px] = blend(row[px], color, a);
                }
            }
        }
        x += g->adv;
    }
}

void ui_text_center(const ui_font_t* font, const char* txt, int cx, int cy, uint16_t color, uint16_t* strip, int y0,
                    int y1)
{
    const int top = cy - (font->cap + font->ascent) / 2;
    ui_text(font, txt, cx - ui_text_width(font, txt) / 2, top, color, strip, y0, y1);
}

/* Columns to skip at each end of row `ly` of a rounded rectangle of height h, corner radius r */
static int corner_inset(int ly, int h, int r)
{
    const int dy    = ly < r ? r - ly : (ly >= h - r ? ly - (h - r - 1) : 0);
    int       inset = 0;
    while (inset < r && (r - inset) * (r - inset) + dy * dy > r * r) {
        inset++;
    }
    return inset;
}

void ui_rrect(int x, int y, int w, int h, int r, uint16_t color, uint16_t* strip, int y0, int y1)
{
    const int ya = y > y0 ? y : y0, yb = y + h < y1 ? y + h : y1;
    for (int py = ya; py < yb; py++) {
        const int inset = corner_inset(py - y, h, r);
        uint16_t* row   = strip + (py - y0) * W;
        for (int px = x + inset; px < x + w - inset; px++) {
            row[px] = color;
        }
    }
}

void ui_dim_rrect(int x, int y, int w, int h, int r, uint16_t* strip, int y0, int y1)
{
    const int ya = y > y0 ? y : y0, yb = y + h < y1 ? y + h : y1;
    for (int py = ya; py < yb; py++) {
        const int inset = corner_inset(py - y, h, r);
        uint16_t* row   = strip + (py - y0) * W;
        for (int px = x + inset; px < x + w - inset; px++) {
            const uint16_t v = UI_SWAP16(row[px]);
            row[px]          = UI_SWAP16((uint16_t)((v >> 2) & 0x39E7));  // each channel / 4
        }
    }
}

void ui_message_box(const char* title, const char* line1, const char* line2, uint16_t* strip, int y0, int y1)
{
    const int lines = (line1 != NULL) + (line2 != NULL);
    const int h = 58 + lines * 20, y = (BOARD_LCD_V_RES - h) / 2;
    ui_dim_rrect(24, y, W - 48, h, 12, strip, y0, y1);
    ui_text_center(&ui_font_l, title, W / 2, y + 30, UI_RGB(255, 255, 255), strip, y0, y1);
    if (line1) {
        ui_text_center(&ui_font_s, line1, W / 2, y + 60, UI_RGB(210, 214, 225), strip, y0, y1);
    }
    if (line2) {
        ui_text_center(&ui_font_s, line2, W / 2, y + 80, UI_RGB(210, 214, 225), strip, y0, y1);
    }
}
