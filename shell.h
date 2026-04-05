#pragma once
#include "types.h"
#include "memory.h"
#include "keyboard.h"
#include "mouse.h"
#include "reboot.h"
#include "compiler.h"
#include "storage.h"
#include "usb.h"
#include "vfs.h"
extern "C" void fb_acquire();
extern "C" void fb_release();
extern uint32_t* g_fb;
extern uint32_t g_width;
extern uint32_t g_cursor_x;
extern uint32_t g_cursor_y;
extern uint32_t g_max_x;
extern uint32_t g_max_y;
extern uint32_t g_start_y;
extern volatile uint32_t key_buffer[256];
extern volatile uint8_t key_read;
extern volatile uint8_t key_write;
extern "C" volatile uint64_t timer_ticks;
static int line_cursor = 0;   // pozycja kursora, 0..line_len
struct HistoryEntry {
    char* text;
    HistoryEntry* prev;  // nowszy
    HistoryEntry* next;  // starszy
};
static HistoryEntry* history_newest = nullptr;
static HistoryEntry* history_oldest = nullptr;
static HistoryEntry* history_current = nullptr; // null = nie przeglądamy
static char line_saved[256];                    // kopia linii sprzed przeglądania
static bool history_browsing = false;
extern "C" void draw_times_new_roman_glyph(uint32_t* fb, int fb_width, int fb_height, int codepoint, int x, int baseline_y, int size);
extern "C" int get_text_width(const char* text, int size);
extern "C" int get_font_baseline(int size);
extern "C" int get_font_line_height(int size);
extern "C" int get_glyph_advance(int codepoint, int size);
extern "C" int get_glyph_kern_advance(int prev_codepoint, int codepoint, int size);
extern "C" void get_glyph_draw_box(int codepoint, int size, int* left, int* top, int* right, int* bottom);
extern "C" bool is_active_font_available();
extern BootVolumeHandoff g_boot_volume_handoff;

void draw_char(uint32_t* fb, uint32_t width, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(uint32_t* fb, uint32_t width, const char* str, uint32_t x, uint32_t y, uint32_t color);
void draw_rect(uint32_t* fb, uint32_t width, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void clear_screen(uint32_t* fb, uint32_t width, uint32_t height, uint32_t color);

static const uint32_t SHELL_X0 = 10;
static const int FONT_SIZE = 18;


static char line_buf[256];
static uint32_t line_codepoints[256];
static int line_pre_x[256];
static int line_clear_x[256];
static int line_clear_y[256];
static int line_clear_w[256];
static int line_clear_h[256];
static int line_prev_cp[256];
static uint8_t line_len = 0;
static bool notatnik_mode = false;
static int shell_prev_codepoint = -1;
static bool storage_scanned = false;
static bool usb_scanned = false;
static bool vfs_ready = false;
static bool shell_ttf_enabled = true;
static uint32_t edit_origin_x = SHELL_X0;
static uint32_t edit_origin_y = 0;
static char shell_current_path[260] = "C:\\";

bool shell_use_ttf() {
    return shell_ttf_enabled && is_active_font_available();
}

int shell_min(int a, int b) { return (a < b) ? a : b; }
int shell_max(int a, int b) { return (a > b) ? a : b; }

int str_len(const char* s) { int i = 0; while (s[i]) i++; return i; }
bool str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return false; a++; b++; }
    return *a == *b;
}
bool str_starts_with(const char* str, const char* prefix) {
    while (*prefix) { if (*str != *prefix) return false; str++; prefix++; }
    return true;
}
void str_copy(char* dst, const char* src, int cap) {
    if (cap <= 0) return;
    int i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}
