// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver es8311_driver;

static const Driver* es8311_drivers[] = {
    &es8311_driver,
    nullptr
};

extern const ModuleSymbol es8311_module_symbols[];

Module es8311_module = {
    .name = "es8311",
    .drivers = es8311_drivers,
    .symbols = es8311_module_symbols
};

}
