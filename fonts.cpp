#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

const EmbeddedFont G_EMBEDDED_FONTS[] = {
    { "inconsolata", _binary_inconsolata_ttf_start, _binary_inconsolata_ttf_end },
    { "roboto", _binary_roboto_ttf_start, _binary_roboto_ttf_end },
    { "tinos", _binary_tinos_ttf_start, _binary_tinos_ttf_end },
};

const int G_EMBEDDED_FONTS_COUNT = 3;

#ifdef __cplusplus
}
#endif
