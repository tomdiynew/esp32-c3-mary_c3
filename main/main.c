/*
 * Flowers for Our Lady (圣母献花) on the 1.50" 240x280 GC9306/GC9307 panel.
 *
 * Start screen: the logo picture with drifting petals and a pulsing "点击献花" button. Opening the altar shows
 * "今日祝福": a rain of petals, the shrine glows and a blessing appears on a ribbon at the top (never over the
 * portrait). Every click offers one flower: a lily or a white rose flies up into the vase with sparkles, a small
 * lily fades in and out at the top right and the count ("已献 N 朵") bumps. The first 20 fill the bouquet.
 * The count is kept in NVS like the wooden fish; holding the key 1 s clears it.
 *
 * Art: tools/make_art.py, blessings: tools/make_blessings.py. Frames are composed strip by strip into two
 * DMA buffers.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "blessings.h"
#include "board.h"
#include "button.h"
#include "lcd.h"
#include "sprites.h"
#include "ui.h"

static const char* TAG = "mary";

#define W BOARD_LCD_H_RES
#define H BOARD_LCD_V_RES

#define LINES_PER_CHUNK   40
#define MAX_TRANSFER_SIZE (W * LINES_PER_CHUNK * sizeof(uint16_t))

#define FLIGHT_MS     650   // flower flying into the vase
#define MAX_FLIGHTS   6
#define BURST_MS      420   // sparkles where it lands
#define GLOW_MS       600
#define BUMP_MS       160
#define POP_MS        1100  // the little lily at the top right fades in and out where it is
#define MAX_POPS      4
#define TOAST_MS      1400
#define TOAST_FADE    350
#define SAVE_IDLE_MS  1000

#define CELEB_MS      4500  // "今日祝福" when the altar opens
#define CELEB_OUT_MS  500
#define BANNER_DELAY  150
#define BANNER_DROP_MS 320
#define BANNER_DROP   18
#define RAIN_PETALS   28
#define AMBIENT_PETALS 5

#define COLOR_COUNT      UI_RGB(62, 106, 168)
#define COLOR_COUNT_BUMP UI_RGB(226, 150, 40)
#define BLESS_FILL       0xFFDE  // #fffaf0, true order
#define BLESS_OUTLINE    0x3AB4  // #3e56a0

extern const uint8_t logo_start[] asm("_binary_logo_rgb565_start");
extern const uint8_t altar_start[] asm("_binary_altar_rgb565_start");
extern const uint8_t spr_start[] asm("_binary_sprites_bin_start");

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t         s_trans_done;

/* ---------------------------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------------------------*/

typedef struct {
    int64_t t0;     // 0 = free
    int     slot;   // where it goes
    int     kind;
    int     side;   // arc to the left (-1) or right (+1)
} flight_t;

typedef struct {
    float x0, y0, speed, sway, freq, phase;
} petal_t;

static struct {
    bool     logo;           // start screen
    uint32_t count;
    bool     dirty;
    int64_t  last_press;
    flight_t flights[MAX_FLIGHTS];
    int64_t  burst_t;
    int      burst_slot;
    int64_t  glow_t;
    int64_t  bump_t;
    int64_t  pop_t[MAX_POPS];
    int64_t  toast_t;
    int64_t  celeb_t;
    int      blessing;
    petal_t  rain[RAIN_PETALS];
    petal_t  ambient[AMBIENT_PETALS];
} s;

static int64_t s_now;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float ease_out(float t)
{
    t = clampf(t, 0, 1);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

static float ease_out_back(float t)
{
    const float c1 = 1.9f, c3 = c1 + 1.0f, u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

static float frand(float lo, float hi)
{
    return lo + (hi - lo) * (esp_random() & 0xFFFF) / 65535.0f;
}

static void petal_init(petal_t* p, float y_lo, float y_hi, float v_lo, float v_hi)
{
    p->x0    = frand(-10, W + 10);
    p->y0    = frand(y_lo, y_hi);
    p->speed = frand(v_lo, v_hi);
    p->sway  = frand(8, 20);
    p->freq  = frand(0.6f, 1.4f);
    p->phase = frand(0, 6.28f);
}

/* ---------------------------------------------------------------------------------------------
 * Counting, NVS
 * -------------------------------------------------------------------------------------------*/

#define NVS_NS  "mary"
#define NVS_KEY "flowers"

static void count_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, NVS_KEY, &s.count);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "flowers so far: %lu", (unsigned long)s.count);
}

static void count_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_u32(h, NVS_KEY, s.count) == ESP_OK && nvs_commit(h) == ESP_OK) {
        s.dirty = false;
        ESP_LOGI(TAG, "saved %lu flowers", (unsigned long)s.count);
    }
    nvs_close(h);
}

