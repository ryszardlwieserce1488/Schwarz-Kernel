// font_engine.cpp
//
// Uwagi architektoniczne:
//   - Wyłącznie x86-64; używamy SSE2 (sqrtss) i __builtin_expect.
//   - Cache glifów: open-addressing hash map, klucz = (codepoint, size, font).
//     Gotowe na Unicode — klucz jest 32-bitowym int, działa dla dowolnego codepoint'a.
//   - Zero alokacji w hot path renderowania (MakeCodepointBitmap do bufora w slocie).
//   - extern "C" na całym publicznym API — kompatybilne z callsitami w C i C++.
//   - Wejście draw_text/get_text_width: UTF-8 (shell koduje codepoints → UTF-8
//     przez shell_sync_line_buf_from_codepoints). Parser UTF-8 wbudowany,
//     obsługuje pełny zakres U+0000–U+10FFFF, przy błędach zwraca U+FFFD.

#include <stdint.h>
#include <stddef.h>

extern "C" void* malloc(size_t size);
extern "C" void  free(void* ptr);

// ---------------------------------------------------------------------------
// STB TrueType — konfiguracja przed #include
// ---------------------------------------------------------------------------
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_no_stdio
#define STBTT_NO_STDLIB
#define STBTT_NO_CRT

#define STBTT_ifloor(x)   ((int)(x))
#define STBTT_iceil(x)    ((int)((x) + 0.99999f))
#define STBTT_fabs(x)     ((x) < 0.0f ? -(x) : (x))
#define STBTT_pow(x, y)   0.0f
#define STBTT_fmod(x, y)  0.0f
#define STBTT_malloc(x,u) malloc(x)
#define STBTT_free(x,u)   free(x)

// sqrtss — SSE2, zawsze dostępne na x86-64, szybsze niż x87 fsqrt.
static inline float fast_sqrt(float x) {
    float r;
    __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x));
    return r;
}
// Alias dla math.h który używa my_sqrt bezpośrednio
#define my_sqrt(x)    fast_sqrt(x)
#define STBTT_sqrt(x) fast_sqrt(x)

#include "stb_truetype.h"

#include "fonts.h"

// Symbole są teraz definiowane w generowanym fonts.h / fonts.cpp

struct FontSlot {
    stbtt_fontinfo info;
    bool           ready;
};

static FontSlot g_font_slots[32]; // Max 32 fonts
static int g_active_font = 0;

// Inicjalizuje slot leniwie; zwraca wskaźnik lub nullptr przy błędzie.
static FontSlot* get_ready_font() {
    if (g_active_font < 0 || g_active_font >= G_EMBEDDED_FONTS_COUNT) return nullptr;
    if (g_active_font >= 32) return nullptr;

    FontSlot* slot = &g_font_slots[g_active_font];
    if (!slot->ready)
        slot->ready = (stbtt_InitFont(&slot->info, G_EMBEDDED_FONTS[g_active_font].data, 0) != 0);
    return slot->ready ? slot : nullptr;
}

// ---------------------------------------------------------------------------
// Glyph cache — open-addressing hash map
//
// Klucz: (codepoint, size, font_idx) — działa dla pełnego Unicode.
// Rozmiar tablicy musi być potęgą 2 (maska zamiast modulo).
//
// Stałe do dostrojenia:
//   GLYPH_CACHE_CAPACITY  — liczba slotów; więcej = mniej kolizji.
//                           Przy pełnym Unicode i wielu rozmiarach zwiększ do 4096.
//   GLYPH_BITMAP_STRIDE   — max szerokość/wysokość bitmamy glyfu w pikselach.
//                           Dla 16px fontu wystarczy 24; dla 48px daj 64.
//                           Glify większe niż ta wartość nie trafiają do cache
//                           (rasteryzowane za każdym razem — akceptowalne dla
//                           rzadkich/gigantycznych glifów).
//
// Aktualne wartości dobrane pod: Latin Extended + kilka rozmiarów, ~12–32px.
// ---------------------------------------------------------------------------
static constexpr int GLYPH_CACHE_CAPACITY = 512;   // musi być potęgą 2
static constexpr int GLYPH_BITMAP_STRIDE = 48;    // max bok bitmamy [px]
static constexpr int GLYPH_BITMAP_BYTES = GLYPH_BITMAP_STRIDE * GLYPH_BITMAP_STRIDE;

static_assert((GLYPH_CACHE_CAPACITY& (GLYPH_CACHE_CAPACITY - 1)) == 0,
    "GLYPH_CACHE_CAPACITY musi byc potega 2");

struct CachedGlyph {
    // Klucz
    int     codepoint;
    int     size;
    int     font_idx;
    bool    occupied;   // slot zajęty (odróżnia pusty slot od codepoint==0)

