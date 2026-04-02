//gdt.h
#pragma once
#include "types.h"

#pragma pack(push, 1)
struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
};

struct GDTDescriptor {
    uint16_t size;
    uint64_t addr;
};
#pragma pack(pop)

extern "C" void gdt_load(GDTDescriptor* desc);

GDTEntry gdt[3];
GDTDescriptor gdt_desc;

void gdt_init() {
    gdt[0] = { 0, 0, 0, 0, 0, 0 };

    gdt[1] = { 0xFFFF, 0x0000, 0x00, 0x9A, 0xAF, 0x00 };
    gdt[2] = { 0xFFFF, 0x0000, 0x00, 0x92, 0xAF, 0x00 };

    gdt_desc.size = sizeof(gdt) - 1;
    gdt_desc.addr = (uint64_t)&gdt;

    gdt_load(&gdt_desc);
}