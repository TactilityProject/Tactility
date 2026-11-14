// SPDX-License-Identifier: Apache-2.0
#include <lvgl/svg.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>

#define SVG_DPI 96.0f
// (size_t)size * size * 4 must fit in size_t on 32-bit targets.
#define SVG_MAX_SIZE 32767u

lv_draw_buf_t* lvgl_svg_rasterize_data(const char* data, size_t length, uint32_t size) {
    if (size == 0 || size > SVG_MAX_SIZE) return NULL;

    // nanosvg modifies the text it parses
    char* text = malloc(length + 1);
    if (text == NULL) return NULL;
    memcpy(text, data, length);
    text[length] = '\0';
    NSVGimage* image = nsvgParse(text, "px", SVG_DPI);
    free(text);
    if (image == NULL) return NULL;

    const float longest_side = image->width > image->height ? image->width : image->height;
    if (longest_side <= 0.0f) {
        nsvgDelete(image);
        return NULL;
    }

    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    unsigned char* rgba = malloc((size_t)size * size * 4);
    lv_draw_buf_t* draw_buf = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if (rasterizer == NULL || rgba == NULL || draw_buf == NULL) {
        if (draw_buf != NULL) lv_draw_buf_destroy(draw_buf);
        free(rgba);
        if (rasterizer != NULL) nsvgDeleteRasterizer(rasterizer);
        nsvgDelete(image);
        return NULL;
    }

    const float scale = (float)size / longest_side;
    const float offset_x = ((float)size - image->width * scale) / 2.0f;
    const float offset_y = ((float)size - image->height * scale) / 2.0f;
    for (size_t i = 0; i < (size_t)size * size * 4; i++) {
        rgba[i] = 0;
    }
    nsvgRasterize(rasterizer, image, offset_x, offset_y, scale, rgba, (int)size, (int)size, (int)size * 4);

    // nanosvg outputs RGBA bytes, LVGL's ARGB8888 is BGRA in memory
    for (uint32_t y = 0; y < size; y++) {
        const unsigned char* source = rgba + (size_t)y * size * 4;
        lv_color32_t* target = (lv_color32_t*)(draw_buf->data + (size_t)y * draw_buf->header.stride);
        for (uint32_t x = 0; x < size; x++) {
            target[x].red = source[x * 4];
            target[x].green = source[x * 4 + 1];
            target[x].blue = source[x * 4 + 2];
            target[x].alpha = source[x * 4 + 3];
        }
    }

    free(rgba);
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);
    return draw_buf;
}

lv_draw_buf_t* lvgl_svg_rasterize_file(const char* path, uint32_t size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return NULL;

    lv_draw_buf_t* draw_buf = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        const long length = ftell(file);
        if (length > 0 && fseek(file, 0, SEEK_SET) == 0) {
            char* data = malloc((size_t)length);
            if (data != NULL) {
                if (fread(data, 1, (size_t)length, file) == (size_t)length) {
                    draw_buf = lvgl_svg_rasterize_data(data, (size_t)length, size);
                }
                free(data);
            }
        }
    }
    fclose(file);
    return draw_buf;
}

lv_draw_buf_t* lvgl_svg_rasterize_variants(
    const char* colour_data, size_t colour_length,
    const char* mono_data, size_t mono_length,
    bool prefer_mono, uint32_t size, bool* out_is_mono
) {
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool mono = prefer_mono == (attempt == 0);
        const char* data = mono ? mono_data : colour_data;
        const size_t length = mono ? mono_length : colour_length;
        if (data == NULL) continue;
        lv_draw_buf_t* draw_buf = lvgl_svg_rasterize_data(data, length, size);
        if (draw_buf != NULL) {
            if (out_is_mono != NULL) *out_is_mono = mono;
            return draw_buf;
        }
    }
    return NULL;
}

lv_draw_buf_t* lvgl_svg_rasterize_file_variants(
    const char* colour_path, const char* mono_path,
    bool prefer_mono, uint32_t size, bool* out_is_mono
) {
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool mono = prefer_mono == (attempt == 0);
        const char* path = mono ? mono_path : colour_path;
        if (path == NULL) continue;
        lv_draw_buf_t* draw_buf = lvgl_svg_rasterize_file(path, size);
        if (draw_buf != NULL) {
            if (out_is_mono != NULL) *out_is_mono = mono;
            return draw_buf;
        }
    }
    return NULL;
}

void lvgl_svg_round_corners(lv_draw_buf_t* draw_buf, uint32_t radius) {
    const uint32_t width = draw_buf->header.w;
    const uint32_t height = draw_buf->header.h;
    if (radius == 0) return;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;

    const float r = (float)radius;
    for (uint32_t dy = 0; dy < radius; dy++) {
        for (uint32_t dx = 0; dx < radius; dx++) {
            // Pixel centre relative to the centre of the corner circle
            const float px = r - (float)dx - 0.5f;
            const float py = r - (float)dy - 0.5f;
            const float distance = sqrtf(px * px + py * py);
            // One pixel wide linear edge
            float coverage = r - distance + 0.5f;
            if (coverage >= 1.0f) continue;
            if (coverage < 0.0f) coverage = 0.0f;

            const uint32_t xs[2] = { dx, width - 1 - dx };
            const uint32_t ys[2] = { dy, height - 1 - dy };
            for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                    lv_color32_t* row = (lv_color32_t*)(draw_buf->data + (size_t)ys[j] * draw_buf->header.stride);
                    row[xs[i]].alpha = (uint8_t)((float)row[xs[i]].alpha * coverage + 0.5f);
                }
            }
        }
    }
}