bool str_eq_ci_text(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

int shell_font_baseline() {
    if (!shell_use_ttf()) return 8;
    return get_font_baseline(FONT_SIZE);
}

int shell_line_height() {
    if (!shell_use_ttf()) return 10;
    return get_font_line_height(FONT_SIZE);
}

int shell_descender_room() {
    int room = shell_line_height() - shell_font_baseline();
    return (room > 0) ? room : 0;
}

void shell_begin_draw() { fb_acquire(); cursor_hide_nolock(); }
void shell_end_draw() { cursor_show_nolock(); fb_release(); }

void itoa_dec(uint64_t val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[24]; int i = 0;
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    for (int j = 0; j < i; j++) buf[j] = tmp[i - j - 1];
    buf[i] = '\0';
}
void itoa_hex16(uint16_t val, char* buf) {
    static const char* hex = "0123456789ABCDEF";
    buf[0] = hex[(val >> 12) & 0xF];
    buf[1] = hex[(val >> 8) & 0xF];
    buf[2] = hex[(val >> 4) & 0xF];
    buf[3] = hex[val & 0xF];
    buf[4] = '\0';
}
void itoa_hex8(uint8_t val, char* buf) {
    static const char* hex = "0123456789ABCDEF";
    buf[0] = hex[(val >> 4) & 0xF];
    buf[1] = hex[val & 0xF];
    buf[2] = '\0';
}

void shell_append_char(char* buf, int& pos, int cap, char c) {
    if (pos + 1 >= cap) return;
    buf[pos++] = c;
    buf[pos] = '\0';
}

void shell_append_str(char* buf, int& pos, int cap, const char* str) {
    while (*str) {
        if (pos + 1 >= cap) return;
        buf[pos++] = *str++;
    }
    buf[pos] = '\0';
}

void shell_append_utf8(char* buf, int& pos, int cap, uint32_t cp) {
    if (cp <= 0x7F) {
        shell_append_char(buf, pos, cap, (char)cp);
    }
    else if (cp <= 0x7FF) {
        shell_append_char(buf, pos, cap, (char)(0xC0 | (cp >> 6)));
        shell_append_char(buf, pos, cap, (char)(0x80 | (cp & 0x3F)));
    }
    else if (cp <= 0xFFFF) {
        shell_append_char(buf, pos, cap, (char)(0xE0 | (cp >> 12)));
        shell_append_char(buf, pos, cap, (char)(0x80 | ((cp >> 6) & 0x3F)));
        shell_append_char(buf, pos, cap, (char)(0x80 | (cp & 0x3F)));
    }
    else {
        shell_append_char(buf, pos, cap, (char)(0xF0 | (cp >> 18)));
        shell_append_char(buf, pos, cap, (char)(0x80 | ((cp >> 12) & 0x3F)));
        shell_append_char(buf, pos, cap, (char)(0x80 | ((cp >> 6) & 0x3F)));
        shell_append_char(buf, pos, cap, (char)(0x80 | (cp & 0x3F)));
    }
}

int shell_decode_utf8_char(const char* s, uint32_t* out_cp) {
    uint8_t b0 = (uint8_t)s[0];
    if (b0 == 0) return 0;
    if (b0 < 0x80) { *out_cp = b0; return 1; }

    if ((b0 & 0xE0) == 0xC0) {
        uint8_t b1 = (uint8_t)s[1];
        if ((b1 & 0xC0) != 0x80) { *out_cp = '?'; return 1; }
        *out_cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0) {
        uint8_t b1 = (uint8_t)s[1], b2 = (uint8_t)s[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { *out_cp = '?'; return 1; }
        *out_cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0) {
        uint8_t b1 = (uint8_t)s[1], b2 = (uint8_t)s[2], b3 = (uint8_t)s[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) { *out_cp = '?'; return 1; }
        *out_cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) |
            ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
        return 4;
    }

    *out_cp = '?';
    return 1;
}

void shell_sync_line_buf_from_codepoints() {
    int pos = 0;
    line_buf[0] = '\0';
    for (int i = 0; i < (int)line_len; i++) {
        shell_append_utf8(line_buf, pos, (int)sizeof(line_buf), line_codepoints[i]);
    }
}

void format_size_mib(uint64_t bytes, char* buf, int cap) {
    if (cap <= 0) return;
    buf[0] = '\0';
    int pos = 0;

    uint64_t gib = 1024ull * 1024ull * 1024ull;
    uint64_t mib = 1024ull * 1024ull;
    char num[32];

    if (bytes >= gib) {
        uint64_t whole = bytes / gib;
        uint64_t frac = ((bytes % gib) * 10) / gib;
        itoa_dec(whole, num);
        shell_append_str(buf, pos, cap, num);
        shell_append_char(buf, pos, cap, '.');
        shell_append_char(buf, pos, cap, (char)('0' + (int)frac));
        shell_append_str(buf, pos, cap, " GiB");
        return;
    }

    uint64_t whole = bytes / mib;
    itoa_dec(whole, num);
    shell_append_str(buf, pos, cap, num);
    shell_append_str(buf, pos, cap, " MiB");
}
void shell_reset_text_flow() {
    shell_prev_codepoint = -1;
}

int shell_glyph_advance_for(int codepoint) {
    if (!shell_use_ttf()) return 8;
    return get_glyph_advance(codepoint, FONT_SIZE);
}

void shell_draw_codepoint_nolock(int codepoint, int x) {
    if (shell_use_ttf()) {
        draw_times_new_roman_glyph(g_fb, (int)g_width, (int)g_max_y + 1, codepoint, x, (int)g_cursor_y, FONT_SIZE);
    }
    else {
        char ch = (codepoint >= 32 && codepoint <= 126) ? (char)codepoint : '?';
        int draw_y = (int)g_cursor_y - 8;
        if (draw_y < 0) draw_y = 0;
        draw_char(g_fb, g_width, ch, (uint32_t)x, (uint32_t)draw_y, 0x00FFFFFF);
    }
}

void shell_mark_edit_origin() {
    edit_origin_x = g_cursor_x;
    edit_origin_y = g_cursor_y;
}

void shell_print_nolock(const char* str);
void shell_scroll_if_needed() {
    int line_h = shell_line_height();
    int descender = shell_descender_room();
    if (g_cursor_y + (uint32_t)descender <= g_max_y) return;

    int start_y = (int)g_start_y - shell_font_baseline();
    if (start_y < 0) start_y = 0;

    for (int y = start_y; y + line_h <= (int)g_max_y; y++) {
        for (uint32_t x = 0; x < g_width; x++) {
            g_fb[(uint32_t)y * g_width + x] = g_fb[(uint32_t)(y + line_h) * g_width + x];
        }
    }

    int clear_y = (int)g_max_y - (line_h - 1);
    if (clear_y < 0) clear_y = 0;
    draw_rect(g_fb, g_width, 0, (uint32_t)clear_y, g_width, (uint32_t)line_h, 0x00000000);
    g_cursor_y -= line_h;
}
void shell_newline_nolock() {
    g_cursor_x = SHELL_X0;
    g_cursor_y += shell_line_height();
    shell_reset_text_flow();
    shell_scroll_if_needed();
}
void shell_draw_glyph_at(int codepoint, int idx) {
    int pre_x = (int)g_cursor_x;
    int draw_x = pre_x;
    int advance = shell_glyph_advance_for(codepoint);

    if (shell_use_ttf()) {
        int kern = get_glyph_kern_advance(shell_prev_codepoint, codepoint, FONT_SIZE);
        int left = 0, top = 0, right = 0, bottom = 0;
        get_glyph_draw_box(codepoint, FONT_SIZE, &left, &top, &right, &bottom);

        draw_x = pre_x + kern;
        int vis_right = draw_x + ((right > advance) ? right : advance);
        if (vis_right > (int)g_max_x) {
            draw_x = pre_x;
        }

        shell_draw_codepoint_nolock(codepoint, draw_x);

        int cl = shell_min(draw_x + left, draw_x);
        int ct = (int)g_cursor_y + top;
        int cr = shell_max(draw_x + right, draw_x + advance);
        int cb = (int)g_cursor_y + bottom;

        line_clear_x[idx] = cl;
        line_clear_y[idx] = ct;
        line_clear_w[idx] = cr - cl + 1;
        line_clear_h[idx] = cb - ct + 1;
    }
    else {
        shell_draw_codepoint_nolock(codepoint, draw_x);
        int draw_y = (int)g_cursor_y - 8;
        if (draw_y < 0) draw_y = 0;
        line_clear_x[idx] = draw_x;
        line_clear_y[idx] = draw_y;
        line_clear_w[idx] = 8;
        line_clear_h[idx] = 8;
    }

    line_pre_x[idx] = pre_x;
    line_prev_cp[idx] = shell_prev_codepoint;
    g_cursor_x = (uint32_t)(draw_x + advance);
    shell_prev_codepoint = codepoint;
}
void shell_redraw_input_line_nolock() {
    int line_h = shell_line_height();
    int top_y = (int)edit_origin_y - shell_font_baseline();
    if (top_y < 0) top_y = 0;
    draw_rect(g_fb, g_width, edit_origin_x, (uint32_t)top_y, g_width - edit_origin_x, (uint32_t)line_h, 0x00000000);

    g_cursor_x = edit_origin_x;
    g_cursor_y = edit_origin_y;
    shell_prev_codepoint = -1;

    for (int i = 0; i < (int)line_len; i++) {
        shell_draw_glyph_at((int)line_codepoints[i], i);
    }

    if (line_cursor <= 0) {
        g_cursor_x = edit_origin_x;
        shell_prev_codepoint = -1;
    }
    else if (line_cursor < (int)line_len) {
        g_cursor_x = (uint32_t)line_pre_x[line_cursor];
        shell_prev_codepoint = (int)line_codepoints[line_cursor - 1];
    }
    else {
        g_cursor_x = edit_origin_x;
        shell_prev_codepoint = -1;
        for (int i = 0; i < (int)line_len; i++) {
            g_cursor_x += (uint32_t)shell_glyph_advance_for((int)line_codepoints[i]);
            shell_prev_codepoint = (int)line_codepoints[i];
        }
    }
}
static bool cursor_visible = false;

void shell_draw_text_cursor_nolock(bool show) {
    // Kursor to pionowa kreska 2px szeroka, wysokość line_height
    int h = shell_line_height();
    int x = (int)g_cursor_x;
    int y = (int)g_cursor_y - shell_font_baseline();
    if (x < 0 || x + 2 >(int)g_width) return;
    if (y < 0) y = 0;
    uint32_t color = show ? 0xFFFFFFFF : 0x00000000;
    draw_rect(g_fb, g_width, (uint32_t)x, (uint32_t)y, 2, (uint32_t)h, color);
    cursor_visible = show;
}

void shell_draw_text_cursor(bool show) {
    shell_begin_draw();
    shell_draw_text_cursor_nolock(show);
    shell_end_draw();
}

void shell_hide_text_cursor() {
    if (cursor_visible) {
        shell_begin_draw();
        shell_draw_text_cursor_nolock(false);
        shell_end_draw();
    }
}

void shell_show_text_cursor() {
    shell_begin_draw();
    shell_draw_text_cursor_nolock(true);
    shell_end_draw();
}

void shell_reset_line_edit_state() {
    line_len = 0;
    line_cursor = 0;
    line_buf[0] = '\0';
    shell_mark_edit_origin();
}


void shell_ensure_storage_ready() {
    if (storage_scanned) return;
    storage_init();
    storage_register_boot_volume(&g_boot_volume_handoff);
    storage_scanned = true;
}

void shell_ensure_usb_ready() {
    if (usb_scanned) return;
    usb_init();
    usb_scanned = true;
}

void shell_ensure_vfs_ready() {
    if (vfs_ready) return;
    shell_ensure_storage_ready();
    shell_ensure_usb_ready();  // ← dodaj to!
    vfs_init();
    vfs_ready = true;
    if (vfs_drive_count() > 0) {
        const VfsDriveInfo* drive = vfs_get_drive(0);
        if (drive) {
            shell_current_path[0] = drive->letter;
            shell_current_path[1] = ':';
            shell_current_path[2] = '\\';
            shell_current_path[3] = '\0';
        }
    }
}

void shell_path_parent(char* path) {
    int len = str_len(path);
    while (len > 3 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        path[--len] = '\0';
    }
    while (len > 3 && path[len - 1] != '\\' && path[len - 1] != '/') {
        path[--len] = '\0';
    }
    if (len > 3) path[len] = '\0';
}

bool shell_resolve_path(const char* raw, char* out, int cap) {
    if (!raw || !out || cap < 4) return false;
    out[0] = '\0';
    while (*raw == ' ') raw++;
    if (*raw == '\0') {
        str_copy(out, shell_current_path, cap);
        return true;
    }

    if (raw[1] == ':') {
        str_copy(out, raw, cap);
    } else if (raw[0] == '\\' || raw[0] == '/') {
        out[0] = shell_current_path[0];
        out[1] = ':';
        out[2] = '\0';
        int pos = 2;
        int i = 0;
        while (raw[i] && pos + 1 < cap) out[pos++] = raw[i++];
        out[pos] = '\0';
    } else {
        str_copy(out, shell_current_path, cap);
        int pos = str_len(out);
        if (pos == 0) return false;
        if (out[pos - 1] != '\\' && out[pos - 1] != '/') {
            if (pos + 1 >= cap) return false;
            out[pos++] = '\\';
            out[pos] = '\0';
        }
        int i = 0;
        while (raw[i] && pos + 1 < cap) out[pos++] = raw[i++];
        out[pos] = '\0';
    }

    char normalized[260];
    int w = 0;
    if (out[1] != ':') return false;
    normalized[w++] = out[0];
    normalized[w++] = ':';
    normalized[w++] = '\\';
    normalized[w] = '\0';

    const char* p = out + 2;
    while (*p == '\\' || *p == '/') p++;
    char component[80];
    while (*p) {
        int c = 0;
        while (*p && *p != '\\' && *p != '/') {
            if (c + 1 < (int)sizeof(component)) component[c++] = *p;
            p++;
        }
        component[c] = '\0';
        while (*p == '\\' || *p == '/') p++;
        if (component[0] == '\0' || str_eq(component, ".")) continue;
        if (str_eq(component, "..")) {
            shell_path_parent(normalized);
            w = str_len(normalized);
            continue;
        }
        if (w > 3 && normalized[w - 1] != '\\') normalized[w++] = '\\';
        for (int i = 0; component[i] && w + 1 < cap; i++) normalized[w++] = component[i];
        normalized[w] = '\0';
    }

    str_copy(out, normalized, cap);
    return true;
}





void shell_draw_glyph_nolock(int codepoint, bool track_for_backspace) {
    if (!shell_use_ttf()) {
        int pre_x = (int)g_cursor_x;
        if (pre_x + 8 > (int)g_max_x) {
            shell_newline_nolock();
            pre_x = (int)g_cursor_x;
        }

        char ch = (codepoint >= 32 && codepoint <= 126) ? (char)codepoint : '?';
        int draw_y = (int)g_cursor_y - 8;
        if (draw_y < 0) draw_y = 0;
        draw_char(g_fb, g_width, ch, (uint32_t)pre_x, (uint32_t)draw_y, 0x00FFFFFF);

        if (track_for_backspace) {
            line_pre_x[line_len] = pre_x;
            line_prev_cp[line_len] = shell_prev_codepoint;
            line_clear_x[line_len] = pre_x;
            line_clear_y[line_len] = draw_y;
            line_clear_w[line_len] = 8;
            line_clear_h[line_len] = 8;
        }

        g_cursor_x = (uint32_t)(pre_x + 8);
        shell_prev_codepoint = (unsigned char)ch;
        return;
    }

    int kern = get_glyph_kern_advance(shell_prev_codepoint, codepoint, FONT_SIZE);
    int advance = get_glyph_advance(codepoint, FONT_SIZE);
    int left = 0, top = 0, right = 0, bottom = 0;
    get_glyph_draw_box(codepoint, FONT_SIZE, &left, &top, &right, &bottom);

    int pre_x = (int)g_cursor_x;
    int draw_x = pre_x + kern;
    int visual_right = draw_x + ((right > advance) ? right : advance);
    if (visual_right > (int)g_max_x) {
        shell_newline_nolock();
        pre_x = (int)g_cursor_x;
        draw_x = pre_x;
        kern = 0;
    }

    draw_times_new_roman_glyph(g_fb, (int)g_width, (int)g_max_y + 1, codepoint, draw_x, (int)g_cursor_y, FONT_SIZE);

    if (track_for_backspace) {
        int clear_left = shell_min(draw_x + left, draw_x);
        int clear_top = (int)g_cursor_y + top;
        int clear_right = shell_max(draw_x + right, draw_x + advance);
        int clear_bottom = (int)g_cursor_y + bottom;

        line_pre_x[line_len] = pre_x;
        line_prev_cp[line_len] = shell_prev_codepoint;
        line_clear_x[line_len] = clear_left;
        line_clear_y[line_len] = clear_top;
        line_clear_w[line_len] = clear_right - clear_left + 1;
        line_clear_h[line_len] = clear_bottom - clear_top + 1;
    }

    g_cursor_x = (uint32_t)(draw_x + advance);
    shell_prev_codepoint = codepoint;
}

void shell_putchar_nolock(char c) {
    if (c == '\n') { shell_newline_nolock(); return; }
    shell_draw_glyph_nolock((unsigned char)c, false);  // cast �eby ASCII dzia�a�o
}

void shell_putchar(char c) { shell_begin_draw(); shell_putchar_nolock(c); shell_end_draw(); }
void shell_print(const char* str) { shell_begin_draw(); shell_print_nolock(str); shell_end_draw(); }
void shell_println(const char* str) { shell_print(str); shell_putchar('\n'); }
void shell_print_nolock(const char* str) {
    while (*str) {
        uint32_t cp = 0;
        int used = shell_decode_utf8_char(str, &cp);
        if (used <= 0) break;
        if (cp == '\r') {
            str += used;
            continue;
        }
        if (cp == '\n') {
            shell_newline_nolock();
            str += used;
            continue;
        }
        shell_draw_glyph_nolock((int)cp, false);
        str += used;
    }
}

void shell_println_nolock(const char* str) {
    shell_print_nolock(str);
    shell_newline_nolock();
}
void cmd_clear() {
    shell_begin_draw();
    clear_screen(g_fb, g_width, g_max_y + 10, 0x00000000);
    g_cursor_x = SHELL_X0;
    g_cursor_y = g_start_y;
    shell_reset_line_edit_state();
    shell_reset_text_flow();
    shell_end_draw();
}
extern "C" void set_active_font(int index);
extern "C" int  get_active_font();
void cmd_font(const char* line) {
    const char* arg = line + 5;

    int idx = -1;
    if (str_eq(arg, "tnr")) idx = 0;
    else if (str_eq(arg, "inc")) idx = 1;
    else if (str_eq(arg, "con")) idx = 2;
    else if (str_eq(arg, "seg")) idx = 3;

    if (idx < 0) {
        shell_println("Nieznany font. Dostepne: tnr, inc, con, seg");
        return;
    }

    set_active_font(idx);
    shell_ttf_enabled = true;
    if (!is_active_font_available()) {
        shell_ttf_enabled = false;
        shell_println("TTF font engine nie jest dostepny.");
        return;
    }

    const char* names[] = {
        "Times New Roman", "Inconsolata", "Consolas", "Segoe UI"
    };
    shell_print("Czcionka: ");
    shell_println(names[idx]);
}
void cmd_compile(const char* line) {
    const char* src = line;
    if (str_starts_with(line, "compiler ")) src = line + 9;
    else if (str_eq(line, "compiler")) src = line + 8;
    else if (str_starts_with(line, "compile ")) src = line + 8;
    else if (str_eq(line, "compile")) src = line + 7;
    else src = line + 8;

    while (*src == ' ') src++;

    if (*src == '\0') {
        shell_println("Uzycie: compiler <kod>");
        return;
    }

    lex(src);
    parse_and_compile();

    int err_count = compile_error_count();
    if (err_count > 0) {
        shell_begin_draw();
        shell_println_nolock("Bledy kompilacji:");
        for (int i = 0; i < err_count; i++) {
            int         line_no;
            const char* msg;
            compile_get_error(i, &line_no, &msg);

            // "  [linia X] komunikat"
            char buf[16];
            shell_print_nolock("  [linia ");
            itoa_dec((uint64_t)line_no, buf);
            shell_print_nolock(buf);
            shell_print_nolock("] ");
            shell_println_nolock(msg);
        }
        shell_end_draw();
        return;
    }

    if (code_idx == 0) {
        shell_println("Blad: brak kodu do wykonania.");
        return;
    }

    int result = execute_compiled();
    if (!compile_has_explicit_return()) {
        shell_println("Brak wyniku: program nie zawiera return.");
        return;
    }

    char buf[32];
    shell_print("Wynik: ");
    itoa_dec((uint64_t)result, buf);
    shell_println(buf);
}

void cmd_help() {
    shell_println("Dostepne komendy: cd, clear, compiler, dir, disk, echo, help, mem, ticks, type, vol, notatnik, mouse, usb");
}

void cmd_restart() {
    reboot();
}

void cmd_mem() {
    char buf[32]; shell_print("Wolne: ");
    itoa_dec(memory_free_bytes() / 1024, buf); shell_print(buf); shell_println(" KB");
}

void cmd_ticks() {
    char buf[32]; itoa_dec(timer_ticks, buf);
    shell_print("Ticki: "); shell_println(buf);
}

void cmd_echo(const char* line) {
    if (str_starts_with(line, "echo ") && str_len(line) > 5) shell_println(line + 5);
    else shell_putchar('\n');
}

void cmd_notatnik() {
    notatnik_mode = true;
    shell_println("--- Tryb notatnika (ESC aby wyjsc) ---");
}
void cmd_mouse() {
    char buf[32];
    shell_begin_draw();

    shell_print_nolock("Pozycja X: ");
    itoa_dec(mouse.x, buf);
    shell_println_nolock(buf);

    shell_print_nolock("Pozycja Y: ");
    itoa_dec(mouse.y, buf);
    shell_println_nolock(buf);

    shell_print_nolock("Lewy przycisk: ");
    shell_println_nolock(mouse.left ? "Wcisniety" : "Puszczony");

    shell_print_nolock("Prawy przycisk: ");
    shell_println_nolock(mouse.right ? "Wcisniety" : "Puszczony");

    shell_print_nolock("Scroll: ");
    itoa_dec((uint64_t)(int64_t)mouse.scroll, buf);
    shell_println_nolock(buf);

    shell_print_nolock("Obsluga scrolla: ");
    shell_println_nolock(mouse_has_scroll ? "Tak (4-bajtowy pakiet)" : "Nie (3-bajtowy pakiet)");

    shell_end_draw();
}

void cmd_disk() {
    shell_ensure_storage_ready();
    shell_ensure_usb_ready();
    uint32_t count = storage_disk_count();
    uint32_t unsupported = storage_unsupported_count();
    if (count == 0) {
        shell_println("Nie wykryto obslugiwanych dyskow.");
        shell_println("Aktualnie obslugiwane: dyski SATA przez AHCI.");
        if (unsupported == 0) {
            shell_println("Nie znaleziono tez niewspieranych kontrolerow storage/USB.");
        }
    } else {
        shell_println("Obslugiwane dyski:");
    }

    for (uint32_t i = 0; i < count; i++) {
        const DiskInfo* disk = storage_get_disk(i);
        if (!disk || !disk->present) continue;

        char num[32];
        char size_buf[32];
        uint64_t total_bytes = disk->sector_count * disk->sector_size;
        format_size_mib(total_bytes, size_buf, sizeof(size_buf));

        shell_print(disk->name);
        shell_print(": ");
        shell_print(disk->transport);
        shell_print(", ");
        shell_print(disk->removable ? "removable" : "nonremovable");
        shell_print(", ");
            shell_print(storage_partition_style_name(disk->partition_style));
        if (disk->filesystem[0] != '\0') {
            shell_print(", fs ");
            shell_print(disk->filesystem);
        }
        shell_print(", ");
        shell_println(size_buf);

        if (disk->partition_count == 0) {
            shell_println("  partycje: brak lub nieodczytane");
            continue;
        }

        for (uint32_t p = 0; p < disk->partition_count; p++) {
            const DiskPartitionInfo* part = &disk->partitions[p];
            if (!part->present) continue;

            uint64_t part_bytes = part->sector_count * disk->sector_size;
            format_size_mib(part_bytes, size_buf, sizeof(size_buf));

            shell_print("  p");
            itoa_dec((uint64_t)(p + 1), num);
            shell_print(num);
            shell_print(": ");
            shell_print(part->type_name);
            if (part->filesystem[0] != '\0') {
                shell_print(", fs ");
                shell_print(part->filesystem);
            }
            shell_print(", start LBA ");
            itoa_dec(part->first_lba, num);
            shell_print(num);
            shell_print(", ");
            shell_println(size_buf);
        }
    }

    if (usb_xhci_count() > 0) {
        shell_println("USB: xHCI wykryty; sprawdz komenda 'usb' po szczegoly portow.");
    }

    if (unsupported == 0) return;

    shell_println("Niewspierane kontrolery:");
    for (uint32_t i = 0; i < unsupported; i++) {
        const UnsupportedControllerInfo* info = storage_get_unsupported(i);
        if (!info) continue;

        char num[32];
        shell_print("  ");
        shell_print(info->name);
        shell_print(" @ PCI ");
        itoa_dec(info->bus, num);
        shell_print(num);
        shell_print(":");
        itoa_dec(info->slot, num);
        shell_print(num);
        shell_print(".");
        itoa_dec(info->func, num);
        shell_print(num);
        shell_print(" - ");
        shell_println(info->reason);
    }
}

void cmd_vol() {
    shell_ensure_vfs_ready();
    uint32_t count = vfs_drive_count();
    if (count == 0) {
        shell_println("Brak zamontowanych woluminow FAT32.");
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const VfsDriveInfo* drive = vfs_get_drive(i);
        if (!drive) continue;
        shell_putchar(drive->letter);
        shell_print(": ");
        shell_print(drive->filesystem);
        shell_print("  ");
        shell_println(drive->label);
    }
}

void cmd_dir(const char* line) {
    shell_ensure_vfs_ready();
    const char* path = line + 3;
    while (*path == ' ') path++;
    char resolved[260];
    if (!shell_resolve_path(path, resolved, sizeof(resolved))) {
        shell_println("Niepoprawna sciezka.");
        return;
    }
    // debug
    for (uint32_t ci = 0; ci < usb_xhci_count(); ci++) {
        const UsbXhciControllerInfo* ctrl = usb_get_xhci(ci);
        for (uint32_t di = 0; di < ctrl->device_count; di++) {
            const UsbDeviceInfo* dev = &ctrl->devices[di];
            shell_print("dev present=");
            shell_print(dev->present ? "1" : "0");
            shell_print(" mass=");
            shell_print(dev->is_mass_storage ? "1" : "0");
            shell_print(" bulk_cfg=");
            shell_println(dev->bulk_configured ? "1" : "0");
        }
    }
    VfsDirEntry entries[64] = {};
    uint32_t count = 0;
    if (!vfs_list_dir(resolved, entries, 64, &count)) {
        shell_println("Nie mozna otworzyc katalogu.");
        return;
    }

    shell_println(resolved);
    if (count == 0) {
        shell_println("Katalog jest pusty.");
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!entries[i].present) continue;
        if (entries[i].is_dir) shell_print("<DIR> ");
        else shell_print("      ");
        shell_print(entries[i].name);
        if (!entries[i].is_dir) {
            char buf[32];
            shell_print("  ");
            itoa_dec(entries[i].size, buf);
            shell_print(buf);
            shell_print(" B");
        }
        shell_putchar('\n');
    }
}

