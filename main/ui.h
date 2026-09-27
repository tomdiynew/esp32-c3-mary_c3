#pragma once

#include <stdint.h>

/* Anti-aliased UI text and simple shapes, drawn into full-width strip buffers (panel byte order).
 * Fonts come from tools/make_ui_font.py (Segoe UI, 4-bit coverage). */

typedef struct {
    uint32_t off;   // first byte in the font's bitmap
    uint8_t  w, h;  // ink box
    int8_t   x, y;  // ink box offset from the pen x / line top
    uint8_t  adv;   // pen advance
} ui_glyph_t;

typedef struct {
    uint8_t           line_h;
    uint8_t           ascent;  // baseline, from the line top
    uint8_t           cap;     // top of the capitals, from the line top
    const ui_glyph_t* glyphs;  // ASCII 32..126
    const uint8_t*    bits;
} ui_font_t;

extern const ui_font_t ui_font_s;  // 13 px: hints
extern const ui_font_t ui_font_m;  // 20 px: list items, messages
extern const ui_font_t ui_font_l;  // 30 px: titles
extern const ui_font_t ui_font_lb; // 30 px bold: counter with many digits
extern const ui_font_t ui_font_xl; // 40 px bold: merit counter

#define UI_SWAP16(v)    ((uint16_t)(((v) >> 8) | ((v) << 8)))
#define UI_RGB(r, g, b) UI_SWAP16((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

int ui_text_width(const ui_font_t* font, const char* txt);

/* Draw `txt` with its line top at (x, y), blending over what is already in the strip */
void ui_text(const ui_font_t* font, const char* txt, int x, int y, uint16_t color, uint16_t* strip, int y0, int y1);

/* Draw `txt` centered on (cx, cy): horizontally on its width, vertically on the capital letters */
void ui_text_center(const ui_font_t* font, const char* txt, int cx, int cy, uint16_t color, uint16_t* strip, int y0,
                    int y1);

/* Filled rounded rectangle, clipped to the strip rows [y0, y1) */
void ui_rrect(int x, int y, int w, int h, int r, uint16_t color, uint16_t* strip, int y0, int y1);

/* Darken everything under a rounded rectangle to 1/4 brightness (a translucent dark panel) */
void ui_dim_rrect(int x, int y, int w, int h, int r, uint16_t* strip, int y0, int y1);

/* Centered message box for games: big `title`, then up to two small lines (NULL = none) */
void ui_message_box(const char* title, const char* line1, const char* line2, uint16_t* strip, int y0, int y1);
