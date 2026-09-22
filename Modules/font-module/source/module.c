// SPDX-License-Identifier: Apache-2.0
#include <font/module.h>
#include <font/render.h>

static const struct ModuleSymbol SYMBOLS[] = {
    DEFINE_MODULE_SYMBOL(font_render_char),
    DEFINE_MODULE_SYMBOL(font_render_string),
    DEFINE_MODULE_SYMBOL(font_get_string_length),
    MODULE_SYMBOL_TERMINATOR
};

struct Module font_module = {
    .name = "font",
    .start = NULL,
    .stop = NULL,
    .drivers = NULL,
    .symbols = SYMBOLS,
    .internal = NULL,
};
