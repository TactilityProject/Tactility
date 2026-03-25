// SPDX-License-Identifier: Apache-2.0
#include <drivers/rx8130ce.h>
#include <tactility/module.h>

const struct ModuleSymbol rx8130ce_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(rx8130ce_get_datetime),
    DEFINE_MODULE_SYMBOL(rx8130ce_set_datetime),
    MODULE_SYMBOL_TERMINATOR
};