static void count_reset(int64_t now)
{
    s.count   = 0;
    s.dirty   = false;
    s.bump_t  = now;
    s.toast_t = now;
    s.celeb_t = now - 100000;
    memset(s.flights, 0, sizeof(s.flights));
    for (int i = 0; i < MAX_POPS; i++) {
        s.pop_t[i] = now - 100000;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        esp_err_t err = nvs_erase_key(h, NVS_KEY);
        if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "count reset, erased from flash");
}

static void start_celebration(int64_t now)
{
    int b = esp_random() % BLESSING_COUNT;
    if (b == s.blessing) {
        b = (b + 1) % BLESSING_COUNT;
    }
    s.blessing = b;
    s.celeb_t  = now;
    for (int i = 0; i < RAIN_PETALS; i++) {
        petal_init(&s.rain[i], -160, -10, 70, 120);
    }
    ESP_LOGI(TAG, "blessing of the day: #%d", b);
}

/* A flower reached the vase */
static void arrive(const flight_t* fl, int64_t now)
{
    s.count++;
    s.dirty      = true;
    s.burst_t    = now;
    s.burst_slot = fl->slot;
    s.glow_t     = now;
    s.bump_t     = now;
    int oldest   = 0;
    for (int i = 1; i < MAX_POPS; i++) {
        if (s.pop_t[i] < s.pop_t[oldest]) {
            oldest = i;
        }
    }
    s.pop_t[oldest] = now;
}

static void offer(int64_t now)
{
    int in_flight = 0, free_i = -1;
    for (int i = 0; i < MAX_FLIGHTS; i++) {
        if (s.flights[i].t0) {
            in_flight++;
        } else if (free_i < 0) {
            free_i = i;
        }
    }
    if (free_i < 0) {
        return;  // too many in the air: this click is dropped, every flower that flies is counted
    }
    const uint32_t n   = s.count + in_flight;  // flowers placed once everything in the air has landed
    flight_t*      fl  = &s.flights[free_i];
    fl->t0             = now;
    fl->slot           = n < SLOT_COUNT ? (int)n : (int)(esp_random() % SLOT_COUNT);
    fl->kind           = n < SLOT_COUNT ? s_slots[fl->slot].kind : (int)(esp_random() % FLOWER_KINDS);
    fl->side           = (esp_random() & 1) ? 1 : -1;
    s.last_press       = now;
}

static void update(int64_t now)
{
    for (int i = 0; i < MAX_FLIGHTS; i++) {
        flight_t* fl = &s.flights[i];
        if (fl->t0 && now - fl->t0 >= FLIGHT_MS) {
            arrive(fl, now);
            fl->t0 = 0;
        }
    }
    if (s.dirty && now - s.last_press > SAVE_IDLE_MS) {
        count_save();
    }
}

/* ---------------------------------------------------------------------------------------------
 * Drawing
 * -------------------------------------------------------------------------------------------*/

static inline uint16_t blend(uint16_t bg, uint16_t fg, int a)
{
    const uint16_t b  = UI_SWAP16(bg);
    const int      rb = b >> 11, gb = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int      r  = rb + ((((fg >> 11) - rb) * a + 128) >> 8);
    const int      g  = gb + (((((fg >> 5) & 0x3F) - gb) * a + 128) >> 8);
    const int      bl = bb + ((((fg & 0x1F) - bb) * a + 128) >> 8);
    return UI_SWAP16((uint16_t)((r << 11) | (g << 5) | bl));
}

/* Sprite scaled by scale/256 about its design point (dcx, dcy), which lands on (cx, cy); alpha mul/256 */
static void blit_at(int id, int dcx, int dcy, int cx, int cy, int scale, int mul, uint16_t* strip, int y0, int y1)
{
    const sprite_t* sp = &s_sprites[id];
    if (scale <= 0 || mul <= 0) {
        return;
    }
    const int      left = cx + (sp->x - dcx) * scale / 256, top = cy + (sp->y - dcy) * scale / 256;
    const int      w = sp->w * scale / 256, h = sp->h * scale / 256;
    const int      ya = top > y0 ? top : y0, yb = top + h < y1 ? top + h : y1;
    const int      xa = left > 0 ? left : 0, xb = left + w < W ? left + w : W;
    const uint8_t* color = spr_start + sp->off;
    const uint8_t* alpha = color + (uint32_t)sp->w * sp->h * 2;
    for (int py = ya; py < yb; py++) {
        const int sy  = scale == 256 ? py - top : (py - top) * 256 / scale;
        uint16_t* out = strip + (py - y0) * W;
        for (int px = xa; px < xb; px++) {
            const int sx = scale == 256 ? px - left : (px - left) * 256 / scale;
            if (sx >= sp->w || sy >= sp->h) {
                continue;
            }
            const int si = sy * sp->w + sx;
            int       a  = alpha[si];
            if (!a) {
                continue;
            }
            if (mul < 256) {
                a = a * mul >> 8;
            }
            const uint16_t c = (uint16_t)(color[2 * si] << 8 | color[2 * si + 1]);
            out[px]          = a >= 255 ? UI_SWAP16(c) : blend(out[px], c, a + (a >> 7));
        }
    }
}

