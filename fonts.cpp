#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

const EmbeddedFont G_EMBEDDED_FONTS[] = {
    { "consolas", _binary_consolas_ttf_start, _binary_consolas_ttf_end },
    { "inconsolata", _binary_inconsolata_ttf_start, _binary_inconsolata_ttf_end },
    { "segoeuithis", _binary_segoeuithis_ttf_start, _binary_segoeuithis_ttf_end },
    { "times", _binary_times_ttf_start, _binary_times_ttf_end },
};

const int G_EMBEDDED_FONTS_COUNT = 4;

#ifdef __cplusplus
}
#endif