void cmd_type(const char* line) {
    shell_ensure_vfs_ready();
    const char* path = line + 4;
    while (*path == ' ') path++;
    char resolved[260];
    if (!shell_resolve_path(path, resolved, sizeof(resolved))) {
        shell_println("Niepoprawna sciezka.");
        return;
    }

    uint8_t* buffer = (uint8_t*)malloc(65536);
    if (!buffer) {
        shell_println("Brak pamieci.");
        return;
    }

    uint32_t size = 0;
    if (!vfs_read_file(resolved, buffer, 65535, &size)) {
        free(buffer);
        shell_println("Nie mozna odczytac pliku.");
        return;
    }

    buffer[size] = 0;
    shell_print((const char*)buffer);
    if (size == 0 || buffer[size - 1] != '\n') shell_putchar('\n');
    free(buffer);
}

void cmd_cd(const char* line) {
    shell_ensure_vfs_ready();
    const char* path = line + 2;
    while (*path == ' ') path++;
    if (*path == '\0') {
        shell_println(shell_current_path);
        return;
    }

    char resolved[260];
    if (!shell_resolve_path(path, resolved, sizeof(resolved))) {
        shell_println("Niepoprawna sciezka.");
        return;
    }

    VfsPathInfo info = {};
    if (!vfs_stat(resolved, &info) || !info.exists || !info.is_dir) {
        shell_println("Katalog nie istnieje.");
        return;
    }

    str_copy(shell_current_path, resolved, sizeof(shell_current_path));
    int len = str_len(shell_current_path);
    if (len > 0 && shell_current_path[len - 1] != '\\') {
        if (len + 1 < (int)sizeof(shell_current_path)) {
            shell_current_path[len] = '\\';
            shell_current_path[len + 1] = '\0';
        }
    }
}