/* Unscaled, moved by (dx, dy) from the design position */
static void blit(int id, int dx, int dy, int mul, uint16_t* strip, int y0, int y1)
{
    blit_at(id, 0, 0, dx, dy, 256, mul, strip, y0, y1);
}

static void draw_petal(const petal_t* p, float t_s, int mul, uint16_t* strip, int y0, int y1)
{
    const float y = p->y0 + p->speed * t_s;
    const float x = p->x0 + p->sway * sinf(p->freq * t_s * 2 + p->phase);
    const int   frame = ((int)(t_s * p->freq * 5 + p->phase * 2)) & 3;
    blit_at(SPR_PETAL_0 + frame, FX, FY, (int)x, (int)y, 256, mul, strip, y0, y1);
}

static void draw_ambient(uint16_t* strip, int y0, int y1)
{
    const float t_s = s_now / 1000.0f;
    for (int i = 0; i < AMBIENT_PETALS; i++) {
        petal_t   p   = s.ambient[i];
        const float span = H + 40;
        p.y0 = fmodf(p.y0 + p.speed * t_s, span) - 20 - p.speed * t_s;  // wrap around the screen height
        draw_petal(&p, t_s, 256, strip, y0, y1);
    }
}

static void blessing_text(int idx, int dy, int mul, uint16_t* strip, int y0, int y1)
{
    int n = 0;
    while (n < BLESSING_MAX && s_blessings[idx][n] != 0xFF) {
        n++;
    }
    for (int k = 0; k < n; k++) {
        const bless_glyph_t* g   = &s_bless_glyphs[s_blessings[idx][k]];
        const int            adv = n >= 7 ? BLESSING_ADVANCE - 3 : BLESSING_ADVANCE;  // 7 characters: a little tighter
        const int            cx  = BANNER_CX + (2 * k - (n - 1)) * adv / 2;
        const int            top = BANNER_TEXT_CY - BLESSING_INK_MID + dy;
        const int            gx = cx + g->x, gy = top + g->y;
        const int            ya = gy > y0 ? gy : y0, yb = gy + g->h < y1 ? gy + g->h : y1;
        for (int py = ya; py < yb; py++) {
            const uint8_t* src = s_bless_bits + g->off + (py - gy) * g->w;
            uint16_t*      out = strip + (py - y0) * W + gx;
            for (int i = 0; i < g->w; i++) {
                const int v = src[i];
                if (!v || gx + i < 0 || gx + i >= W) {
                    continue;
                }
                const int oa = (v >> 4) * 17 * mul >> 8, fa = (v & 15) * 17 * mul >> 8;
                if (oa) {
                    out[i] = blend(out[i], BLESS_OUTLINE, oa + (oa >> 7));
                }
                if (fa) {
                    out[i] = blend(out[i], BLESS_FILL, fa + (fa >> 7));
                }
            }
        }
    }
}

static void render_logo(uint16_t* strip, int y0, int y1)
{
    memcpy(strip, logo_start + y0 * W * 2, (size_t)(y1 - y0) * W * 2);
    draw_ambient(strip, y0, y1);
    const float pulse = 0.5f + 0.5f * sinf(s_now * (2 * (float)M_PI) / 1400);
    blit(SPR_BUTTON, 0, (int)lroundf(-2 * pulse), 190 + (int)(66 * pulse), strip, y0, y1);
}

