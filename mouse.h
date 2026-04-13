#pragma once
#include "types.h"
extern "C" void fb_acquire();
extern "C" void fb_release();
extern "C" void outb(uint16_t port, uint8_t value);
extern "C" uint8_t inb(uint16_t port);

// Stan myszy
struct MouseState {
    int32_t x;
    int32_t y;
    bool left;
    bool right;
    int8_t scroll;
};

static MouseState mouse = { 640, 400, false, false, 0 };
static uint8_t mouse_packet[4];
static uint8_t mouse_packet_idx = 0;
static bool mouse_has_scroll = false;
static uint8_t mouse_packet_size = 3;
static const int CURSOR_HEIGHT = 17;
static const int CURSOR_WIDTH = 12;
static uint32_t cursor_saved[CURSOR_HEIGHT][CURSOR_WIDTH];
static bool cursor_saved_valid = false;
static volatile bool cursor_dirty = true;

// Granice kursora
extern uint32_t g_width;
extern uint32_t g_max_y;
extern uint32_t g_start_y;

// ===== Komunikacja z kontrolerem PS/2 =====

void mouse_wait_write() {
    // Czekaj a� bufor wej�ciowy kontrolera jest pusty
    uint32_t timeout = 100000;
    while (timeout--) {
        if (!(inb(0x64) & 0x02)) return;
    }
}

void mouse_wait_read() {
    // Czekaj a� s� dane do odczytu
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 0x01) return;
    }
}

void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(0x64, 0xD4);   // powiedz kontrolerowi: dane id� do myszy
    mouse_wait_write();
    outb(0x60, data);
}

uint8_t mouse_read() {
    mouse_wait_read();
    return inb(0x60);
}

uint8_t mouse_command(uint8_t cmd) {
    mouse_write(cmd);
    return mouse_read();  // mysz odsy�a ACK (0xFA)
}

// ===== Inicjalizacja =====

void mouse_init() {
    // 1. W��cz port myszy w kontrolerze PS/2
    mouse_wait_write();
    outb(0x64, 0xA8);   // enable auxiliary device

    // 2. W��cz IRQ12 w kontrolerze
    mouse_wait_write();
    outb(0x64, 0x20);   // odczytaj bajt konfiguracyjny
    mouse_wait_read();
    uint8_t config = inb(0x60);
    config |= 0x02;     // w��cz IRQ12 (bit 1)
    config &= ~0x20;    // w��cz zegar myszy (bit 5 = 0)
    mouse_wait_write();
    outb(0x64, 0x60);   // zapisz bajt konfiguracyjny
    mouse_wait_write();
    outb(0x60, config);

    // 3. Zresetuj mysz
    mouse_command(0xFF);
    mouse_read();   // 0xAA (self-test passed)
    mouse_read();   // 0x00 (mouse ID)

    // 4. Spr�buj w��czy� tryb scroll (IntelliMouse)
    // Sekwencja: ustaw sample rate 200, 100, 80 � potem zapytaj o ID
    mouse_command(0xF3); mouse_command(200);
    mouse_command(0xF3); mouse_command(100);
    mouse_command(0xF3); mouse_command(80);
    mouse_command(0xF2);  // zapytaj o ID
    uint8_t mouse_id = mouse_read();
    if (mouse_id == 3) {
        mouse_has_scroll = true;
        mouse_packet_size = 4;
    }

    // 5. Ustaw rozdzielczo�� i sample rate
    mouse_command(0xE8); mouse_command(0x03);  // rozdzielczo�� 8 count/mm
    mouse_command(0xF3); mouse_command(100);   // 100 samples/sec

    // 6. W��cz raportowanie danych
    mouse_command(0xF4);
}

// ===== Rysowanie kursora =====

// Poprzednia pozycja kursora (�eby go wymaza�)
static int32_t cursor_prev_x = -1;
static int32_t cursor_prev_y = -1;

extern uint32_t* g_fb;

void draw_cursor(int32_t x, int32_t y, uint32_t color) {
    // Prosty krzy�yk 5x5
    for (int i = -2; i <= 2; i++) {
        int32_t px = x + i;
        int32_t py = y;
        if (px >= 0 && px < (int32_t)g_width && py >= 0 && py < (int32_t)(g_max_y + 10))
            g_fb[py * g_width + px] = color;
    }
    for (int i = -2; i <= 2; i++) {
        int32_t px = x;
        int32_t py = y + i;
        if (px >= 0 && px < (int32_t)g_width && py >= 0 && py < (int32_t)(g_max_y + 10))
            g_fb[py * g_width + px] = color;
    }
}