void cmd_usb() {
    shell_ensure_usb_ready();
    uint32_t count = usb_xhci_count();
    if (count == 0) {
        shell_println("Nie wykryto kontrolera xHCI.");
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        const UsbXhciControllerInfo* ctrl = usb_get_xhci(i);
        if (!ctrl || !ctrl->present) continue;

        char num[32];
        shell_print("xHCI @ PCI ");
        itoa_dec(ctrl->bus, num);
        shell_print(num);
        shell_print(":");
        itoa_dec(ctrl->slot, num);
        shell_print(num);
        shell_print(".");
        itoa_dec(ctrl->func, num);
        shell_print(num);
        shell_print(", ");
        shell_print(ctrl->initialized ? "ready" : "not-ready");
        shell_print(", enable-slot probe");
        shell_print(", ports ");
        itoa_dec(ctrl->max_ports, num);
        shell_print(num);
        shell_print(", slots ");
        itoa_dec(ctrl->max_slots, num);
        shell_println(num);
/*
        shell_println("  before handoff:");
        for (uint32_t p = 0; p < ctrl->max_ports && p < USB_MAX_PORTS; p++) {
            const UsbPortInfo* port = &ctrl->ports_before_handoff[p];
            if (!port->connected && !port->powered) continue;

            shell_print("  port ");
            itoa_dec((uint64_t)(p + 1), num);
            shell_print(num);
            shell_print(": ");
            shell_print(port->connected ? "device" : "empty");
            shell_print(", ");
            shell_print(port->powered ? "powered" : "no-power");
            shell_print(", ");
            shell_print(port->enabled ? "enabled" : "disabled");
            shell_print(", speed ");
            shell_println(usb_speed_name(port->speed_id));
        }

        shell_println("  after handoff/enable-slot:");
        for (uint32_t p = 0; p < ctrl->max_ports && p < USB_MAX_PORTS; p++) {
            const UsbPortInfo* port = &ctrl->ports_after_handoff[p];
            if (!port->connected && !port->powered) continue;

            shell_print("  port ");
            itoa_dec((uint64_t)(p + 1), num);
            shell_print(num);
            shell_print(": ");
            shell_print(port->connected ? "device" : "empty");
            shell_print(", ");
            shell_print(port->powered ? "powered" : "no-power");
            shell_print(", ");
            shell_print(port->enabled ? "enabled" : "disabled");
            shell_print(", ");
            shell_print(port->reset_ok ? "reset-ok" : "reset-n/a");
            shell_print(", speed ");
            shell_print(usb_speed_name(port->speed_id));
            if (port->enable_slot_ok) {
                shell_print(", slot ");
                itoa_dec(port->slot_id, num);
                shell_print(num);
            }
            shell_putchar('\n');
        }
        */
        if (ctrl->device_count > 0) {
            shell_println("  devices:");
            for (uint32_t d = 0; d < ctrl->device_count && d < USB_MAX_DEVICES; d++) {
                const UsbDeviceInfo* dev = &ctrl->devices[d];
                if (!dev->present) continue;
                shell_print("  dev port ");
                itoa_dec(dev->port_index, num);
                shell_print(num);
                shell_print(", slot ");
                itoa_dec(dev->slot_id, num);
                shell_print(num);
                shell_print(", ");
                shell_print(dev->addressed ? "addressed" : "slot-only");
             /*   shell_print(", speed ");
                shell_print(usb_speed_name(dev->speed_id));
                if (!dev->addressed && dev->address_completion_code != 0) {
                    shell_print(", address-cc ");
                    itoa_dec(dev->address_completion_code, num);
                    shell_print(num);
                }
                if (dev->descriptor_ok) {
                    shell_print(", class ");
                    itoa_dec(dev->usb_class, num);
                    shell_print(num);
                    shell_print(", subclass ");
                    itoa_dec(dev->usb_subclass, num);
                    shell_print(num);
                    shell_print(", proto ");
                    itoa_dec(dev->usb_protocol, num);
                    shell_print(num);
                    if (dev->vendor_id || dev->product_id) {
                        char hexbuf[8];
                        shell_print(", vid:pid ");
                        itoa_hex16(dev->vendor_id, hexbuf);
                        shell_print(hexbuf);
                        shell_print(":");
                        itoa_hex16(dev->product_id, hexbuf);
                        shell_print(hexbuf);
                    }
                    if (dev->interface_count > 0) {
                        shell_print(", if0 ");
                        itoa_dec(dev->interface_class, num);
                        shell_print(num);
                        shell_print("/");
                        itoa_dec(dev->interface_subclass, num);
                        shell_print(num);
                        shell_print("/");
                        itoa_dec(dev->interface_protocol, num);
                        shell_print(num);
                    }
                    if (dev->device_bytes_transferred != 0) {
                        shell_print(", dev-bytes ");
                        itoa_dec(dev->device_bytes_transferred, num);
                        shell_print(num);
                    }
                    if (dev->is_mass_storage) shell_print(", mass-storage");
                } else {
                    shell_print(", descriptors-pending");
                    if (dev->descriptor_completion_code != 0) {
                        shell_print(", desc-cc ");
                        itoa_dec(dev->descriptor_completion_code, num);
                        shell_print(num);
                    }
                }
                if (dev->config_completion_code != 0) {
                    shell_print(", cfg-cc ");
                    itoa_dec(dev->config_completion_code, num);
                    shell_print(num);
                }
                else {
                    shell_print(", cfg-cc 0");
                }
                if (dev->config_bytes_transferred != 0) {
                    shell_print(", cfg-bytes ");
                    itoa_dec(dev->config_bytes_transferred, num);
                    shell_print(num);
                }
                if (dev->config_header_len != 0 || dev->config_header_type != 0) {
                    char hexbuf[8];
                    shell_print(", cfg-hdr ");
                    itoa_dec(dev->config_header_len, num);
                    shell_print(num);
                    shell_print("/");
                    itoa_dec(dev->config_header_type, num);
                    shell_print(num);
                    shell_print(" [");
                    for (uint32_t bi = 0; bi < 8; bi++) {
                        if (bi != 0) shell_print(" ");
                        itoa_hex8(dev->config_header_bytes[bi], hexbuf);
                        shell_print(hexbuf);
                    }
                    shell_print("]");
                }
                else if (dev->descriptor_ok) {
                    shell_print(", cfg-hdr none");
                }
                if (dev->config_total_length != 0) {
                    shell_print(", cfg-len ");
                    itoa_dec(dev->config_total_length, num);
                    shell_print(num);
                }
                if (dev->config_descriptor_count != 0) {
                    shell_print(", cfg-types ");
                    uint32_t shown = dev->config_descriptor_count;
                    if (shown > 8) shown = 8;
                    for (uint32_t ti = 0; ti < shown; ti++) {
                        if (ti != 0) shell_print("/");
                        itoa_dec(dev->config_first_types[ti], num);
                        shell_print(num);
                    }
                }
                if (dev->bulk_in_endpoint || dev->bulk_out_endpoint) {
                    shell_print(", bulk-in ep");
                    itoa_dec(dev->bulk_in_endpoint, num);
                    shell_print(num);
                    shell_print(" mps");
                    itoa_dec(dev->bulk_in_max_packet, num);
                    shell_print(num);
                    shell_print(", bulk-out ep");
                    itoa_dec(dev->bulk_out_endpoint, num);
                    shell_print(num);
                    shell_print(" mps");
                    itoa_dec(dev->bulk_out_max_packet, num);
                    shell_print(num);
                }
                shell_print(dev->bulk_configured ? ", bulk-cfg-ok" : ", bulk-cfg-FAIL");
                shell_print(", bulk-cfg-cc ");
                itoa_dec(dev->bulk_cfg_completion_code, num);
                shell_print(num);
                // tymczasowo
                shell_print(", cfg-ep-out-ctx ");
                itoa_dec(dev->bulk_cfg_ep_out_ctx, num); shell_print(num);
                shell_print(", cfg-ep-in-ctx ");
                itoa_dec(dev->bulk_cfg_ep_in_ctx, num);  shell_print(num);
                shell_print(", cfg-icc1 ");
                itoa_hex16((uint16_t)(dev->bulk_cfg_icc1 >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->bulk_cfg_icc1 & 0xFFFF), num); shell_print(num);
                shell_print(", cfg-entries ");
                itoa_dec(dev->bulk_cfg_entries, num);    shell_print(num);
                shell_print(", bot-phase ");
                itoa_dec(dev->bot_cbw_phase, num); shell_print(num);
                shell_print(", csw-sig ");
                itoa_hex16((uint16_t)(dev->bot_csw_sig >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->bot_csw_sig & 0xFFFF), num); shell_print(num);
                shell_print(", csw-status ");
                itoa_dec(dev->bot_csw_status, num); shell_print(num);
                shell_print(", bulk-out-cc ");
                itoa_dec(dev->bot_bulk_out_cc, num); shell_print(num);
                shell_print(", bulk-in-cc ");
                itoa_dec(dev->bot_bulk_in_cc, num); shell_print(num);
                shell_print(", dbg-evt-type ");
                itoa_dec(dev->dbg_event_type, num); shell_print(num);
                shell_print(", dbg-evt-ptr ");
                itoa_hex16((uint16_t)(dev->dbg_event_ptr_hi >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_event_ptr_hi & 0xFFFF), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_event_ptr_lo >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_event_ptr_lo & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-tgt-ptr ");
                itoa_hex16((uint16_t)(dev->dbg_target_hi >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_target_hi & 0xFFFF), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_target_lo >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_target_lo & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-ring ");
                itoa_hex16((uint16_t)(dev->dbg_ring_hi), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_ring_lo >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_ring_lo & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-idx ");
                itoa_dec(dev->dbg_idx, num); shell_print(num);
                shell_print(", dbg-in-idx ");
                itoa_dec(dev->dbg_in_idx, num); shell_print(num);
                shell_print(", dbg-in-tgt ");
                itoa_hex16((uint16_t)(dev->dbg_in_target_lo >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_in_target_lo & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-in-evt-type ");
                itoa_dec(dev->dbg_in_event_type, num); shell_print(num);
                shell_print(", dbg-in-evt-ptr ");
                itoa_hex16((uint16_t)(dev->dbg_in_event_ptr_lo >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_in_event_ptr_lo & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-in-ring ");
                itoa_hex16((uint16_t)(dev->dbg_in_ring_lo >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_in_ring_lo & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-in-trb-ctrl ");
                itoa_hex16((uint16_t)(dev->dbg_in_trb_ctrl >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_in_trb_ctrl & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-in-trb-stat ");
                itoa_hex16((uint16_t)(dev->dbg_in_trb_stat >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_in_trb_stat & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-in-trb-p0 ");
                itoa_hex16((uint16_t)(dev->dbg_in_trb_p0 >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_in_trb_p0 & 0xFFFF), num); shell_print(num);
                shell_print(", dbg-cfg-in-ring-lo ");
                itoa_hex16((uint16_t)(dev->dbg_cfg_in_ring_lo >> 16), num); shell_print(num);
                itoa_hex16((uint16_t)(dev->dbg_cfg_in_ring_lo & 0xFFFF), num); shell_print(num); */
                if (dev->disk_block_size) {
                    shell_print(", disk-lba ");
                    itoa_dec(dev->disk_last_lba, num);
                    shell_print(num);
                    shell_print(" blksz ");
                    itoa_dec(dev->disk_block_size, num);
                    shell_print(num);
                    shell_print(dev->disk_read_ok ? ", sector0-ok [" : ", sector0-fail");
                    if (dev->disk_read_ok) {
                        shell_println(", sector0:");
                        char hx[4];
                        for (uint32_t row = 0; row < 32; row++) {
                            shell_print("    ");
                            for (uint32_t col = 0; col < 16; col++) {
                                itoa_hex8(dev->disk_sector0[row * 16 + col], hx);
                                shell_print(hx);
                                shell_print(" ");
                            }
                            shell_putchar('\n');
                        }
                    }
                }
                else {
					shell_print(", no-disk-block-size");
                }
                shell_putchar('\n');
            }
        }
    }
}
void shell_prompt() {
    shell_begin_draw();
    shell_newline_nolock();
    shell_print_nolock(shell_current_path);
    shell_print_nolock("> ");
    shell_mark_edit_origin();
    shell_end_draw();
}