static void render_altar(uint16_t* strip, int y0, int y1)
{
    const int64_t now = s_now;
    memcpy(strip, altar_start + y0 * W * 2, (size_t)(y1 - y0) * W * 2);

    /* shrine glow: after each flower, and throughout a celebration */
    int glow = 0;
    if (now - s.glow_t < GLOW_MS) {
        glow = (int)(200 * (1 - (float)(now - s.glow_t) / GLOW_MS));
    }
    const int64_t tc = now - s.celeb_t;
    int           celeb_fade = 0;
    if (tc >= 0 && tc < CELEB_MS) {
        celeb_fade = tc > CELEB_MS - CELEB_OUT_MS ? (int)((CELEB_MS - tc) * 256 / CELEB_OUT_MS) : 256;
        const int g = (int)(celeb_fade * (0.75f + 0.25f * sinf(tc * 0.006f)));
        glow        = g > glow ? g : glow;
    }
    if (glow > 0) {
        blit(SPR_GLOW, 0, 0, glow, strip, y0, y1);
    }

    for (int i = 0; i < 2; i++) {  // candle flames flicker
        const float f   = 0.82f + 0.12f * sinf(now * 0.013f + i * 2) + 0.06f * sinf(now * 0.031f + i);
        const int   jit = (int)(sinf(now * 0.02f + i * 3) > 0.7f);
        blit_at(SPR_FLAME, FX, FY, s_candle_x[i], FLAME_Y - jit, 256, (int)(256 * f), strip, y0, y1);
    }

    const int placed = s.count < SLOT_COUNT ? (int)s.count : SLOT_COUNT;
    for (int i = 0; i < placed; i++) {
        blit_at(SPR_FLOWER_0 + s_slots[i].kind, FX, FY, s_slots[i].x, s_slots[i].y, 256, 256, strip, y0, y1);
    }

    for (int i = 0; i < MAX_FLIGHTS; i++) {  // flowers on their way: an arc from below the altar into the vase
        const flight_t* fl = &s.flights[i];
        if (!fl->t0) {
            continue;
        }
        const float p  = clampf((float)(now - fl->t0) / FLIGHT_MS, 0, 1);
        const float e  = ease_out(p);
        const float sx = 120 + fl->side * 20, sy = 300;
        const float cx = 120 + fl->side * 80, cy = 150;  // Bezier control point
        const float tx = s_slots[fl->slot].x, ty = s_slots[fl->slot].y;
        const float x  = (1 - e) * (1 - e) * sx + 2 * (1 - e) * e * cx + e * e * tx;
        const float y  = (1 - e) * (1 - e) * sy + 2 * (1 - e) * e * cy + e * e * ty;
        blit_at(SPR_FLOWER_0 + fl->kind, FX, FY, (int)x, (int)y, (int)(256 * (1.7f - 0.7f * e)), 256, strip, y0, y1);
        if (p > 0.1f) {  // a little trail of sparkle
            const float q = e - 0.12f < 0 ? 0 : e - 0.12f;
            const float x2 = (1 - q) * (1 - q) * sx + 2 * (1 - q) * q * cx + q * q * tx;
            const float y2 = (1 - q) * (1 - q) * sy + 2 * (1 - q) * q * cy + q * q * ty;
            blit_at(SPR_SPARKLE_SMALL, FX, FY, (int)x2, (int)y2, 256, 200, strip, y0, y1);
        }
    }

    if (now - s.burst_t < BURST_MS) {  // sparkles fly out from the flower that just landed
        const float p  = (float)(now - s.burst_t) / BURST_MS;
        const int   bx = s_slots[s.burst_slot].x, by = s_slots[s.burst_slot].y;
        for (int k = 0; k < 6; k++) {
            const float a = k * 1.047f + 0.4f;
            const float r = 6 + 24 * ease_out(p);
            blit_at(k & 1 ? SPR_SPARKLE_SMALL : SPR_SPARKLE_BIG, FX, FY, bx + (int)(r * cosf(a)), by + (int)(r * sinf(a)),
                    (int)(256 * (1.1f - 0.6f * p)), (int)(256 * (1 - p)), strip, y0, y1);
        }
    }

    draw_ambient(strip, y0, y1);
    if (celeb_fade) {
        const float t_s = tc / 1000.0f;
        for (int i = 0; i < RAIN_PETALS; i++) {
            draw_petal(&s.rain[i], t_s, celeb_fade, strip, y0, y1);  // fades out with the ribbon
        }
    }

    /* counter */
    char txt[12];
    snprintf(txt, sizeof(txt), "%lu", (unsigned long)s.count);
    const ui_font_t* font  = strlen(txt) > 5 ? &ui_font_lb : &ui_font_xl;
    int              dy    = 0;
    uint16_t         color = COLOR_COUNT;
    if (now - s.bump_t < BUMP_MS) {
        dy    = -(int)lroundf(7 * sinf((float)(now - s.bump_t) / BUMP_MS * (float)M_PI));
        color = COLOR_COUNT_BUMP;
    }
    ui_text(font, txt, W / 2 - ui_text_width(font, txt) / 2, PILL_Y + PILL_H / 2 - (font->cap + font->ascent) / 2 + dy,
            color, strip, y0, y1);

    for (int i = 0; i < MAX_POPS; i++) {
        const int64_t t = now - s.pop_t[i];
        if (t < 0 || t >= POP_MS) {
            continue;
        }
        const float a = t < 250 ? t / 250.0f : (t > POP_MS - 500 ? (POP_MS - t) / 500.0f : 1);
        blit_at(SPR_FLOWER_1, FX, FY, 200, 84, 230, (int)(230 * clampf(a, 0, 1)), strip, y0, y1);
    }

    if (celeb_fade && tc >= BANNER_DELAY) {
        const float drop = ease_out_back(clampf((float)(tc - BANNER_DELAY) / BANNER_DROP_MS, 0, 1));
        const int   bdy  = (int)lroundf(-BANNER_DROP * (1 - drop));
        const int   in   = tc - BANNER_DELAY < 150 ? (int)((tc - BANNER_DELAY) * 256 / 150) : 256;
        const int   mul  = in < celeb_fade ? in : celeb_fade;
        blit(SPR_BANNER, 0, bdy, mul, strip, y0, y1);
        blessing_text(s.blessing, bdy, mul, strip, y0, y1);
    }

    const int64_t tt = now - s.toast_t;
    if (tt >= 0 && tt < TOAST_MS) {
        blit(SPR_TOAST_RESET, 0, 0, tt < TOAST_MS - TOAST_FADE ? 256 : (int)((TOAST_MS - tt) * 256 / TOAST_FADE), strip,
             y0, y1);
    }
}

