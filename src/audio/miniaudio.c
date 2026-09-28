// miniaudio's code, with Ogg Vorbis decoding from stb_vorbis (which ships with miniaudio). The order is miniaudio's
// own recipe: stb_vorbis's declarations first, then miniaudio, which turns Vorbis on when it sees them, then
// stb_vorbis's code.
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
