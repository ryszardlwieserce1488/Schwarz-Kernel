#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

const EmbeddedFont G_EMBEDDED_FONTS[] = {
    { "consolas", _binary_consolas_ttf_start, _binary_consolas_ttf_size },
    { "inconsolata", _binary_inconsolata_ttf_start, _binary_inconsolata_ttf_size },
    { "segoeuithis", _binary_segoeuithis_ttf_start, _binary_segoeuithis_ttf_size },
    { "times", _binary_times_ttf_start, _binary_times_ttf_size },
};

const int G_EMBEDDED_FONTS_COUNT = 4;

#ifdef __cplusplus
}
#endif
