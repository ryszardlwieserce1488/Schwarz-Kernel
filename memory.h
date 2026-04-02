#pragma once
#include "types.h"

// Flagi dla czytelnoœci (zak³adam z Twojego kodu)
constexpr uint32_t MEM_REGION_USABLE = 1;

struct MemoryRegion {
    uint64_t base;
    uint64_t length;
    uint32_t kind;
    uint32_t pad;
};

// Deklaracje funkcji publicznych
void memory_init(MemoryRegion* regions, uint64_t count);
extern "C" void* malloc(uint64_t size);
extern "C" void free(void* ptr);

uint64_t memory_free_bytes();
uint64_t memory_used_bytes();