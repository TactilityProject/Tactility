// SPDX-License-Identifier: Apache-2.0
#include <binfont/binfont.h>
#include <binfont/generator.h>
#include <binfont/module.h>
#include <binfont/render.h>

static const struct ModuleSymbol SYMBOLS[] = {
    // binfont
    DEFINE_MODULE_SYMBOL(binfont_open_file),
    DEFINE_MODULE_SYMBOL(binfont_open_memory),
    DEFINE_MODULE_SYMBOL(binfont_close),
    DEFINE_MODULE_SYMBOL(binfont_begin),
    DEFINE_MODULE_SYMBOL(binfont_end),
    DEFINE_MODULE_SYMBOL(binfont_get_metrics),
    DEFINE_MODULE_SYMBOL(binfont_get_glyph),
    DEFINE_MODULE_SYMBOL(binfont_get_kerning_x16),
    DEFINE_MODULE_SYMBOL(binfont_get_glyph_bitmap),
    // generator
    DEFINE_MODULE_SYMBOL(binfont_generate),
    DEFINE_MODULE_SYMBOL(binfont_get_ttf_codepoints),
    // render
    DEFINE_MODULE_SYMBOL(binfont_draw_glyph_rgb565),
    DEFINE_MODULE_SYMBOL(binfont_draw_text_rgb565),
    MODULE_SYMBOL_TERMINATOR,
};

struct Module binfont_module = {
    .name = "binfont",
    .start = NULL,
    .stop = NULL,
    .drivers = NULL,
    .symbols = SYMBOLS,
    .internal = NULL,
};
