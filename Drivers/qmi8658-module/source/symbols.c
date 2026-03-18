// SPDX-License-Identifier: Apache-2.0
#include <drivers/qmi8658.h>
#include <tactility/module.h>

const struct ModuleSymbol qmi8658_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(qmi8658_read),
    MODULE_SYMBOL_TERMINATOR
};
