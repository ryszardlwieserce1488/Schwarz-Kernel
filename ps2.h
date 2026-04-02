#pragma once
#include "types.h"

extern "C" uint8_t inb(uint16_t port);
extern "C" void outb(uint16_t port, uint8_t value);

// Czeka, a¿ kontroler bêdzie gotowy na przyjêcie komendy
void ps2_wait_write() {
    uint32_t timeout = 100000;
    while (timeout-- && (inb(0x64) & 0x02));
}

// Czeka, a¿ kontroler wystawi dane do odczytu
void ps2_wait_read() {
    uint32_t timeout = 100000;
    while (timeout-- && !(inb(0x64) & 0x01));
}

void ps2_init() {
    // 1. Wy³¹cz oba porty na czas konfiguracji
    ps2_wait_write();
    outb(0x64, 0xAD); // Disable Keyboard
    ps2_wait_write();
    outb(0x64, 0xA7); // Disable Mouse

    // 2. Wyp³ucz stare dane z bufora
    while (inb(0x64) & 0x01) inb(0x60);

    // 3. Odczytaj Controller Configuration Byte (CCB)
    ps2_wait_write();
    outb(0x64, 0x20);
    ps2_wait_read();
    uint8_t ccb = inb(0x60);

    // 4. Ustaw flagi: IRQ klawiatury (bit 0), IRQ myszy (bit 1), translacja (bit 6)
    ccb |= (1 << 0) | (1 << 1) | (1 << 6);
    ccb &= ~((1 << 4) | (1 << 5)); // W³¹cz zegary (bity 4 i 5 off)

    // 5. Zapisz CCB z powrotem
    ps2_wait_write();
    outb(0x64, 0x60);
    ps2_wait_write();
    outb(0x60, ccb);

    // 6. W³¹cz porty
    ps2_wait_write();
    outb(0x64, 0xAE);
    ps2_wait_write();
    outb(0x64, 0xA8);
}