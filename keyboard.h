#pragma once
#include "types.h"
#include "reboot.h"
extern "C" uint8_t inb(uint16_t port);
extern "C" void outb(uint16_t port, uint8_t value);
extern "C" void keyboard_handler();

// Bufor klawiatury — uint32_t, jeden slot = jeden Unicode codepoint
extern volatile uint32_t key_buffer[256];
extern volatile uint8_t key_write;
extern volatile uint8_t key_read;

const uint8_t kb_scancode_map[128] = {
    0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', 8,
    9, 'q','w','e','r','t','y','u','i','o','p','[',']', 10,
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0
};

const uint8_t kb_scancode_map_shift[128] = {
    0, 0, '!','@','#','$','%','^','&','*','(',')','_','+', 8,
    9, 'Q','W','E','R','T','Y','U','I','O','P','{','}', 10,
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0
};

struct AltPolishEntry { uint8_t code; uint32_t cp_lower; uint32_t cp_upper; };

static const AltPolishEntry kb_alt_polish[] = {
    { 0x1E, 0x0105, 0x0104 }, // a/A → ą/Ą
    { 0x2E, 0x0107, 0x0106 }, // c/C → ć/Ć
    { 0x12, 0x0119, 0x0118 }, // e/E → ę/Ę
    { 0x26, 0x0142, 0x0141 }, // l/L → ł/Ł
    { 0x31, 0x0144, 0x0143 }, // n/N → ń/Ń
    { 0x18, 0x00F3, 0x00D3 }, // o/O → ó/Ó
    { 0x1F, 0x015B, 0x015A }, // s/S → ś/Ś
    { 0x2D, 0x017A, 0x0179 }, // x/X → ź/Ź
    { 0x2C, 0x017C, 0x017B }, // z/Z → ż/Ż
    { 0,    0,      0      }  // sentinel
};

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool kb_extended_next = false;
static uint8_t kb_pause_ignore = 0;
static bool key_state[256] = { false }; // Tablica stanów klawiszy (scancody 0..127)
static bool key_state_ext[256] = { false }; // Tablica stanów klawiszy rozszerzonych

static inline void keyboard_reset_state() {
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    kb_extended_next = false;
    kb_pause_ignore = 0;
    for (int i = 0; i < 256; i++) {
        key_state[i] = false;
        key_state_ext[i] = false;
    }
}

#define SC_BACKSPACE  0x0E
#define SC_ENTER      0x1C
#define SC_LSHIFT     0x2A
#define SC_RSHIFT     0x36
#define SC_LCTRL      0x1D
#define SC_LALT       0x38
#define SC_CAPSLOCK   0x3A
#define SC_ESC        0x01
#define KEY_UP     0x10001u
#define KEY_DOWN   0x10002u
#define KEY_LEFT   0x10003u
#define KEY_RIGHT  0x10004u
#define KEY_DELETE 0x10005u
#define KEY_HOME   0x10006u
#define KEY_END    0x10007u

static inline void kb_push(uint32_t codepoint) {
    uint8_t next = (key_write + 1) & 0xFF;
    if (next != key_read) {
        key_buffer[key_write] = codepoint;
        key_write = next;
    }
}

static inline void keyboard_handle_byte(uint8_t sc) {
    if (kb_pause_ignore) { kb_pause_ignore--; return; }
    if (sc == 0xE1) { kb_pause_ignore = 5; kb_extended_next = false; return; }
    if (sc == 0xE0) { kb_extended_next = true; return; }

    const bool extended = kb_extended_next;
    kb_extended_next = false;
    const bool released = sc & 0x80;
    const uint8_t code = sc & 0x7F;

    if (code == SC_LSHIFT || code == SC_RSHIFT) { shift_pressed = !released; }
    if (code == SC_LCTRL) { ctrl_pressed = !released; }
    if (code == SC_LALT) { alt_pressed = !released; }

    if (extended) key_state_ext[code] = !released;
    else key_state[code] = !released;

    if (released) return;

    if (ctrl_pressed && alt_pressed && code == 0x53) { reboot(); }
    if (extended) {
        if (!released) {
            switch (code) {
            case 0x48: kb_push(0x10001); break; // strzałka góra
            case 0x50: kb_push(0x10002); break; // strzałka dół
            case 0x4B: kb_push(0x10003); break; // strzałka lewo
            case 0x4D: kb_push(0x10004); break; // strzałka prawo
            case 0x53: kb_push(0x10005); break; // Delete
            case 0x47: kb_push(0x10006); break; // Home
            case 0x4F: kb_push(0x10007); break; // End
            }
        }
        return;
    }

    if (alt_pressed) {
        for (int i = 0; kb_alt_polish[i].code != 0; i++) {
            if (kb_alt_polish[i].code == code) {
                kb_push(shift_pressed ? kb_alt_polish[i].cp_upper
                    : kb_alt_polish[i].cp_lower);
                return;
            }
        }
      
        return;
    }

    uint32_t c = 0;
    if (code == SC_ESC)       c = 27;
    else if (code == SC_BACKSPACE) c = '\b';
    else if (code == SC_ENTER)     c = '\n';
    else c = shift_pressed
        ? (uint32_t)kb_scancode_map_shift[code]
        : (uint32_t)kb_scancode_map[code];

    if (c) kb_push(c);
}

extern "C" uint64_t lapic_base;
extern "C" void keyboard_handler_c() {
    uint8_t status = inb(0x64);
    if ((status & 0x01) && !(status & 0x20)) {
        uint8_t sc = inb(0x60);
        keyboard_handle_byte(sc);
    }
    outb(0x20, 0x20); // EOI Master
}