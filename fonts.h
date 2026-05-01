#pragma once
#include "types.h"

struct EmbeddedFont {
    const char* name;
    unsigned char* data;
    unsigned char* end;
};

static inline uint64_t embedded_font_size(const EmbeddedFont* font) {
    return (uint64_t)(font->end - font->data);
}

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char _binary_inconsolata_ttf_start[];
extern unsigned char _binary_inconsolata_ttf_end[];
extern unsigned char _binary_roboto_ttf_start[];
extern unsigned char _binary_roboto_ttf_end[];
extern unsigned char _binary_tinos_ttf_start[];
extern unsigned char _binary_tinos_ttf_end[];

extern const EmbeddedFont G_EMBEDDED_FONTS[];
extern const int G_EMBEDDED_FONTS_COUNT;

#ifdef __cplusplus
}
#endif
