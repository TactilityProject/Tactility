// SPDX-License-Identifier: Apache-2.0
#include <graphics/pixel_buffer.h>

#include <tactility/memory.h>

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_cache.h>
#endif

struct PixelBuffer {
    enum DisplayColorFormat format;
    int width;
    int height;
    size_t stride_bytes;
    uint8_t* data;
    bool owns_data;
};

size_t pixel_buffer_row_stride_bytes(enum DisplayColorFormat format, int width) {
    switch (format) {
        case DISPLAY_COLOR_FORMAT_MONOCHROME:
            return ((size_t)width + 7) / 8;
        case DISPLAY_COLOR_FORMAT_GRAYSCALE8:
            return (size_t)width;
        case DISPLAY_COLOR_FORMAT_RGB888:
            return (size_t)width * 3;
        case DISPLAY_COLOR_FORMAT_BGR565:
        case DISPLAY_COLOR_FORMAT_BGR565_SWAPPED:
        case DISPLAY_COLOR_FORMAT_RGB565:
        case DISPLAY_COLOR_FORMAT_RGB565_SWAPPED:
        default:
            return (size_t)width * 2;
    }
}

struct PixelBuffer* pixel_buffer_create(enum DisplayColorFormat format, int width, int height) {
    const struct MemoryPolicy policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 64 };
    return pixel_buffer_create_with_policy(format, width, height, &policy);
}

struct PixelBuffer* pixel_buffer_create_with_policy(enum DisplayColorFormat format, int width, int height, const struct MemoryPolicy* policy) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    struct PixelBuffer* buffer = malloc(sizeof(struct PixelBuffer));
    if (buffer == NULL) {
        return NULL;
    }

    const size_t stride = pixel_buffer_row_stride_bytes(format, width);
    const size_t logical_bytes = stride * (size_t)height;
    // Padded up to a cache-line multiple (64), not just the logical size: a caller that msyncs
    // this buffer (e.g. before DMA) needs both the address and the size of the sync region to be
    // a multiple of the cache line - rounding the logical size up for that sync call is only safe
    // if the allocation itself has that much room.
    const size_t bytes = (logical_bytes + 63) & ~(size_t)63;
    uint8_t* data = memory_alloc_with_policy(bytes, policy);
    if (data == NULL) {
        free(buffer);
        return NULL;
    }
    memset(data, 0, bytes);

    buffer->format = format;
    buffer->width = width;
    buffer->height = height;
    buffer->stride_bytes = stride;
    buffer->data = data;
    buffer->owns_data = true;
    return buffer;
}

struct PixelBuffer* pixel_buffer_wrap(void* data, enum DisplayColorFormat format, int width, int height, size_t stride_bytes) {
    if (data == NULL || width <= 0 || height <= 0) {
        return NULL;
    }

    struct PixelBuffer* buffer = malloc(sizeof(struct PixelBuffer));
    if (buffer == NULL) {
        return NULL;
    }

    buffer->format = format;
    buffer->width = width;
    buffer->height = height;
    buffer->stride_bytes = stride_bytes != 0 ? stride_bytes : pixel_buffer_row_stride_bytes(format, width);
    buffer->data = (uint8_t*)data;
    buffer->owns_data = false;
    return buffer;
}

void pixel_buffer_free(struct PixelBuffer* buffer) {
    if (buffer == NULL) {
        return;
    }
    if (buffer->owns_data) {
        memory_free(buffer->data);
    }
    free(buffer);
}

enum DisplayColorFormat pixel_buffer_get_format(const struct PixelBuffer* buffer) {
    return buffer->format;
}

int pixel_buffer_get_width(const struct PixelBuffer* buffer) {
    return buffer->width;
}

int pixel_buffer_get_height(const struct PixelBuffer* buffer) {
    return buffer->height;
}

size_t pixel_buffer_get_stride_bytes(const struct PixelBuffer* buffer) {
    return buffer->stride_bytes;
}

void* pixel_buffer_get_data(struct PixelBuffer* buffer) {
    return buffer->data;
}