static void render_strip(uint16_t* strip, int y0, int y1)
{
    if (s.logo) {
        render_logo(strip, y0, y1);
    } else {
        render_altar(strip, y0, y1);
    }
}

static bool on_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &woken);
    return woken == pdTRUE;
}

static void draw_frame(uint16_t* bufs[2])
{
    int  cur     = 0;
    bool pending = false;
    for (int y0 = 0; y0 < H; y0 += LINES_PER_CHUNK) {
        int y1 = y0 + LINES_PER_CHUNK < H ? y0 + LINES_PER_CHUNK : H;
        render_strip(bufs[cur], y0, y1);
        if (pending) {
            xSemaphoreTake(s_trans_done, portMAX_DELAY);
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y0, W, y1, bufs[cur]);
        pending = true;
        cur ^= 1;
    }
    xSemaphoreTake(s_trans_done, portMAX_DELAY);
}

/* ---------------------------------------------------------------------------------------------*/

void app_main(void)
{
    count_load();
    ESP_ERROR_CHECK(lcd_init(&s_panel, &s_io, MAX_TRANSFER_SIZE));

    s_trans_done      = xSemaphoreCreateBinary();
    uint16_t* bufs[2] = {heap_caps_malloc(MAX_TRANSFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                         heap_caps_malloc(MAX_TRANSFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)};
    assert(s_trans_done && bufs[0] && bufs[1]);
    const esp_lcd_panel_io_callbacks_t cbs = {.on_color_trans_done = on_trans_done};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL));

    const int64_t t0 = now_ms();
    s.logo           = true;
    s.blessing       = -1;
    s.burst_t = s.glow_t = s.bump_t = s.toast_t = s.celeb_t = t0 - 100000;
    for (int i = 0; i < MAX_POPS; i++) {
        s.pop_t[i] = t0 - 100000;
    }
    for (int i = 0; i < AMBIENT_PETALS; i++) {
        petal_init(&s.ambient[i], 0, H, 16, 28);
    }

    s_now = t0;
    draw_frame(bufs);
    lcd_backlight(true);
    button_init();
    ESP_LOGI(TAG, "ready: click BOOT to offer a flower, hold 1 s to reset the count");

    bool long_fired = false;
    while (1) {
        const int64_t  now = now_ms();
        button_event_t ev;
        while ((ev = button_get(0)) != BUTTON_NONE) {
            if (ev == BUTTON_DOWN) {
                long_fired = false;
                if (!s.logo) {
                    offer(now);  // respond on press, like the wooden fish
                }
            } else if (ev == BUTTON_LONG) {  // BUTTON_LONG_MS = 1000
                long_fired = true;
                if (!s.logo) {
                    count_reset(now);
                }
            } else if (ev == BUTTON_UP && !long_fired && s.logo) {
                s.logo = false;  // enter the altar (not counted) with the blessing of the day
                start_celebration(now);
            }
        }
        update(now);
        s_now = now;
        draw_frame(bufs);
        vTaskDelay(1);
    }
}
