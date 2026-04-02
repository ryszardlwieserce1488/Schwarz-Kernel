//idt.h
#pragma once
#include "types.h"

extern "C" void keyboard_handler();
extern "C" void isr_default();
extern "C" void isr_default_err();   // <- NOWE

#pragma pack(push, 1)
struct IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};
struct IDTDescriptor {
    uint16_t size;
    uint64_t addr;
};
#pragma pack(pop)

extern "C" void idt_load(IDTDescriptor* desc);

IDTEntry idt[256];
IDTDescriptor idt_desc;

void idt_set_entry(uint8_t num, uint64_t handler) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = 0x08;
    idt[num].ist = 0;
    idt[num].type_attr = 0x8E;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].zero = 0;
}

void idt_init() {
    // Domyœlnie: bez error code
    for (int i = 0; i < 256; i++) {
        idt_set_entry(i, (uint64_t)isr_default);
    }

    // Wyj¹tki które pushuj¹ error code na stos:
    idt_set_entry(8, (uint64_t)isr_default_err);  // #DF Double Fault
    idt_set_entry(10, (uint64_t)isr_default_err);  // #TS Invalid TSS
    idt_set_entry(11, (uint64_t)isr_default_err);  // #NP Segment Not Present
    idt_set_entry(12, (uint64_t)isr_default_err);  // #SS Stack Fault
    idt_set_entry(13, (uint64_t)isr_default_err);  // #GP General Protection
    idt_set_entry(14, (uint64_t)isr_default_err);  // #PF Page Fault
    idt_set_entry(17, (uint64_t)isr_default_err);  // #AC Alignment Check
    idt_set_entry(21, (uint64_t)isr_default_err);  // #CP Control Protection

    idt_desc.size = sizeof(idt) - 1;
    idt_desc.addr = (uint64_t)&idt;
    idt_load(&idt_desc);
}