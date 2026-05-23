// SPDX-License-Identifier: Apache-2.0
#include <drivers/ina226.h>
#include <tactility/module.h>

const struct ModuleSymbol ina226_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(ina226_read_bus_voltage),
    DEFINE_MODULE_SYMBOL(ina226_read_shunt_current),
    MODULE_SYMBOL_TERMINATOR
};
