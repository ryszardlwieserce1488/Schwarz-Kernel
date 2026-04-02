#include "memory.h"

// Magiczny numer u³atwiaj¹cy wykrycie nadpisania sterty
constexpr uint64_t HEAP_MAGIC = 0xC001C0DECAFE8888;

struct BlockHeader {
    uint64_t magic;       // Zabezpieczenie (canary)
    uint64_t size;        // Rozmiar danych BEZ nag³ówka
    uint64_t used;        // 1 = zajêty, 0 = wolny
    BlockHeader* next;
    BlockHeader* prev;
};

// Zmienne statyczne widoczne tylko w tym pliku
static BlockHeader* heap_head = nullptr;
static uint64_t heap_start_addr = 0;
static uint64_t heap_end_addr = 0;

void memory_init(MemoryRegion* regions, uint64_t count) {
    BlockHeader* previous_block = nullptr;
    heap_head = nullptr;

    // Ustawiamy skrajne wartoœci dla œledzenia granic sterty
    heap_start_addr = 0xFFFFFFFFFFFFFFFF;
    heap_end_addr = 0;

    for (uint64_t i = 0; i < count; i++) {
        if (regions[i].kind != MEM_REGION_USABLE) continue;

        uint64_t base = regions[i].base;
        uint64_t size = regions[i].length;

        // Omiñ pierwsze 2MB (BIOS, kernel, struktury bootloadera)
        if (base < 0x200000) {
            uint64_t skip = 0x200000 - base;
            if (skip >= size) continue;
            base += skip;
            size -= skip;
        }

        // Ignoruj skrawki pamiêci, w których nie zmieœci siê nawet nag³ówek i minimum danych
        if (size < sizeof(BlockHeader) + 16) continue;

        // Aktualizuj globalne granice sterty (opcjonalne, ale przydatne do statystyk)
        if (base < heap_start_addr) heap_start_addr = base;
        if (base + size > heap_end_addr) heap_end_addr = base + size;

        // Utwórz wolny blok na pocz¹tku tego regionu pamiêci
        BlockHeader* current_block = reinterpret_cast<BlockHeader*>(base);
        current_block->magic = HEAP_MAGIC;
        current_block->size = size - sizeof(BlockHeader);
        current_block->used = 0;
        current_block->next = nullptr;
        current_block->prev = previous_block;

        // £¹czymy regiony w jedn¹ spójn¹ listê!
        if (previous_block != nullptr) {
            previous_block->next = current_block;
        }
        else {
            // To jest pierwszy poprawny region, który znaleŸliœmy
            heap_head = current_block;
        }

        previous_block = current_block;
    }

    // Zabezpieczenie: totalny brak wolnej pamiêci
    if (heap_head == nullptr) {
        while (1) asm volatile("cli; hlt"); // Kernel Panic
    }
}

extern "C" void* malloc(uint64_t size) {
    if (size == 0) return nullptr;

    // Wyrównaj do 8 bajtów (sztywna matematyka bitowa)
    size = (size + 7) & ~7ULL;

    BlockHeader* current = heap_head;

    while (current != nullptr) {
        // Zabezpieczenie: SprawdŸ czy sterta nie zosta³a uszkodzona!
        if (current->magic != HEAP_MAGIC) {
            // Tutaj w przysz³oœci wywo³asz swój kernel_panic()
            while (1) asm volatile("cli; hlt");
        }

        if (!current->used && current->size >= size) {

            uint64_t min_split = sizeof(BlockHeader) + 16;

            // Dzielimy blok
            if (current->size >= size + min_split) {
                // Bezpieczna arytmetyka na adresach
                uintptr_t next_addr = reinterpret_cast<uintptr_t>(current) + sizeof(BlockHeader) + size;
                BlockHeader* new_block = reinterpret_cast<BlockHeader*>(next_addr);

                new_block->magic = HEAP_MAGIC; // Wa¿ne! Nowy blok te¿ musi mieæ magiê
                new_block->size = current->size - size - sizeof(BlockHeader);
                new_block->used = 0;
                new_block->next = current->next;
                new_block->prev = current;

                if (current->next != nullptr) {
                    current->next->prev = new_block;
                }

                current->next = new_block;
                current->size = size;
            }

            current->used = 1;

            // Zwróæ wskaŸnik NA DANE
            uintptr_t data_addr = reinterpret_cast<uintptr_t>(current) + sizeof(BlockHeader);
            return reinterpret_cast<void*>(data_addr);
        }
        current = current->next;
    }

    return nullptr; // OOM (Out of Memory)
}

extern "C" void free(void* ptr) {
    if (ptr == nullptr) return;

    // Cofnij siê do nag³ówka
    uintptr_t header_addr = reinterpret_cast<uintptr_t>(ptr) - sizeof(BlockHeader);
    BlockHeader* block = reinterpret_cast<BlockHeader*>(header_addr);

    // BARDZO WA¯NE: Weryfikacja czy wskaŸnik faktycznie pochodzi z malloc!
    if (block->magic != HEAP_MAGIC) {
        // Ktoœ próbuje zwolniæ z³y wskaŸnik (Double Free, invalid pointer, itp.)
        // Kernel panic!
        while (1) asm volatile("cli; hlt");
    }

    block->used = 0;

    // Scal z nastêpnym blokiem jeœli jest wolny
    if (block->next != nullptr && !block->next->used) {
        BlockHeader* next = block->next;
        block->size += sizeof(BlockHeader) + next->size;
        block->next = next->next;
        if (next->next != nullptr) {
            next->next->prev = block;
        }
        // Magia next nie ulega zmianie/zniszczeniu, po prostu ignorujemy stary nag³ówek
    }

    // Scal z poprzednim blokiem jeœli jest wolny
    if (block->prev != nullptr && !block->prev->used) {
        BlockHeader* prev = block->prev;
        prev->size += sizeof(BlockHeader) + block->size;
        prev->next = block->next;
        if (block->next != nullptr) {
            block->next->prev = prev;
        }
    }
}

// Funkcje statystyczne
uint64_t memory_free_bytes() {
    uint64_t total = 0;
    BlockHeader* current = heap_head;
    while (current != nullptr) {
        if (!current->used && current->magic == HEAP_MAGIC) total += current->size;
        current = current->next;
    }
    return total;
}

uint64_t memory_used_bytes() {
    uint64_t total = 0;
    BlockHeader* current = heap_head;
    while (current != nullptr) {
        if (current->used && current->magic == HEAP_MAGIC) total += current->size;
        current = current->next;
    }
    return total;
}