const char windows_cursor[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,1,1,1,1,1,1},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0}
};
void cursor_draw() {
    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            int32_t px = mouse.x + dx;
            int32_t py = mouse.y + dy;

            // Używamy Twojego sprawdzenia krawędzi:
            if (px < 0 || px >= (int32_t)g_width || py < 0 || py >= (int32_t)(g_max_y + 10)) {
                continue;
            }

            // 1. Pobieramy typ piksela z tablicy windows_cursor (którą wrzuciłem wcześniej)
            char pixel_type = windows_cursor[dy][dx];

            if (pixel_type == 0) continue; // Przezroczyste - nie dotykamy FB ani nie zapisujemy tła

            // 2. Zapisujemy tło tylko dla nieprzezroczystych pikseli kursora
            cursor_saved[dy][dx] = g_fb[py * g_width + px];

            // 3. Rysujemy na ekranie
            if (pixel_type == 1) {
                g_fb[py * g_width + px] = 0x000000; // Czarna obwódka
            }
            else if (pixel_type == 2) {
                g_fb[py * g_width + px] = 0xFFFFFF; // Białe wnętrze
            }
        }
    }
    cursor_saved_valid = true;
    cursor_prev_x = mouse.x;
    cursor_prev_y = mouse.y;
}
void cursor_erase() {
    if (!cursor_saved_valid) return;

    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            // Używamy starych współrzędnych (prev_x/y)
            int32_t px = cursor_prev_x + dx;
            int32_t py = cursor_prev_y + dy;

            if (px < 0 || px >= (int32_t)g_width || py < 0 || py >= (int32_t)(g_max_y + 10)) {
                continue;
            }

            // Sprawdzamy, czy w tym miejscu kursor w ogóle coś namalował (pixel_type != 0)
            if (windows_cursor[dy][dx] != 0) {
                // PRZYWRACAMY stare piksele z tablicy saved
                g_fb[py * g_width + px] = cursor_saved[dy][dx];
            }
        }
    }
    cursor_saved_valid = false;
}
void cursor_hide_nolock() {
    cursor_erase();
}

void cursor_show_nolock() {
    cursor_draw();
    cursor_dirty = false;
}

void cursor_refresh() {
    fb_acquire();
    cursor_hide_nolock();
    cursor_show_nolock();
    fb_release();
}

bool cursor_is_dirty() {
    return cursor_dirty;
}

// ===== Obs�uga pakietu =====

extern "C" void mouse_handler_c() {
    uint8_t status = inb(0x64);

    // 1. Sprawd� czy s� dane (bit 0) i czy nale�� do myszy (bit 5)
    if ((status & 0x01) && (status & 0x20)) {
        uint8_t data = inb(0x60);

        // 2. Synchronizacja: pierwszy bajt pakietu MUSI mie� bit 3 zapalony
        if (mouse_packet_idx == 0 && !(data & 0x08)) {
            // Je�li bit 3 jest 0 w pierwszym bajcie, ignorujemy go, by odzyska� synchronizacj�
            goto done;
        }

        mouse_packet[mouse_packet_idx++] = data;

        // 3. Czy mamy pe�ny pakiet? (3 bajty standard, 4 bajty je�li scroll)
        if (mouse_packet_idx >= mouse_packet_size) {
            mouse_packet_idx = 0;

            uint8_t flags = mouse_packet[0];

            // 4. Sprawdzenie overflow (bity 6 i 7) - je�li s�, pakiet jest niewiarygodny
            if (flags & 0x40 || flags & 0x80) {
                goto done;
            }

            // 5. Przyciski
            mouse.left = flags & 0x01;
            mouse.right = flags & 0x02;

            // 6. Przeliczanie ruchu (uwzgl�dnienie znak�w 0x10 i 0x20)
            int32_t dx = (int32_t)mouse_packet[1];
            int32_t dy = (int32_t)mouse_packet[2];

            if (flags & 0x10) dx -= 256;
            if (flags & 0x20) dy -= 256;

            // 7. Obs�uga Scrolla (opcjonalnie)
            if (mouse_has_scroll) {
                mouse.scroll = (int8_t)(mouse_packet[3] & 0x0F);
                if (mouse.scroll > 7) mouse.scroll -= 16;
            }
            mouse.x += dx;
            mouse.y -= dy; // Mysz ma Y w g�r� dodatnie, ekran w d� dodatnie

            // Granice ekranu
            if (mouse.x < 0) mouse.x = 0;
            if (mouse.y < 0) mouse.y = 0;
            if (mouse.x >= (int32_t)g_width) mouse.x = g_width - 1;
            if (mouse.y >= (int32_t)(g_max_y + 10)) mouse.y = g_max_y + 10 - 1;

            cursor_dirty = true;
        }
    }

done:
    outb(0xA0, 0x20); // EOI Slave
    outb(0x20, 0x20); // EOI Master
}
