#pragma once

#include <stdint.h>

#define HAXORMEDIUM10_GLYPH_WIDTH 6
#define HAXORMEDIUM10_GLYPH_HEIGHT 11
#define HAXORMEDIUM10_GLYPH_BYTES_PER_ROW 1
#define HAXORMEDIUM10_GLYPH_FIRST 0x20
#define HAXORMEDIUM10_GLYPH_LAST 0x7E

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t haxormedium10_glyph_bitmap[];

#ifdef __cplusplus
}
#endif
