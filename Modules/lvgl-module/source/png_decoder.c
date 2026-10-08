// SPDX-License-Identifier: MIT
// Based on LVGL's lv_lodepng.c (MIT license, Copyright (c) 2020 LVGL Kft), adapted to decode with lodepng-module.

#include <png_decoder.h>

#include <lodepng/lodepng.h>

#include <lvgl.h>
#include <src/core/lv_global.h>
#include <src/image/lv_image_decoder_private.h>

#define DECODER_NAME "LODEPNG"

#define image_cache_draw_buf_handlers &(LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers)

static const uint8_t PNG_MAGIC[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };

static uint32_t read_u32_big_endian(const uint8_t* bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

static lv_result_t decoder_info(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc, lv_image_header_t* header) {
    LV_UNUSED(decoder);

    // Width and height are at bytes 16..23 of a PNG file, in big endian
    uint8_t buffer[24];
    if (dsc->src_type == LV_IMAGE_SRC_FILE) {
        uint32_t read_count;
        lv_fs_read(&dsc->file, buffer, sizeof(buffer), &read_count);
        if (read_count != sizeof(buffer)) return LV_RESULT_INVALID;
    } else if (dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t* image_dsc = dsc->src;
        if (image_dsc->data_size < sizeof(buffer)) return LV_RESULT_INVALID;
        lv_memcpy(buffer, image_dsc->data, sizeof(buffer));
    } else {
        return LV_RESULT_INVALID;
    }

    if (lv_memcmp(buffer, PNG_MAGIC, sizeof(PNG_MAGIC)) != 0) return LV_RESULT_INVALID;

    header->cf = LV_COLOR_FORMAT_ARGB8888;
    header->w = read_u32_big_endian(&buffer[16]);
    header->h = read_u32_big_endian(&buffer[20]);
    return LV_RESULT_OK;
}

/** Reads a whole file through lv_fs, so LVGL drive letter paths ("A:...") work */
static uint8_t* load_file(const char* path, size_t* out_size) {
    lv_fs_file_t file;
    if (lv_fs_open(&file, path, LV_FS_MODE_RD) != LV_FS_RES_OK) return NULL;

    uint8_t* data = NULL;
    uint32_t size = 0;
    if (lv_fs_seek(&file, 0, LV_FS_SEEK_END) == LV_FS_RES_OK &&
        lv_fs_tell(&file, &size) == LV_FS_RES_OK &&
        size > 0 &&
        lv_fs_seek(&file, 0, LV_FS_SEEK_SET) == LV_FS_RES_OK) {
        data = lodepng_malloc(size);
        uint32_t read_count = 0;
        if (data != NULL && (lv_fs_read(&file, data, size, &read_count) != LV_FS_RES_OK || read_count != size)) {
            lodepng_free(data);
            data = NULL;
        }
    }
    lv_fs_close(&file);
    *out_size = size;
    return data;
}

/** Decodes a PNG into an ARGB8888 draw buffer */
static lv_draw_buf_t* decode_png_data(const uint8_t* png_data, size_t png_data_size) {
    unsigned char* rgba = NULL;
    unsigned width;
    unsigned height;
    const unsigned error = lodepng_decode32(&rgba, &width, &height, png_data, png_data_size);
    if (error) {
        LV_LOG_WARN("error %u: %s", error, lodepng_error_text(error));
        lodepng_free(rgba);
        return NULL;
    }

    lv_draw_buf_t* decoded = lv_draw_buf_create_ex(image_cache_draw_buf_handlers, width, height, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if (decoded != NULL) {
        // lodepng outputs RGBA bytes, LVGL's ARGB8888 is BGRA in memory
        for (unsigned y = 0; y < height; y++) {
            const unsigned char* source = rgba + (size_t)y * width * 4;
            lv_color32_t* target = (lv_color32_t*)(decoded->data + (size_t)y * decoded->header.stride);
            for (unsigned x = 0; x < width; x++) {
                target[x].red = source[x * 4];
                target[x].green = source[x * 4 + 1];
                target[x].blue = source[x * 4 + 2];
                target[x].alpha = source[x * 4 + 3];
            }
        }
    }

    lodepng_free(rgba);
    return decoded;
}

static lv_result_t decoder_open(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc) {
    LV_PROFILER_DECODER_BEGIN_TAG("png_decoder_open");

    lv_draw_buf_t* decoded = NULL;
    if (dsc->src_type == LV_IMAGE_SRC_FILE) {
        size_t size = 0;
        uint8_t* data = load_file(dsc->src, &size);
        if (data == NULL) {
            LV_LOG_WARN("Failed to load %s", (const char*)dsc->src);
            LV_PROFILER_DECODER_END_TAG("png_decoder_open");
            return LV_RESULT_INVALID;
        }
        decoded = decode_png_data(data, size);
        lodepng_free(data);
    } else if (dsc->src_type == LV_IMAGE_SRC_VARIABLE) {
        const lv_image_dsc_t* image_dsc = dsc->src;
        decoded = decode_png_data(image_dsc->data, image_dsc->data_size);
    }

    if (decoded == NULL) {
        LV_LOG_WARN("Error decoding PNG");
        LV_PROFILER_DECODER_END_TAG("png_decoder_open");
        return LV_RESULT_INVALID;
    }

    lv_draw_buf_t* adjusted = lv_image_decoder_post_process(dsc, decoded);
    if (adjusted == NULL) {
        lv_draw_buf_destroy(decoded);
        LV_PROFILER_DECODER_END_TAG("png_decoder_open");
        return LV_RESULT_INVALID;
    }

    // The adjusted draw buffer is newly allocated
    if (adjusted != decoded) {
        lv_draw_buf_destroy(decoded);
        decoded = adjusted;
    }

    dsc->decoded = decoded;

    if (dsc->args.no_cache || !lv_image_cache_is_enabled()) {
        LV_PROFILER_DECODER_END_TAG("png_decoder_open");
        return LV_RESULT_OK;
    }

    lv_image_cache_data_t search_key;
    search_key.src_type = dsc->src_type;
    search_key.src = dsc->src;
    search_key.slot.size = decoded->data_size;

    lv_cache_entry_t* entry = lv_image_decoder_add_to_cache(decoder, &search_key, decoded, NULL);
    if (entry == NULL) {
        LV_PROFILER_DECODER_END_TAG("png_decoder_open");
        return LV_RESULT_INVALID;
    }
    dsc->cache_entry = entry;

    LV_PROFILER_DECODER_END_TAG("png_decoder_open");
    return LV_RESULT_OK;
}

static void decoder_close(lv_image_decoder_t* decoder, lv_image_decoder_dsc_t* dsc) {
    LV_UNUSED(decoder);
    if (dsc->args.no_cache || !lv_image_cache_is_enabled()) {
        lv_draw_buf_destroy((lv_draw_buf_t*)dsc->decoded);
    }
}

void lvgl_png_decoder_init(void) {
    lv_image_decoder_t* decoder = lv_image_decoder_create();
    lv_image_decoder_set_info_cb(decoder, decoder_info);
    lv_image_decoder_set_open_cb(decoder, decoder_open);
    lv_image_decoder_set_close_cb(decoder, decoder_close);
    decoder->name = DECODER_NAME;
}
