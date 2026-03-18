// SPDX-License-Identifier: Apache-2.0
#include <drivers/bm8563.h>
#include <tactility/module.h>

const struct ModuleSymbol bm8563_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(bm8563_get_datetime),
    DEFINE_MODULE_SYMBOL(bm8563_set_datetime),
    MODULE_SYMBOL_TERMINATOR
};
