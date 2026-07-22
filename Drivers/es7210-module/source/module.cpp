// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver es7210_driver;

static const Driver* es7210_drivers[] = {
    &es7210_driver,
    nullptr
};

extern const ModuleSymbol es7210_module_symbols[];

Module es7210_module = {
    .name = "es7210",
    .drivers = es7210_drivers,
    .symbols = es7210_module_symbols,
};

}
