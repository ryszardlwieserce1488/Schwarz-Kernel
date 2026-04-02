// font_engine.cpp
#include <stdint.h>
#include <stddef.h>

extern "C" void* malloc(size_t size);
extern "C" void free(void* ptr);

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_no_stdio
#define STBTT_no_stdlib
#define STBTT_NO_STDLIB
#define STBTT_NO_CRT

#define STBTT_ifloor(x)    ((int)(x))
#define STBTT_iceil(x)     ((int)((x) + 0.99999f))
#define STBTT_fabs(x)      ((x) < 0 ? -(x) : (x))

inline float my_sqrt(float x) {
    float res;
    asm("fsqrt" : "=t" (res) : "0" (x));
    return res;
}
#define STBTT_sqrt(x)      my_sqrt(x)

#define STBTT_pow(x,y)     0.0f
#define STBTT_fmod(x,y)    0.0f

#define STBTT_malloc(x,u)  malloc(x)
#define STBTT_free(x,u)    free(x)

#include "stb_truetype.h"

extern "C" unsigned char _binary_times_ttf_start[];
extern "C" unsigned char _binary_inconsolata_ttf_start[];
extern "C" unsigned char _binary_consolas_ttf_start[];
extern "C" unsigned char _binary_segoeuithis_ttf_start[];

struct FontSlot {
    unsigned char* data;
    stbtt_fontinfo  info;
    bool            ready;
};

static FontSlot g_fonts[] = {
    { _binary_times_ttf_start,       {}, false },  // 0 = FONT_TNR
    { _binary_inconsolata_ttf_start, {}, false },  // 1 = FONT_INCONSOLATA
    { _binary_consolas_ttf_start,    {}, false },  // 2 = FONT_CONSOLAS
    { _binary_segoeuithis_ttf_start,       {}, false },  // 3 = FONT_SEGOE
};
static const int FONT_COUNT = 4;

static int g_active_font = 0;  // indeks zamiast enum

static FontSlot* active() { return &g_fonts[g_active_font]; }
static bool ensure_font();

static bool ensure_font_slot(FontSlot* slot) {
    if (!slot->ready)
        slot->ready = stbtt_InitFont(&slot->info, slot->data, 0) != 0;
    return slot->ready;
}

// Eksport dla shella
extern "C" void set_active_font(int index) {
    if (index >= 0 && index < FONT_COUNT)
        g_active_font = index;
}

extern "C" int get_active_font() { return g_active_font; }

extern "C" bool is_active_font_available() {
    return ensure_font();
}

static bool ensure_font() {
    return ensure_font_slot(active());
}

static float font_scale(int size) {
    return stbtt_ScaleForPixelHeight(&active()->info, (float)size);
}

static int round_to_int(float value) {
    return (value >= 0.0f) ? (int)(value + 0.5f) : (int)(value - 0.5f);
}

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static void blend_pixel(uint32_t* fb, int fb_width, int x, int y, unsigned char coverage) {
    if (coverage == 0) return;

    uint32_t dst = fb[y * fb_width + x];
    uint32_t dst_r = (dst >> 16) & 0xFF;
    uint32_t dst_g = (dst >> 8) & 0xFF;
    uint32_t dst_b = dst & 0xFF;
    uint32_t alpha = coverage;

    uint32_t out_r = dst_r + (((255 - dst_r) * alpha) / 255);
    uint32_t out_g = dst_g + (((255 - dst_g) * alpha) / 255);
    uint32_t out_b = dst_b + (((255 - dst_b) * alpha) / 255);

    fb[y * fb_width + x] = (out_r << 16) | (out_g << 8) | out_b;
}

extern "C" int get_font_baseline(int size) {
    if (!ensure_font()) return size;

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&active()->info, &ascent, &descent, &line_gap);
    return round_to_int((float)ascent * font_scale(size));
}

extern "C" int get_font_line_height(int size) {
    if (!ensure_font()) return size;

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&active()->info, &ascent, &descent, &line_gap);
    return round_to_int((float)(ascent - descent + line_gap) * font_scale(size));
}

extern "C" int get_glyph_advance(int codepoint, int size) {
    if (!ensure_font()) return size / 2;

    int advance = 0;
    stbtt_GetCodepointHMetrics(&active()->info, codepoint, &advance, 0);
    return round_to_int((float)advance * font_scale(size));
}

extern "C" int get_glyph_kern_advance(int prev_codepoint, int codepoint, int size) {
    if (!ensure_font() || prev_codepoint < 0) return 0;

    int kern = stbtt_GetCodepointKernAdvance(&active()->info, prev_codepoint, codepoint);
    return round_to_int((float)kern * font_scale(size));
}

extern "C" void get_glyph_draw_box(
    int codepoint,
    int size,
    int* left,
    int* top,
    int* right,
    int* bottom
) {
    if (!ensure_font()) {
        if (left) *left = 0;
        if (top) *top = 0;
        if (right) *right = size / 2;
        if (bottom) *bottom = size;
        return;
    }

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float scale = font_scale(size);
    stbtt_GetCodepointBitmapBox(&active()->info, codepoint, scale, scale, &x0, &y0, &x1, &y1);

    if (left) *left = x0;
    if (top) *top = y0;
    if (right) *right = x1;
    if (bottom) *bottom = y1;
}

extern "C" void draw_times_new_roman_glyph(
    uint32_t* fb,
    int fb_width,
    int fb_height,
    int codepoint,
    int x,
    int baseline_y,
    int size
) {
    if (!ensure_font()) return;

    float scale = font_scale(size);
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(&active()->info, 0, scale, codepoint, &w, &h, &xoff, &yoff);
    if (!bitmap) return;

    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int fx = x + xoff + col;
            int fy = baseline_y + yoff + row;
            if (fx < 0 || fx >= fb_width || fy < 0 || fy >= fb_height) continue;

            blend_pixel(fb, fb_width, fx, fy, bitmap[row * w + col]);
        }
    }

    stbtt_FreeBitmap(bitmap, 0);
}

extern "C" void draw_times_new_roman_text(
    uint32_t* fb,
    int fb_width,
    int fb_height,
    const char* text,
    int x,
    int baseline_y,
    int size
) {
    if (!ensure_font()) return;

    int cur_x = x;
    int prev_codepoint = -1;

    for (int i = 0; text[i]; i++) {
        unsigned char codepoint = (unsigned char)text[i];
        cur_x += get_glyph_kern_advance(prev_codepoint, codepoint, size);
        draw_times_new_roman_glyph(fb, fb_width, fb_height, codepoint, cur_x, baseline_y, size);
        cur_x += get_glyph_advance(codepoint, size);
        prev_codepoint = codepoint;
    }
}

extern "C" int get_text_width(const char* text, int size) {
    if (!ensure_font()) return 0;

    int width = 0;
    int prev_codepoint = -1;
    for (int i = 0; text[i]; i++) {
        unsigned char codepoint = (unsigned char)text[i];
        width += get_glyph_kern_advance(prev_codepoint, codepoint, size);
        width += get_glyph_advance(codepoint, size);
        prev_codepoint = codepoint;
    }

    return max_int(width, 0);
}
