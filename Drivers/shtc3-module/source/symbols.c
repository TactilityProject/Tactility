// SPDX-License-Identifier: Apache-2.0
#include <drivers/shtc3.h>
#include <tactility/module.h>

const struct ModuleSymbol shtc3_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(shtc3_read),
    MODULE_SYMBOL_TERMINATOR
};