void* pixel_buffer_get_row(struct PixelBuffer* buffer, int y) {
    return buffer->data + (size_t)y * buffer->stride_bytes;
}

void pixel_buffer_clear(struct PixelBuffer* buffer) {
    memset(buffer->data, 0, buffer->stride_bytes * (size_t)buffer->height);
}

void pixel_buffer_msync(struct PixelBuffer* buffer, int x, int y, int width, int height) {
#ifdef ESP_PLATFORM
    const bool contiguous = x == 0 && width == buffer->width;
    if (contiguous) {
        size_t bytes = buffer->stride_bytes * (size_t)height;
        if (buffer->owns_data) {
            // pixel_buffer_create()'s allocation is padded up to this same cache-line multiple.
            bytes = (bytes + 63) & ~(size_t)63;
        }
        esp_cache_msync(pixel_buffer_get_row(buffer, y), bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    } else {
        const size_t rowBytes = pixel_buffer_row_stride_bytes(buffer->format, width);
        const size_t xBytes = pixel_buffer_row_stride_bytes(buffer->format, x);
        for (int row = 0; row < height; row++) {
            uint8_t* rowPtr = (uint8_t*)pixel_buffer_get_row(buffer, y + row) + xBytes;
            esp_cache_msync(rowPtr, rowBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        }
    }
#else
    (void)buffer;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
#endif
}

// ============ RGB565 conversion helpers ============

static inline uint16_t rgb565_byteswap(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

// Swapping the 5-bit R and B fields is its own inverse, so this also converts BGR565 -> RGB565.
static inline uint16_t rgb565_swap_r_b(uint16_t v) {
    const uint16_t r = (v >> 11) & 0x1F;
    const uint16_t g = (v >> 5) & 0x3F;
    const uint16_t b = v & 0x1F;
    return (uint16_t)((b << 11) | (g << 5) | r);
}

static inline void rgb565_to_rgb888(uint16_t v, uint8_t out[3]) {
    const uint32_t r5 = (v >> 11) & 0x1F;
    const uint32_t g6 = (v >> 5) & 0x3F;
    const uint32_t b5 = v & 0x1F;
    out[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    out[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    out[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
}

static inline uint16_t rgb888_to_rgb565(const uint8_t rgb[3]) {
    return (uint16_t)(((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3));
}

static inline uint8_t rgb565_to_luma8(uint16_t v) {
    uint8_t rgb[3];
    rgb565_to_rgb888(v, rgb);
    const uint32_t luma = (uint32_t)rgb[0] * 299 + (uint32_t)rgb[1] * 587 + (uint32_t)rgb[2] * 114;
    return (uint8_t)(luma / 1000);
}

static inline uint8_t rgb565_to_average8(uint16_t v) {
    uint8_t rgb[3];
    rgb565_to_rgb888(v, rgb);
    return (uint8_t)(((uint16_t)rgb[0] + (uint16_t)rgb[1] + (uint16_t)rgb[2]) / 3);
}

static inline uint8_t rgb565_to_gray8(uint16_t v, enum PixelBufferConversion conversion) {
    return conversion == PIXEL_BUFFER_CONVERSION_AVERAGE ? rgb565_to_average8(v) : rgb565_to_luma8(v);
}

static inline bool rgb565_to_ink(uint16_t v, enum PixelBufferConversion conversion) {
    if (conversion == PIXEL_BUFFER_CONVERSION_EXACT_BLACK) {
        return v != 0x0000;
    }
    return rgb565_to_gray8(v, conversion) >= 128; // LUMA_THRESHOLD or AVERAGE
}

// ============ Pixel access ============

void pixel_buffer_set_pixel_rgb565(struct PixelBuffer* buffer, int x, int y, uint16_t rgb565, enum PixelBufferConversion conversion) {
    if (x < 0 || y < 0 || x >= buffer->width || y >= buffer->height) {
        return;
    }
    uint8_t* row = pixel_buffer_get_row(buffer, y);

    switch (buffer->format) {
        case DISPLAY_COLOR_FORMAT_RGB565:
            ((uint16_t*)row)[x] = rgb565;
            break;
        case DISPLAY_COLOR_FORMAT_RGB565_SWAPPED:
            ((uint16_t*)row)[x] = rgb565_byteswap(rgb565);
            break;
        case DISPLAY_COLOR_FORMAT_BGR565:
            ((uint16_t*)row)[x] = rgb565_swap_r_b(rgb565);
            break;
        case DISPLAY_COLOR_FORMAT_BGR565_SWAPPED:
            ((uint16_t*)row)[x] = rgb565_byteswap(rgb565_swap_r_b(rgb565));
            break;
        case DISPLAY_COLOR_FORMAT_RGB888:
            rgb565_to_rgb888(rgb565, &row[x * 3]);
            break;
        case DISPLAY_COLOR_FORMAT_GRAYSCALE8:
            row[x] = rgb565_to_gray8(rgb565, conversion);
            break;
        case DISPLAY_COLOR_FORMAT_MONOCHROME:
        default: {
            const uint8_t mask = (uint8_t)(0x80U >> (x % 8));
            if (rgb565_to_ink(rgb565, conversion)) {
                row[x / 8] |= mask;
            } else {
                row[x / 8] &= (uint8_t)~mask;
            }
            break;
        }
    }
}

uint16_t pixel_buffer_get_pixel_rgb565(const struct PixelBuffer* buffer, int x, int y) {
    if (x < 0 || y < 0 || x >= buffer->width || y >= buffer->height) {
        return 0;
    }
    const uint8_t* row = &buffer->data[(size_t)y * buffer->stride_bytes];

    switch (buffer->format) {
        case DISPLAY_COLOR_FORMAT_RGB565:
            return ((const uint16_t*)row)[x];
        case DISPLAY_COLOR_FORMAT_RGB565_SWAPPED:
            return rgb565_byteswap(((const uint16_t*)row)[x]);
        case DISPLAY_COLOR_FORMAT_BGR565:
            return rgb565_swap_r_b(((const uint16_t*)row)[x]);
        case DISPLAY_COLOR_FORMAT_BGR565_SWAPPED:
            return rgb565_swap_r_b(rgb565_byteswap(((const uint16_t*)row)[x]));
        case DISPLAY_COLOR_FORMAT_RGB888:
            return rgb888_to_rgb565(&row[x * 3]);
        case DISPLAY_COLOR_FORMAT_GRAYSCALE8: {
            const uint8_t rgb[3] = { row[x], row[x], row[x] };
            return rgb888_to_rgb565(rgb);
        }
        case DISPLAY_COLOR_FORMAT_MONOCHROME:
        default: {
            const uint8_t mask = (uint8_t)(0x80U >> (x % 8));
            return (row[x / 8] & mask) ? 0xFFFF : 0x0000;
        }
    }
}

void pixel_buffer_blit(struct PixelBuffer* dst, int dst_x, int dst_y,
                        const struct PixelBuffer* src, int src_x, int src_y, int width, int height,
                        enum PixelBufferConversion conversion) {
    if (dst->format == src->format) {
        // Same native format: a straight byte copy is both correct and much cheaper than
        // converting every pixel through RGB565 and back.
        const size_t row_bytes = pixel_buffer_row_stride_bytes(dst->format, width);
        const size_t dst_x_bytes = pixel_buffer_row_stride_bytes(dst->format, dst_x);
        const size_t src_x_bytes = pixel_buffer_row_stride_bytes(src->format, src_x);
        for (int row = 0; row < height; row++) {
            uint8_t* dst_row = dst->data + (size_t)(dst_y + row) * dst->stride_bytes + dst_x_bytes;
            const uint8_t* src_row = src->data + (size_t)(src_y + row) * src->stride_bytes + src_x_bytes;
            memcpy(dst_row, src_row, row_bytes);
        }
        return;
    }

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            const uint16_t rgb565 = pixel_buffer_get_pixel_rgb565(src, src_x + col, src_y + row);
            pixel_buffer_set_pixel_rgb565(dst, dst_x + col, dst_y + row, rgb565, conversion);
        }
    }
}