void shell_execute(const char* line) {
    if (str_eq(line, "clear")) cmd_clear();
    else if (str_eq(line, "disk")) cmd_disk();
    else if (str_eq(line, "dis")) cmd_disk();
    else if (str_eq(line, "help")) cmd_help();
    else if (str_eq(line, "mem")) cmd_mem();
    else if (str_eq(line, "ticks")) cmd_ticks();
    else if (str_eq(line, "cd")) cmd_cd(line);
    else if (str_starts_with(line, "cd ")) cmd_cd(line);
    else if (str_eq(line, "vol")) cmd_vol();
    else if (str_eq(line, "dir") || str_starts_with(line, "dir ")) cmd_dir(line);
    else if (str_starts_with(line, "type ")) cmd_type(line);
    else if (str_starts_with(line, "echo")) cmd_echo(line);
    else if (str_eq(line, "notatnik")) cmd_notatnik();
    else if (str_starts_with(line, "font")) cmd_font(line); // <--- DODAJ TO
    else if (str_eq(line, "mouse")) cmd_mouse();
    else if (str_eq(line, "usb")) cmd_usb();
    else if (str_eq(line, "restart")) cmd_restart();
    else if (str_eq(line, "compile") || str_starts_with(line, "compile ")) cmd_compile(line);
    else if (str_eq(line, "compiler") || str_starts_with(line, "compiler ")) cmd_compile(line);
    else if (line[0] != '\0') { shell_print("Nieznana: "); shell_println(line); }
}
void history_push(const char* cmd) {
    if (cmd[0] == '\0') return;
    // Nie duplikuj jeśli taka sama jak ostatnia
    if (history_newest && str_eq(history_newest->text, cmd)) return;

    char* copy = (char*)malloc(str_len(cmd) + 1);
    if (!copy) return;
    for (int i = 0; cmd[i]; i++) copy[i] = cmd[i];
    copy[str_len(cmd)] = '\0';

    HistoryEntry* e = (HistoryEntry*)malloc(sizeof(HistoryEntry));
    if (!e) { free(copy); return; }
    e->text = copy;
    e->prev = nullptr;
    e->next = history_newest;
    if (history_newest) history_newest->prev = e;
    history_newest = e;
    if (!history_oldest) history_oldest = e;
}

