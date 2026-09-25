// SPDX-License-Identifier: Apache-2.0
#include <font/render.h>

#include <graphics/module.h>
#include <graphics/pixel_buffer.h>

static const struct ModuleSymbol SYMBOLS[] = {
    // font
    DEFINE_MODULE_SYMBOL(font_render_char_pixel_buffer_rgb565),
    // pixel_buffer
    DEFINE_MODULE_SYMBOL(pixel_buffer_create),
    DEFINE_MODULE_SYMBOL(pixel_buffer_create_with_policy),
    DEFINE_MODULE_SYMBOL(pixel_buffer_wrap),
    DEFINE_MODULE_SYMBOL(pixel_buffer_free),
    DEFINE_MODULE_SYMBOL(pixel_buffer_get_format),
    DEFINE_MODULE_SYMBOL(pixel_buffer_get_width),
    DEFINE_MODULE_SYMBOL(pixel_buffer_get_height),
    DEFINE_MODULE_SYMBOL(pixel_buffer_get_stride_bytes),
    DEFINE_MODULE_SYMBOL(pixel_buffer_get_data),
    DEFINE_MODULE_SYMBOL(pixel_buffer_get_row),
    DEFINE_MODULE_SYMBOL(pixel_buffer_clear),
    DEFINE_MODULE_SYMBOL(pixel_buffer_msync),
    DEFINE_MODULE_SYMBOL(pixel_buffer_set_pixel_rgb565),
    DEFINE_MODULE_SYMBOL(pixel_buffer_get_pixel_rgb565),
    DEFINE_MODULE_SYMBOL(pixel_buffer_blit),
    DEFINE_MODULE_SYMBOL(pixel_buffer_row_stride_bytes),
    MODULE_SYMBOL_TERMINATOR,
};

struct Module graphics_module = {
    .name = "graphics",
    .start = NULL,
    .stop = NULL,
    .drivers = NULL,
    .symbols = SYMBOLS,
    .internal = NULL,
};