    // Dane glyfu
    uint8_t bitmap[GLYPH_BITMAP_BYTES];
    int     w, h;
    int     xoff, yoff;
    int     advance;
};

static CachedGlyph g_cache[GLYPH_CACHE_CAPACITY];

// ---------------------------------------------------------------------------
// Hash — łączy trzy pola w jeden uint32, miesza bity żeby uniknąć skupisk.
// ---------------------------------------------------------------------------
static inline uint32_t glyph_hash(int codepoint, int size, int font_idx) {
    uint32_t h = (uint32_t)codepoint * 2654435761u  // Knuth multiplicative hash
        ^ (uint32_t)size * 40503u
        ^ (uint32_t)font_idx * 2246822519u;
    h ^= h >> 16;
    return h & (uint32_t)(GLYPH_CACHE_CAPACITY - 1);  // maska zamiast modulo
}

// Szuka glyfu w cache. Zwraca wskaźnik lub nullptr jeśli brak.
static CachedGlyph* cache_lookup(int codepoint, int size, int font_idx) {
    uint32_t idx = glyph_hash(codepoint, size, font_idx);

    for (int probe = 0; probe < GLYPH_CACHE_CAPACITY; probe++) {
        CachedGlyph* slot = &g_cache[(idx + (uint32_t)probe) & (uint32_t)(GLYPH_CACHE_CAPACITY - 1)];

        if (!slot->occupied)
            return nullptr;  // pusty slot = nie ma dalej (brak lazy deletion)

        if (slot->codepoint == codepoint &&
            slot->size == size &&
            slot->font_idx == font_idx)
            return slot;
    }
    return nullptr;
}

// Znajduje wolny slot dla nowego wpisu.
static CachedGlyph* cache_find_slot(int codepoint, int size, int font_idx) {
    uint32_t idx = glyph_hash(codepoint, size, font_idx);

    for (int probe = 0; probe < GLYPH_CACHE_CAPACITY; probe++) {
        CachedGlyph* slot = &g_cache[(idx + (uint32_t)probe) & (uint32_t)(GLYPH_CACHE_CAPACITY - 1)];
        if (!slot->occupied)
            return slot;
    }
    // Tablica pełna — nadpisz pozycję bazową (bardzo rzadki przypadek).
    return &g_cache[idx];
}

// Inwalidacja przy zmianie fontu — czyści tylko flagę, nie zeruje bitmap.
static void cache_invalidate() {
    for (int i = 0; i < GLYPH_CACHE_CAPACITY; i++)
        g_cache[i].occupied = false;
}

// ---------------------------------------------------------------------------
// Narzędzia
// ---------------------------------------------------------------------------
[[nodiscard]] static constexpr int max_int(int a, int b) noexcept { return a > b ? a : b; }

static inline int round_to_int(float v) noexcept {
    return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static inline float font_scale(const FontSlot* slot, int size) noexcept {
    return stbtt_ScaleForPixelHeight(&slot->info, (float)size);
}

// ---------------------------------------------------------------------------
// Blending
//
// Formuła: białe glify (src = biały) na dowolnym tle.
//   out = dst + (255 - dst) * alpha / 256
// >> 8 zamiast / 255 — błąd maks. 1/256, niewidoczny, eliminuje dzielenie.
// __builtin_expect: statystycznie większość pikseli bitmamy glyfu jest pusta.
// ---------------------------------------------------------------------------
__attribute__((always_inline))
static inline void blend_pixel(
    uint32_t* __restrict__ fb,
    int fb_width,
    int x, int y,
    uint8_t coverage
) noexcept {
    if (__builtin_expect(coverage == 0, 1)) return;

    uint32_t dst = fb[y * fb_width + x];
    uint32_t dst_r = (dst >> 16) & 0xFF;
    uint32_t dst_g = (dst >> 8) & 0xFF;
    uint32_t dst_b = dst & 0xFF;

    fb[y * fb_width + x] =
        ((dst_r + (((255u - dst_r) * coverage) >> 8)) << 16) |
        ((dst_g + (((255u - dst_g) * coverage) >> 8)) << 8) |
        (dst_b + (((255u - dst_b) * coverage) >> 8));
}

// ---------------------------------------------------------------------------
// Rasteryzacja do cache
// ---------------------------------------------------------------------------
static const CachedGlyph* rasterize_to_cache(int codepoint, int size, FontSlot* slot) {
    float scale = font_scale(slot, size);

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&slot->info, codepoint, scale, scale,
        &x0, &y0, &x1, &y1);
    int w = x1 - x0;
    int h = y1 - y0;

    if (w <= 0 || h <= 0)
        return nullptr;  // glyf niewidoczny (spacja itp.)

    if (w > GLYPH_BITMAP_STRIDE || h > GLYPH_BITMAP_STRIDE)
        return nullptr;  // za duży na statyczny bufor — rasteryzuj bez cache

    CachedGlyph* entry = cache_find_slot(codepoint, size, g_active_font);

    // Zero alokacji — rasteryzacja wprost do bufora w slocie.
    stbtt_MakeCodepointBitmap(&slot->info,
        entry->bitmap, w, h, GLYPH_BITMAP_STRIDE,
        scale, scale, codepoint);

    int advance, lsb;
    stbtt_GetCodepointHMetrics(&slot->info, codepoint, &advance, &lsb);

    entry->codepoint = codepoint;
    entry->size = size;
    entry->font_idx = g_active_font;
    entry->w = w;
    entry->h = h;
    entry->xoff = x0;
    entry->yoff = y0;
    entry->advance = round_to_int((float)advance * scale);
    entry->occupied = true;

    return entry;
}

