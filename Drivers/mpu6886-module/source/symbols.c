// SPDX-License-Identifier: Apache-2.0
#include <drivers/mpu6886.h>
#include <tactility/module.h>

const struct ModuleSymbol mpu6886_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(mpu6886_read),
    MODULE_SYMBOL_TERMINATOR
};
