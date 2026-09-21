#pragma once

#include <stdint.h>

#define HAXORNARROW18_GLYPH_WIDTH 10
#define HAXORNARROW18_GLYPH_HEIGHT 23
#define HAXORNARROW18_GLYPH_BYTES_PER_ROW 2
#define HAXORNARROW18_GLYPH_FIRST 0x20
#define HAXORNARROW18_GLYPH_LAST 0x7E

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t haxornarrow18_glyph_bitmap[];

#ifdef __cplusplus
}
#endif
