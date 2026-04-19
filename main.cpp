#ifdef __INTELLISENSE__
// Oszukujemy edytor, żeby nie widział wstawek asm
#define asm(...)
#define volatile
#endif
//main.cpp
#include "ps2.h"
#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "memory.h"
#include "pit.h"
#include "task.h"
#include "shell.h"
#include "mouse.h"
#include "reboot.h"
#include "storage.h"
#include "usb.h"

extern "C" void mouse_handler();
extern "C" void timer_handler();
extern "C" void fb_acquire();
extern "C" void fb_release();
extern "C" uint64_t lapic_base = 0;
extern "C" volatile uint64_t fb_lock = 0;
extern "C" uint64_t scheduler_tick = 0;
extern "C" uint64_t isr_counter = 0;
extern "C" Task* current_task = nullptr;
extern "C" Task* task_list = nullptr;
extern "C" uint64_t next_id = 1;
extern "C" volatile uint64_t timer_ticks = 0;
BootVolumeHandoff g_boot_volume_handoff = {};
uint32_t* g_fb = nullptr;
uint32_t g_width = 0;
uint32_t g_cursor_x = 10;
uint32_t g_cursor_y = 150;
uint32_t g_max_x = 0;
uint32_t g_max_y = 0;
uint32_t g_start_y = 150;
struct BootInfo {
    uint64_t framebuffer_addr;
    uint64_t framebuffer_size;
    uint32_t horizontal_res;
    uint32_t vertical_res;
    uint64_t memory_map_addr;
    uint64_t memory_map_count;
    uint64_t rsdp_addr;  // NOWE
    BootVolumeHandoff boot_volume;
};
extern "C" void* memset(void* dst, int val, unsigned long long n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)val;
    return dst;
}

extern "C" void* memcpy(void* dst, const void* src, unsigned long long n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

extern "C" unsigned long long strlen(const char* s) {
    unsigned long long n = 0;
    while (*s++) n++;
    return n;
}
void put_pixel(uint32_t* fb, uint32_t width, uint32_t x, uint32_t y, uint32_t color) {
    fb[y * width + x] = color;
}

void clear_screen(uint32_t* fb, uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++)
            fb[y * width + x] = color;
}

