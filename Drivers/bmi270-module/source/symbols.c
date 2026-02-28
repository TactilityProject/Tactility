// SPDX-License-Identifier: Apache-2.0
#include <drivers/bmi270.h>
#include <tactility/module.h>

const struct ModuleSymbol bmi270_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(bmi270_read),
    MODULE_SYMBOL_TERMINATOR
};
