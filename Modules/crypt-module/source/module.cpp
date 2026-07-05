// SPDX-License-Identifier: Apache-2.0
#include <tactility/crypt.h>
#include <tactility/hash.h>
#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

static const ModuleSymbol crypt_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(crypt_get_iv),
    DEFINE_MODULE_SYMBOL(crypt_generate_iv),
    DEFINE_MODULE_SYMBOL(crypt_encrypt),
    DEFINE_MODULE_SYMBOL(crypt_decrypt),
    DEFINE_MODULE_SYMBOL(djb2_str),
    DEFINE_MODULE_SYMBOL(djb2_data),
    MODULE_SYMBOL_TERMINATOR
};

Module crypt_module = {
    .name = "crypt",
    .start = start,
    .stop = stop,
    .symbols = crypt_module_symbols,
    .internal = nullptr
};

}
