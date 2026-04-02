//pit.h
#pragma once
#include "types.h"

extern "C" void outb(uint16_t port, uint8_t value);
extern "C" void io_wait();

#define PIT_CHANNEL0  0x40
#define PIT_CMD       0x43
#define PIT_FREQ      1193182  // Hz — bazowa czêstotliwoœæ PIT

void pit_init(uint32_t hz) {
    uint32_t divisor = PIT_FREQ / hz;
    outb(PIT_CMD, 0x36);                        // kana³ 0, tryb 3, binarny
    io_wait();
    outb(PIT_CHANNEL0, divisor & 0xFF);         // m³odszy bajt
    io_wait();
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);  // starszy bajt
    io_wait();
}