// Wpisz tekst do aktywnej linii i przerysuj całość
void shell_set_line(const char* text) {
    shell_begin_draw();
    shell_draw_text_cursor_nolock(false);
    line_len = 0;
    line_cursor = 0;
    for (int i = 0; text[i] && line_len < 255;) {
        uint32_t cp = 0;
        int used = shell_decode_utf8_char(text + i, &cp);
        if (used <= 0) break;
        i += used;
        line_codepoints[line_len] = cp;
        line_len++;
    }
    shell_sync_line_buf_from_codepoints();
    line_cursor = line_len;
    shell_redraw_input_line_nolock();
    shell_draw_text_cursor_nolock(true);
    shell_end_draw();
}
void shell() {
    shell_ttf_enabled = is_active_font_available();
    shell_println(shell_use_ttf() ? "Schwarz OS - TTF Shell" : "Schwarz OS - Bitmap Shell");
    shell_prompt();

    uint64_t last_blink = timer_ticks;
    bool blink_state = true;
    shell_show_text_cursor();

    while (1) {
        // Miganie kursora co ~500ms (zakładam 100Hz timer = 50 ticków)
        if (timer_ticks - last_blink >= 50) {
            last_blink = timer_ticks;
            blink_state = !blink_state;
            shell_begin_draw();
            shell_draw_text_cursor_nolock(blink_state);
            shell_end_draw();
        }

        if (key_read == key_write) { asm volatile("hlt"); continue; }

        uint32_t c = key_buffer[key_read];
        key_read = (key_read + 1) & 0xFF;

        // Zatrzymaj miganie podczas obsługi klawisza
        shell_begin_draw();
        shell_draw_text_cursor_nolock(false);
        shell_end_draw();

        if (c == 27 && notatnik_mode) {
            notatnik_mode = false;
            shell_println("\n--- Wyjscie ---");
            shell_prompt();
            shell_show_text_cursor();
            continue;
        }

        if (c == '\n') {
            shell_begin_draw();
            shell_draw_text_cursor_nolock(false);
            shell_end_draw();

            shell_sync_line_buf_from_codepoints();
            shell_putchar('\n');

            if (!notatnik_mode) {
                if (!history_browsing)
                    history_push(line_buf);
                else {
                    history_push(line_buf);
                    history_current = nullptr;
                    history_browsing = false;
                }
                shell_execute(line_buf);
                shell_reset_line_edit_state();
                line_cursor = 0;
                shell_prompt();
            }
            else {
                shell_reset_line_edit_state();
                line_cursor = 0;
            }
            shell_show_text_cursor();
        }
        else if (c == '\b') {
            if (line_cursor > 0) {
                shell_begin_draw();
                shell_draw_text_cursor_nolock(false);

                for (int i = line_cursor - 1; i < (int)line_len - 1; i++)
                {
                    line_buf[i] = line_buf[i + 1];
                    line_codepoints[i] = line_codepoints[i + 1];
                }
                line_len--;
                line_cursor--;
                shell_sync_line_buf_from_codepoints();

                shell_redraw_input_line_nolock();
                shell_draw_text_cursor_nolock(true);
                shell_end_draw();
            }
        }
        else if (c == KEY_DELETE) {
            if (line_cursor < (int)line_len) {
                shell_begin_draw();
                shell_draw_text_cursor_nolock(false);

                for (int i = line_cursor; i < (int)line_len - 1; i++)
                {
                    line_buf[i] = line_buf[i + 1];
                    line_codepoints[i] = line_codepoints[i + 1];
                }
                line_len--;
                shell_sync_line_buf_from_codepoints();

                shell_redraw_input_line_nolock();
                shell_draw_text_cursor_nolock(true);
                shell_end_draw();
            }
        }
        else if (c == KEY_LEFT) {
            if (line_cursor > 0) {
                shell_begin_draw();
                shell_draw_text_cursor_nolock(false);
                line_cursor--;
                // Przesuń kursor ekranowy
                g_cursor_x = (uint32_t)line_pre_x[line_cursor];
                shell_prev_codepoint = line_prev_cp[line_cursor];
                shell_draw_text_cursor_nolock(true);
                shell_end_draw();
            }
        }
        else if (c == KEY_RIGHT) {
            if (line_cursor < (int)line_len) {
                shell_begin_draw();
                shell_draw_text_cursor_nolock(false);
                line_cursor++;
                shell_redraw_input_line_nolock();
                shell_draw_text_cursor_nolock(true);
                shell_end_draw();
            }
        }
        else if (c == KEY_HOME) {
            shell_begin_draw();
            shell_draw_text_cursor_nolock(false);
            line_cursor = 0;
            shell_redraw_input_line_nolock();
            shell_draw_text_cursor_nolock(true);
            shell_end_draw();
        }
        else if (c == KEY_END) {
            shell_begin_draw();
            shell_draw_text_cursor_nolock(false);
            line_cursor = line_len;
            shell_redraw_input_line_nolock();
            shell_draw_text_cursor_nolock(true);
            shell_end_draw();
        }
        else if (c == KEY_UP) {
            HistoryEntry* next =
                history_browsing ? (history_current ? history_current->next : nullptr)
                : history_newest;
            if (next) {
                if (!history_browsing) {
                    // Zapisz bieżącą linię
                    for (int i = 0; i <= (int)line_len; i++) line_saved[i] = line_buf[i];
                    history_browsing = true;
                }
                history_current = next;
                shell_set_line(history_current->text);
            }
        }
        else if (c == KEY_DOWN) {
            if (history_browsing) {
                if (history_current && history_current->prev) {
                    history_current = history_current->prev;
                    shell_set_line(history_current->text);
                }
                else {
                    // Wróć do zapisanej linii
                    history_browsing = false;
                    history_current = nullptr;
                    shell_set_line(line_saved);
                }
            }
        }
        else if (c < 0x110000u && c >= 32u) {
            if (line_len < 255) {
                shell_begin_draw();
                shell_draw_text_cursor_nolock(false);

                for (int i = (int)line_len; i > line_cursor; i--)
                {
                    line_codepoints[i] = line_codepoints[i - 1];
                }
                line_codepoints[line_cursor] = c;
                line_len++;
                shell_sync_line_buf_from_codepoints();
                line_cursor++;
                shell_redraw_input_line_nolock();
                shell_draw_text_cursor_nolock(true);
                shell_end_draw();
            }
        }

        blink_state = true;
        last_blink = timer_ticks;
    }
}
