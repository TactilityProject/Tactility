// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <binfont/binfont.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opens a font file without loading it into memory: only the lookup tables are loaded and
 * glyph data is read from the file on demand. binfont_open_file() falls back to this when the file
 * doesn't fit in memory.
 */
error_t binfont_open_file_streaming(const char* path, struct BinFont** out);

#ifdef __cplusplus
}
#endif
