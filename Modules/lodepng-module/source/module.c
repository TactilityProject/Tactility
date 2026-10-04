// SPDX-License-Identifier: Apache-2.0
#include <lodepng/lodepng.h>
#include <lodepng/module.h>

static const struct ModuleSymbol SYMBOLS[] = {
    // decode
    DEFINE_MODULE_SYMBOL(lodepng_decode_memory),
    DEFINE_MODULE_SYMBOL(lodepng_decode32),
    DEFINE_MODULE_SYMBOL(lodepng_decode24),
    DEFINE_MODULE_SYMBOL(lodepng_decode_file),
    DEFINE_MODULE_SYMBOL(lodepng_decode32_file),
    DEFINE_MODULE_SYMBOL(lodepng_decode24_file),
    // encode
    DEFINE_MODULE_SYMBOL(lodepng_encode_memory),
    DEFINE_MODULE_SYMBOL(lodepng_encode32),
    DEFINE_MODULE_SYMBOL(lodepng_encode24),
    DEFINE_MODULE_SYMBOL(lodepng_encode_file),
    DEFINE_MODULE_SYMBOL(lodepng_encode32_file),
    DEFINE_MODULE_SYMBOL(lodepng_encode24_file),
    // utilities
    DEFINE_MODULE_SYMBOL(lodepng_error_text),
    DEFINE_MODULE_SYMBOL(lodepng_load_file),
    DEFINE_MODULE_SYMBOL(lodepng_save_file),
    DEFINE_MODULE_SYMBOL(lodepng_malloc),
    DEFINE_MODULE_SYMBOL(lodepng_realloc),
    DEFINE_MODULE_SYMBOL(lodepng_free),
    MODULE_SYMBOL_TERMINATOR,
};

struct Module lodepng_module = {
    .name = "lodepng",
    .start = NULL,
    .stop = NULL,
    .drivers = NULL,
    .symbols = SYMBOLS,
    .internal = NULL,
};