static const CachedGlyph* get_glyph(int codepoint, int size) {
    FontSlot* slot = get_ready_font();
    if (!slot) return nullptr;

    const CachedGlyph* hit = cache_lookup(codepoint, size, g_active_font);
    return hit ? hit : rasterize_to_cache(codepoint, size, slot);
}

static int uncached_advance(int codepoint, int size, FontSlot* slot) {
    int advance;
    stbtt_GetCodepointHMetrics(&slot->info, codepoint, &advance, nullptr);
    return round_to_int((float)advance * font_scale(slot, size));
}

// ---------------------------------------------------------------------------
// API publiczne — extern "C", kompatybilne z C i C++
// ---------------------------------------------------------------------------

extern "C" void set_active_font(int index) {
    if (index >= 0 && index < G_EMBEDDED_FONTS_COUNT && index != g_active_font) {
        g_active_font = index;
        cache_invalidate();
    }
}

extern "C" int get_active_font() {
    return g_active_font;
}

extern "C" bool is_active_font_available() {
    return get_ready_font() != nullptr;
}

extern "C" int get_font_baseline(int size) {
    FontSlot* slot = get_ready_font();
    if (!slot) return size;

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&slot->info, &ascent, &descent, &line_gap);
    return round_to_int((float)ascent * font_scale(slot, size));
}

extern "C" int get_font_line_height(int size) {
    FontSlot* slot = get_ready_font();
    if (!slot) return size;

    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&slot->info, &ascent, &descent, &line_gap);
    return round_to_int((float)(ascent - descent + line_gap) * font_scale(slot, size));
}

extern "C" int get_glyph_advance(int codepoint, int size) {
    FontSlot* slot = get_ready_font();
    if (!slot) return size / 2;

    const CachedGlyph* g = cache_lookup(codepoint, size, g_active_font);
    if (g) return g->advance;

    const CachedGlyph* rast = rasterize_to_cache(codepoint, size, slot);
    if (rast) return rast->advance;

    return uncached_advance(codepoint, size, slot);
}

extern "C" int get_glyph_kern_advance(int prev_codepoint, int codepoint, int size) {
    if (prev_codepoint < 0) return 0;
    FontSlot* slot = get_ready_font();
    if (!slot) return 0;
    int kern = stbtt_GetCodepointKernAdvance(&slot->info, prev_codepoint, codepoint);
    return round_to_int((float)kern * font_scale(slot, size));
}

struct GlyphBox { int left, top, right, bottom; };

extern "C" GlyphBox get_glyph_box(int codepoint, int size) {
    FontSlot* slot = get_ready_font();
    if (!slot) return { 0, 0, size / 2, size };

    float scale = font_scale(slot, size);
    GlyphBox box;
    stbtt_GetCodepointBitmapBox(&slot->info, codepoint, scale, scale,
        &box.left, &box.top, &box.right, &box.bottom);
    return box;
}

extern "C" void draw_glyph(
    uint32_t* __restrict__ fb,
    int fb_width,
    int fb_height,
    int codepoint,
    int x,
    int baseline_y,
    int size
) {
    const CachedGlyph* g = get_glyph(codepoint, size);
    if (!g) return;

    for (int row = 0; row < g->h; row++) {
        int fy = baseline_y + g->yoff + row;
        if ((unsigned)fy >= (unsigned)fb_height) continue;

        for (int col = 0; col < g->w; col++) {
            int fx = x + g->xoff + col;
            if ((unsigned)fx >= (unsigned)fb_width) continue;

            blend_pixel(fb, fb_width, fx, fy,
                g->bitmap[row * GLYPH_BITMAP_STRIDE + col]);
        }
    }
}

