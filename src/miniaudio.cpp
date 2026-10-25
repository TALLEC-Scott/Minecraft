// OGG Vorbis decoding: miniaudio picks up stb_vorbis when its header part
// is visible before the miniaudio implementation; the stb_vorbis
// implementation follows (miniaudio's documented integration).
#define STB_VORBIS_HEADER_ONLY
#include <stb/stb_vorbis.c>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#undef STB_VORBIS_HEADER_ONLY
#include <stb/stb_vorbis.c>
