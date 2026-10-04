// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BinFontGeneratorConfig {
    /** Path to a TrueType (.ttf) file */
    const char* ttf_path;
    /** Font size in pixels (em height) */
    uint16_t size;
    /** Bits per pixel: 1, 2, 3 or 4 */
    uint8_t bpp;
    /** Codepoints to include. Codepoints that the TTF doesn't contain are skipped. */
    const uint32_t* codepoints;
    size_t codepoint_count;
} BinFontGeneratorConfig;

/**
 * @brief Rasterizes a TrueType font into the lv_font_conv binary format.
 * @param[in] config the generator configuration
 * @param[out] out_data the generated font file contents, to be released with memory_free()
 * @param[out] out_size the size of out_data in bytes
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT when the configuration is invalid
 * @retval ERROR_NOT_FOUND when the TTF file can't be read
 * @retval ERROR_NOT_SUPPORTED when the TTF file can't be parsed or contains none of the codepoints
 * @retval ERROR_OUT_OF_MEMORY when memory allocation failed
 */
error_t binfont_generate(const BinFontGeneratorConfig* config, uint8_t** out_data, size_t* out_size);

#ifdef __cplusplus
}
#endif
