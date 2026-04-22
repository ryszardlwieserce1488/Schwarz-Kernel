#pragma once
#include <stdint.h>

struct EmbeddedFont {
    const char* name;
    unsigned char* data;
    uint64_t size;
};

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char _binary_consolas_ttf_start[];
extern uint64_t _binary_consolas_ttf_size;
extern unsigned char _binary_inconsolata_ttf_start[];
extern uint64_t _binary_inconsolata_ttf_size;
extern unsigned char _binary_segoeuithis_ttf_start[];
extern uint64_t _binary_segoeuithis_ttf_size;
extern unsigned char _binary_times_ttf_start[];
extern uint64_t _binary_times_ttf_size;

#ifdef __cplusplus
}
#endif

extern const EmbeddedFont G_EMBEDDED_FONTS[];
extern const int G_EMBEDDED_FONTS_COUNT;
