// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/display.h>
#include <tactility/memory.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PixelBuffer;

/** Allocates a new buffer, width x height pixels, in the given pixel format. External-RAM
 * preferred, 64-byte aligned. Returns NULL on allocation failure or invalid dimensions. */
struct PixelBuffer* pixel_buffer_create(enum DisplayColorFormat format, int width, int height);

/** Like pixel_buffer_create(), but allocates via the given policy instead of the external-RAM-
 * preferred, 64-byte-aligned default. */
struct PixelBuffer* pixel_buffer_create_with_policy(enum DisplayColorFormat format, int width, int height, const struct MemoryPolicy* policy);

/**
 * Wraps existing memory (e.g. a driver's own hw frame buffer, or a sub-rect of one) without
 * owning it - pixel_buffer_free() releases only the handle, not this memory.
 * @param[in] stride_bytes 0 for the tightly-packed default, or an explicit value when wrapping a
 * sub-rectangle of something larger
 */
struct PixelBuffer* pixel_buffer_wrap(void* data, enum DisplayColorFormat format, int width, int height, size_t stride_bytes);

/** Frees the handle; also frees the underlying memory if it was allocated by pixel_buffer_create(). */
void pixel_buffer_free(struct PixelBuffer* buffer);

enum DisplayColorFormat pixel_buffer_get_format(const struct PixelBuffer* buffer);
int pixel_buffer_get_width(const struct PixelBuffer* buffer);
int pixel_buffer_get_height(const struct PixelBuffer* buffer);
size_t pixel_buffer_get_stride_bytes(const struct PixelBuffer* buffer);
void* pixel_buffer_get_data(struct PixelBuffer* buffer);

/** Pointer to the start of row y, for bulk/row-based writes (e.g. font glyph blitting). */
void* pixel_buffer_get_row(struct PixelBuffer* buffer, int y);

/** Zeroes the whole buffer. */
void pixel_buffer_clear(struct PixelBuffer* buffer);

/**
 * Flushes CPU cache writes for the width x height pixel rect at (x, y) out to memory (cache-to-
 * memory writeback) so DMA/hardware reading the buffer sees the latest data. No-op when not built
 * for ESP-IDF. Pass (0, 0, pixel_buffer_get_width(buffer), pixel_buffer_get_height(buffer)) to
 * sync the whole buffer. For MONOCHROME buffers, x and width must be multiples of 8.
 */
void pixel_buffer_msync(struct PixelBuffer* buffer, int x, int y, int width, int height);

/**
 * How a richer colour reduces to a lossy target format (matters for MONOCHROME destinations, and
 * for GRAYSCALE8's choice of averaging formula - the RGB565/BGR565/RGB565_SWAPPED/BGR565_SWAPPED/
 * RGB888 conversions are all direct/unambiguous and ignore this).
 */
enum PixelBufferConversion {
    /** MONOCHROME: only exact black (RGB565 0x0000) is ink-off, everything else is ink-on.
     * GRAYSCALE8: same as PIXEL_BUFFER_CONVERSION_LUMA_THRESHOLD. */
    PIXEL_BUFFER_CONVERSION_EXACT_BLACK,
    /** Perceptual luminance (299/587/114-weighted). MONOCHROME: thresholded at 128 for ink
     * on/off. GRAYSCALE8: kept as the full byte. */
    PIXEL_BUFFER_CONVERSION_LUMA_THRESHOLD,
    /** Unweighted (r+g+b)/3. MONOCHROME: thresholded at 128 for ink on/off. GRAYSCALE8: kept as
     * the full byte. */
    PIXEL_BUFFER_CONVERSION_AVERAGE,
};

/** Sets one pixel from an RGB565 colour, converting into the buffer's native format per
 * `conversion` where that's ambiguous (see PixelBufferConversion). Out-of-bounds is a no-op. */
void pixel_buffer_set_pixel_rgb565(struct PixelBuffer* buffer, int x, int y, uint16_t rgb565, enum PixelBufferConversion conversion);

/** Reads one pixel back as RGB565, regardless of the buffer's native format. Always unambiguous
 * (e.g. MONOCHROME ink/off map directly to white/black), so no conversion flag needed.
 * Out-of-bounds returns 0. */
uint16_t pixel_buffer_get_pixel_rgb565(const struct PixelBuffer* buffer, int x, int y);

/**
 * Copies a width x height region from src (src_x, src_y) into dst (dst_x, dst_y), converting
 * between their native formats as needed (per `conversion` where ambiguous) - e.g. moving
 * glyph/sprite data rendered in one format into a scratch buffer of another, or into a driver's
 * own hw frame buffer.
 */
void pixel_buffer_blit(struct PixelBuffer* dst, int dst_x, int dst_y,
                        const struct PixelBuffer* src, int src_x, int src_y, int width, int height,
                        enum PixelBufferConversion conversion);

/** Bytes per row for `width` pixels in `format`: (width+7)/8 for MONOCHROME, width for
 * GRAYSCALE8, width*2 for the RGB565/BGR565 family, width*3 for RGB888. */
size_t pixel_buffer_row_stride_bytes(enum DisplayColorFormat format, int width);

#ifdef __cplusplus
}
#endif