// ---------------------------------------------------------------------------
// Parser UTF-8 — bez alokacji, bez CRT.
//
// Zwraca codepoint spod text[i] i przesuwa i o dodatkowe bajty sekwencji
// (petla for zinkrementuje i o 1 sama — lacznie przeskok = dlugosc sekwencji).
//
// Obsluga bledow: uszkodzony bajt kontynuacji lub obcieta sekwencja
// zwraca U+FFFD (replacement character) bez wyjscia poza string.
//
// Zakresy:
//   1 bajt  0x00-0x7F   U+0000-U+007F   ASCII
//   2 bajty 0xC2-0xDF   U+0080-U+07FF   Latin Extended (polskie znaki tutaj)
//   3 bajty 0xE0-0xEF   U+0800-U+FFFF   BMP (arabski, CJK, ...)
//   4 bajty 0xF0-0xF4   U+10000-U+10FFFF emoji i reszta
// ---------------------------------------------------------------------------
static inline int utf8_decode(const char* text, int& i) {
    unsigned char b0 = (unsigned char)text[i];

    // ASCII — najczestszy przypadek, osobna galaz dla predyktora skokow
    if (__builtin_expect(b0 < 0x80, 1))
        return (int)b0;

    // Pomocnik: bajt kontynuacji na pozycji i+offset.
    // Zwraca bity danych (6 bitow) lub -1 przy bledzie/koncu stringa.
    auto cont = [&](int offset) -> int {
        unsigned char b = (unsigned char)text[i + offset];
        if (b == '\0' || (b & 0xC0) != 0x80) return -1;
        return b & 0x3F;
        };

    if ((b0 & 0xE0) == 0xC0) {                      // 110xxxxx: 2 bajty
        int b1 = cont(1);
        if (b1 < 0) return 0xFFFD;
        i += 1;
        return ((b0 & 0x1F) << 6) | b1;
    }

    if ((b0 & 0xF0) == 0xE0) {                      // 1110xxxx: 3 bajty
        int b1 = cont(1), b2 = cont(2);
        if (b1 < 0 || b2 < 0) return 0xFFFD;
        i += 2;
        return ((b0 & 0x0F) << 12) | (b1 << 6) | b2;
    }

    if ((b0 & 0xF8) == 0xF0) {                      // 11110xxx: 4 bajty
        int b1 = cont(1), b2 = cont(2), b3 = cont(3);
        if (b1 < 0 || b2 < 0 || b3 < 0) return 0xFFFD;
        i += 3;
        return ((b0 & 0x07) << 18) | (b1 << 12) | (b2 << 6) | b3;
    }

    return 0xFFFD;                                   // bajt startowy nieprawidlowy
}

extern "C" void draw_text(
    uint32_t* __restrict__ fb,
    int fb_width,
    int fb_height,
    const char* text,
    int x,
    int baseline_y,
    int size
) {
    int cur_x = x;
    int prev = -1;

    for (int i = 0; text[i]; i++) {
        int cp = utf8_decode(text, i);  // i przeskakuje o 0-3 bajty kontynuacji

        cur_x += get_glyph_kern_advance(prev, cp, size);
        draw_glyph(fb, fb_width, fb_height, cp, cur_x, baseline_y, size);
        cur_x += get_glyph_advance(cp, size);

        prev = cp;
    }
}

extern "C" int get_text_width(const char* text, int size) {
    int width = 0;
    int prev = -1;

    for (int i = 0; text[i]; i++) {
        int cp = utf8_decode(text, i);

        width += get_glyph_kern_advance(prev, cp, size);
        width += get_glyph_advance(cp, size);
        prev = cp;
    }

    return max_int(width, 0);
}

// ---------------------------------------------------------------------------
// Aliasy dla starych nazw — usuń po migracji callsitów
// ---------------------------------------------------------------------------

extern "C" void draw_times_new_roman_glyph(
    uint32_t* fb, int fb_w, int fb_h,
    int codepoint, int x, int baseline_y, int size)
{
    draw_glyph(fb, fb_w, fb_h, codepoint, x, baseline_y, size);
}

extern "C" void draw_times_new_roman_text(
    uint32_t* fb, int fb_w, int fb_h,
    const char* text, int x, int baseline_y, int size)
{
    draw_text(fb, fb_w, fb_h, text, x, baseline_y, size);
}

extern "C" void get_glyph_draw_box(
    int codepoint, int size,
    int* left, int* top, int* right, int* bottom)
{
    GlyphBox box = get_glyph_box(codepoint, size);
    if (left)   *left = box.left;
    if (top)    *top = box.top;
    if (right)  *right = box.right;
    if (bottom) *bottom = box.bottom;
}