void draw_rect(uint32_t* fb, uint32_t width,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h,
    uint32_t color) {
    for (uint32_t row = y; row < y + h; row++)
        for (uint32_t col = x; col < x + w; col++)
            fb[row * width + col] = color;
}
uint8_t font8x8_basic[128][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0000 (nul)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0001
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0002
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0003
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0004
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0005
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0006
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0007
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0008
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0009
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+000F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0010
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0011
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0012
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0013
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0014
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0015
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0016
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0017
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0018
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0019
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001A
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001B
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001C
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001D
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001E
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+001F
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (space)
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},   // U+0021 (!)
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0022 (")
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},   // U+0023 (#)
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},   // U+0024 ($)
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},   // U+0025 (%)
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},   // U+0026 (&)
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0027 (')
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},   // U+0028 (()
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},   // U+0029 ())
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},   // U+002A (*)
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},   // U+002B (+)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+002C (,)
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},   // U+002D (-)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+002E (.)
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},   // U+002F (/)
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+003B (;)
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},   // U+003C (<)
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},   // U+003D (=)
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},   // U+003E (>)
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},   // U+003F (?)
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},   // U+0040 (@)
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},   // U+005B ([)
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},   // U+005C (\)
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},   // U+005D (])
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},   // U+005E (^)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},   // U+005F (_)
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0060 (`)
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},   // U+0061 (a)
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},   // U+0062 (b)
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},   // U+0063 (c)
    { 0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},   // U+0064 (d)
    { 0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},   // U+0065 (e)
    { 0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},   // U+0066 (f)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0067 (g)
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},   // U+0068 (h)
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0069 (i)
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},   // U+006A (j)
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},   // U+006B (k)
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+006C (l)
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},   // U+006D (m)
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},   // U+006E (n)
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},   // U+006F (o)
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},   // U+0070 (p)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},   // U+0071 (q)
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},   // U+0072 (r)
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},   // U+0073 (s)
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},   // U+0074 (t)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},   // U+0075 (u)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0076 (v)
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},   // U+0077 (w)
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},   // U+0078 (x)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0079 (y)
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},   // U+007A (z)
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},   // U+007B ({)
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},   // U+007C (|)
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},   // U+007D (})
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+007E (~)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}    // U+007F
};
void draw_char(uint32_t* fb, uint32_t width, char c, uint32_t x, uint32_t y, uint32_t color) {
    const uint8_t* glyph = font8x8_basic[(uint8_t)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << col)) {
                fb[(y + row) * width + (x + col)] = color;
            }
        }
    }
}
void draw_string(uint32_t* fb, uint32_t width, const char* str, uint32_t x, uint32_t y, uint32_t color) {
    while (*str) {
        draw_char(fb, width, *str, x, y, color);
        x += 8;
        str++;
    }
}

extern "C" uint8_t inb(uint16_t port);

uint8_t read_scancode() {
    // bit 0 w porcie 0x64 = czy jest dane do odebrania
    while (!(inb(0x64) & 0x01));
    return inb(0x60);
}

const uint8_t scancode_map[128] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=',(uint8_t)'\b',
    (uint8_t)'\t','q','w','e','r','t','y','u','i','o','p','[',']',(uint8_t)'\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ',
    // wype�nij reszt� do 128 element�w
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0
};

char scancode_to_char(uint8_t sc) {
    if (sc & 0x80) return 0;
    if (sc >= 128) return 0;
    return (char)scancode_map[sc];
}

extern "C" void sti();

volatile uint32_t key_buffer[256];
volatile uint8_t key_write = 0;
volatile uint8_t key_read = 0;

void keyboard_flush() {
    // Opr�nij bufor kontrolera PS/2
    while (inb(0x64) & 0x01) {
        inb(0x60);  // wyrzu� bajt
    }
    keyboard_reset_state();
}
void itoa_hex(uint64_t val, char* buf) {
    const char* hex = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = hex[(val >> (60 - i * 4)) & 0xF];
    }
    buf[18] = '\0';
}
void ui_task() {
    uint64_t last_clock_ticks = ~0ULL;
    uint64_t last_blink_phase = ~0ULL;

    while (1) {
        uint64_t ticks_snapshot = timer_ticks;
        uint64_t blink_phase = ticks_snapshot / 25;
        bool needs_clock = ticks_snapshot != last_clock_ticks;
        bool needs_blink = blink_phase != last_blink_phase;
        bool needs_cursor = cursor_is_dirty();

        if (!needs_clock && !needs_blink && !needs_cursor) {
            asm volatile("hlt");
            continue;
        }

        fb_acquire();
        cursor_hide_nolock();

        if (needs_clock) {
            draw_rect(g_fb, g_width, 10, 140, 32, 10, 0x00000000);

            char buf[4];
            buf[0] = 'T';
            buf[1] = '0' + ((ticks_snapshot / 10) % 10);
            buf[2] = '0' + (ticks_snapshot % 10);
            buf[3] = 0;
            draw_string(g_fb, g_width, buf, 10, 140, 0x00FF00);
            last_clock_ticks = ticks_snapshot;
        }

        if (needs_blink) {
            uint32_t color = (blink_phase & 1) ? 0x0000FF : 0xFF0000;
            draw_rect(g_fb, g_width, 80, 0, 20, 20, color);
            last_blink_phase = blink_phase;
        }

        cursor_show_nolock();
        fb_release();
    }
}


void draw_hex(uint32_t* fb, uint32_t width, uint64_t val, uint32_t x, uint32_t y, uint32_t color) {
    char buf[17];
    buf[16] = 0;
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = val & 0xF;
        buf[i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        val >>= 4;
    }
    draw_string(fb, width, buf, x, y, color);
}
[[noreturn]] void reboot()
{
    asm volatile("cli");

    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);

    while (1)
        asm volatile("hlt");
}
extern "C" __attribute__((ms_abi)) void kernel_main(BootInfo* info) {
    uint32_t* fb = (uint32_t*)info->framebuffer_addr;
    g_fb = fb;
    g_width = info->horizontal_res;
    g_max_x = info->horizontal_res - 8;
    g_max_y = info->vertical_res - 10;

    clear_screen(fb, info->horizontal_res, info->vertical_res, 0x00000000);
    memcpy(&g_boot_volume_handoff, &info->boot_volume, sizeof(BootVolumeHandoff));
    if (info->rsdp_addr == 0) {
        draw_string(fb, g_width, "RSDP=0 FAIL", 200, 10, 0xFF0000);
    }
    else {
        draw_string(fb, g_width, "RSDP ok", 200, 10, 0x00FF00);
    }
    // 1. Podstawy procesora i przerwań
    draw_string(fb, g_width, "1 gdt", 10, 10, 0xFFFFFFFF);
    gdt_init();

    draw_string(fb, g_width, "2 idt", 10, 20, 0xFFFFFFFF);
    idt_init();

    draw_string(fb, g_width, "3 pic", 10, 30, 0xFFFFFFFF);
    pic_init();

    // 2. Pamięć
    draw_string(fb, g_width, "4 mem", 10, 40, 0xFFFFFFFF);
    MemoryRegion* regions = (MemoryRegion*)info->memory_map_addr;
    memory_init(regions, info->memory_map_count);

    // 3. System PS/2 - REMONT
    draw_string(fb, g_width, "5 ps2 init", 10, 50, 0xFFFFFFFF);
    ps2_init();      // Wspólna inicjalizacja portów 0x64/0x60

    draw_string(fb, g_width, "6 mouse init", 10, 60, 0xFFFFFFFF);
    mouse_init();    // Komendy specyficzne dla myszy (0xD4...)

    draw_string(fb, g_width, "7 keyboard flush", 10, 70, 0xFFFFFFFF);
    keyboard_flush();

    // 4. Konfiguracja IDT i IRQ
    draw_string(fb, g_width, "8 idt entries", 10, 80, 0xFFFFFFFF);
    idt_set_entry(0x20, (uint64_t)timer_handler);    // PIT
    idt_set_entry(0x21, (uint64_t)keyboard_handler); // Klawiatura (IRQ1)
    idt_set_entry(0x2C, (uint64_t)mouse_handler);    // Mysz (IRQ12)
    // z tym PITem jest taki problem że na QEMU działa, a na prawdziwym sprzęcie nie działa, ale jak się go utnie to się zaczyna problem z innymi sprzętami, także wmieszaliśmy się w olbrzymi problem z PIT i ACPI, i ucięcie jednego ucina większość funkcjonalności i niezbyt to już ogarniam
    draw_string(fb, g_width, "9 pit 100hz", 10, 90, 0xFFFFFFFF);

    struct RSDP {
        char signature[8];
        uint8_t checksum;
        char oem[6];
        uint8_t revision;
        uint32_t rsdt_addr;
        uint32_t length;
        uint64_t xsdt_addr;
    } __attribute__((packed));

    struct ACPIHeader {
        char signature[4];
        uint32_t length;
        uint8_t revision;
        uint8_t checksum;
        char oem[6];
        char oem_table[8];
        uint32_t oem_revision;
        uint32_t creator_id;
        uint32_t creator_revision;
    } __attribute__((packed));

    uint64_t ioapic_addr = 0;
    uint64_t lapic_addr = 0xFEE00000;
    ACPIHeader* madt = nullptr;

    RSDP* rsdp = (RSDP*)info->rsdp_addr;

    if (rsdp->revision >= 2 && rsdp->xsdt_addr != 0) {
        ACPIHeader* xsdt = (ACPIHeader*)rsdp->xsdt_addr;
        uint64_t entries = (xsdt->length - sizeof(ACPIHeader)) / 8;
        uint64_t* ptrs = (uint64_t*)((uint64_t)xsdt + sizeof(ACPIHeader));
        for (uint64_t i = 0; i < entries; i++) {
            ACPIHeader* h = (ACPIHeader*)ptrs[i];
            if (h->signature[0] == 'A' && h->signature[1] == 'P' &&
                h->signature[2] == 'I' && h->signature[3] == 'C') {
                madt = h; break;
            }
        }
    }
    else {
        ACPIHeader* rsdt = (ACPIHeader*)(uint64_t)rsdp->rsdt_addr;
        uint64_t entries = (rsdt->length - sizeof(ACPIHeader)) / 4;
        uint32_t* ptrs = (uint32_t*)((uint64_t)rsdt + sizeof(ACPIHeader));
        for (uint64_t i = 0; i < entries; i++) {
            ACPIHeader* h = (ACPIHeader*)(uint64_t)ptrs[i];
            if (h->signature[0] == 'A' && h->signature[1] == 'P' &&
                h->signature[2] == 'I' && h->signature[3] == 'C') {
                madt = h; break;
            }
        }
    }

    if (madt) {
        draw_string(fb, g_width, "MADT ok", 200, 40, 0x00FF00);
    }
    else {
        draw_string(fb, g_width, "MADT FAIL", 200, 40, 0xFF0000);
    }
    // Parsuj MADT - znajdź Local APIC i I/O APIC
    struct MADTHeader {
        ACPIHeader header;
        uint32_t lapic_addr;
        uint32_t flags;
    } __attribute__((packed));

    struct MADTEntry {
        uint8_t type;
        uint8_t length;
    } __attribute__((packed));

    MADTHeader* madt_hdr = (MADTHeader*)madt;
    lapic_addr = madt_hdr->lapic_addr;

    uint32_t irq_to_gsi[16];
    for (int i = 0; i < 16; i++) {
        irq_to_gsi[i] = i; // Default 1:1 mapping
    }

    uint8_t* entry = (uint8_t*)madt + sizeof(MADTHeader);
    uint8_t* end = (uint8_t*)madt + madt->length;

    while (entry < end) {
        MADTEntry* e = (MADTEntry*)entry;
        if (e->length == 0) break; // Zabezpieczenie przed pętlą nieskończoną
        if (e->type == 1) {  // I/O APIC
            if (ioapic_addr == 0) { // Bierzemy pierwszy I/O APIC
                ioapic_addr = *(uint32_t*)(entry + 4);
            }
        }
        else if (e->type == 2) { // Interrupt Source Override
            uint8_t bus = *(uint8_t*)(entry + 2);
            uint8_t source = *(uint8_t*)(entry + 3);
            uint32_t gsi = *(uint32_t*)(entry + 4);
            if (bus == 0 && source < 16) {
                irq_to_gsi[source] = gsi;
            }
        }
        entry += e->length;
    }

    if (ioapic_addr) {
        draw_string(fb, g_width, "IOAPIC ok", 200, 50, 0x00FF00);
        lapic_base = lapic_addr;
    }
    else {
        draw_string(fb, g_width, "IOAPIC FAIL", 200, 50, 0xFF0000);
    }
    // Nie wyłączamy całkowicie PIC, bo KBD i Mysz będą z niego korzystać.

    // Pomocnicze funkcje do I/O APIC
    auto ioapic_read = [&](uint8_t reg) -> uint32_t {
        *(volatile uint32_t*)(ioapic_addr) = reg;
        return *(volatile uint32_t*)(ioapic_addr + 0x10);
    };
    auto ioapic_write = [&](uint8_t reg, uint32_t val) {
        *(volatile uint32_t*)(ioapic_addr) = reg;
        *(volatile uint32_t*)(ioapic_addr + 0x10) = val;
    };
    auto ioapic_set_gsi = [&](uint32_t gsi, uint8_t vector) {
        if (!ioapic_addr) return;
        uint32_t reg = 0x10 + (gsi * 2);
        // vector, fixed delivery, active high, edge triggered, unmasked
        ioapic_write(reg, vector);
        
        // Pobierz lokalne APIC ID (bity 24-31 rejestru 0x20)
        uint32_t my_apic_id = *(volatile uint32_t*)(lapic_addr + 0x20) >> 24;
        
        // destination APIC ID
        ioapic_write(reg + 1, my_apic_id << 24);
    };

    // Włącz Local APIC (ustaw bit 8 w Spurious Interrupt Vector Register)
    *(volatile uint32_t*)(lapic_addr + 0xF0) |= (1 << 8);
    // Ustaw spurious vector na 0xFF
    *(volatile uint32_t*)(lapic_addr + 0xF0) = 0x1FF;
    // Wyzeruj TPR (Task Priority Register), aby akceptować wszystkie przerwania
    *(volatile uint32_t*)(lapic_addr + 0x80) = 0;

    // KONFIGURACJA LAPIC TIMER (Niezawodny heartbeat wbudowany w CPU)
    *(volatile uint32_t*)(lapic_addr + 0x3E0) = 0x03; // Divide by 16
    *(volatile uint32_t*)(lapic_addr + 0x320) = 0x20 | 0x20000; // Wektor 0x20, tryb Periodic
    *(volatile uint32_t*)(lapic_addr + 0x380) = 25000; // Znacznie szybszy tick-rate (ok. 60-100Hz)

    draw_string(fb, g_width, "APIC cfg ok", 200, 60, 0x00FF00);
    // Zastąpiliśmy PIT przez LAPIC Timer, więc go nie inicjalizujemy.
    // pit_init(100);

    draw_string(fb, g_width, "10 pic unmask", 10, 100, 0xFFFFFFFF);
    // Przywracamy Legacy PIC wyłącznie dla Klawiatury (IRQ1) i Myszki (IRQ12).
    // Maskujemy PIT (IRQ0), ponieważ używamy LAPIC Timera.
    // 0xF9 = 1111 1001 (Odmaskowane bity 1 i 2)
    outb(0x21, 0xF9); 
    // 0xEF = 1110 1111 (Odmaskowany bit 4, czyli IRQ12 na slave)
    outb(0xA1, 0xEF);

    // 5. Ustawienia powłoki na bazie metryk TTF
    constexpr uint32_t kBootLogLastY = 130;
    constexpr uint32_t kBootLogFontH = 8;
    constexpr uint32_t kShellTopGap = 12;
    uint32_t shell_top = kBootLogLastY + kBootLogFontH + kShellTopGap;
    uint32_t shell_baseline = shell_top + (uint32_t)shell_font_baseline();

    if (shell_baseline > g_max_y - 30) shell_baseline = 10 + (uint32_t)shell_font_baseline();
    g_start_y = shell_baseline;
    g_cursor_x = 10;
    g_cursor_y = shell_baseline;
    // 6. Wielozadaniowość i start
    draw_string(fb, g_width, "11 task", 10, 110, 0xFFFFFFFF);
    task_create(shell);
    task_create(ui_task);

    
    // Sprawdź czy PIT w ogóle liczy
    outb(0x43, 0x00);  // latch channel 0
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    uint16_t count = (hi << 8) | lo;
    if (count == 0) {
        draw_string(fb, g_width, "PIT=0", 400, 160, 0xFF0000);
    }
    else {
        draw_string(fb, g_width, "PIT ok", 400, 150, 0x00FF00);
    }
    draw_string(fb, g_width, "12 sti", 10, 120, 0xFFFFFFFF);
    sti();
	// UWAGA: CAŁY PONIŻSZY KOD NIE WYKONA SIĘ NIGDY, CHYBA ŻE SCHEDULER NIE DZIAŁA, MULTITASKING NIE DZIAŁA, LUB NIE MA PRZERWAŃ. W NORMALNYCH OKOLICZNOŚCIACH NIE POWINIEN SIĘ W OGÓLE WYŚWIETLIĆ.
    // Ewentualnie, mogłoby sie wyświetlić, gdyby wszystkie taski zabić. Ale nawet nie mam takiej funkcji (na dzień pisania tego komentarza) żeby zabijać taski, a gdyby po prostu je urwać (kończąc np. nieskończoną pętle while) to one sie zamrożą, ale chyba tu nie wrócą. Nie wiem, nie testowałem. 
    draw_string(fb, g_width, "13 idle", 10, 130, 0xFFFFFFFF);
    

    bool last_isr_state = false;
    while (1) {
        bool isr_active = isr_counter > 0;
        if (isr_active != last_isr_state) {
            fb_acquire();
            cursor_hide_nolock();
            if (isr_active) {
                draw_rect(fb, g_width, 400, 170, 64, 10, 0x00000000);
                draw_string(fb, g_width, "ISR hit!", 400, 170, 0x00FF00);
            }
            else {
                draw_rect(fb, g_width, 400, 180, 64, 10, 0x00000000);
                draw_string(fb, g_width, "no ISR", 400, 180, 0xFF0000);
            }
            cursor_show_nolock();
            fb_release();
            last_isr_state = isr_active;
        }
        asm volatile("hlt");
    }
}
