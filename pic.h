//pic.h
#pragma once
#include "types.h"

extern "C" void outb(uint16_t port, uint8_t value);
extern "C" uint8_t inb(uint16_t port);
extern "C" void io_wait();

// porty PIC
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

// komendy
#define PIC_EOI   0x20   // End Of Interrupt
#define ICW1_INIT 0x11   // inicjalizacja + ICW4 required
#define ICW4_8086 0x01   // tryb 8086

void pic_init() {
    outb(0x22, 0x70);
    io_wait();
    outb(0x23, 0x01);
    io_wait();
    // ICW1 - rozpocznij inicjalizacjê obu PIC
    outb(PIC1_CMD, ICW1_INIT);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT);
    io_wait();

    // ICW2 - remapuj wektory
    outb(PIC1_DATA, 0x20);   // Master: IRQ0-7 -> 0x20-0x27
    io_wait();
    outb(PIC2_DATA, 0x28);   // Slave:  IRQ8-15 -> 0x28-0x2F
    io_wait();

    // ICW3 - po³¹czenie Master/Slave
    outb(PIC1_DATA, 0x04);   // Master: Slave pod³¹czony do IRQ2
    io_wait();
    outb(PIC2_DATA, 0x02);   // Slave: jego numer kaskady to 2
    io_wait();

    // ICW4 - tryb 8086
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // wy³¹cz wszystkie IRQ na razie (maska = 0xFF)
    outb(PIC1_DATA, 0xFF);
    io_wait();
    outb(PIC2_DATA, 0xFF);
    io_wait();
}